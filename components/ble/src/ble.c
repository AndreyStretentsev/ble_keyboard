//
// Created by Egor Kryndach on 2020-06-26.
//

//MARK: Import common headers
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"

#include "esp_hidd_prf_api.h"
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "hid_dev.h"

//MARK: Import component header
#include "ble.h"

//MARK: Private macros and constants
#define TAG "BLE"

/** "Keepalive" rate when in idle (no HID commands)
 * @note Microseconds!
 * @see timestampLastSent
 * @see periodicHIDCallback */
#define HID_IDLE_UPDATE_RATE 200000

/** @brief Event bit, set if pairing is enabled
 * @note If MODULE_BT_PAIRING ist set in menuconfig, this bit is disable by default
 * and can be enabled via $PM1 , disabled via $PM0.
 * If MODULE_BT_PAIRING is not set, this bit will be set on boot.*/
#define SYSTEM_PAIRING_ENABLED (1 << 0)

/** @brief Event bit, set if the ESP32 is currently advertising.
 *
 * Used for determining if we need to set advertising params again,
 * when the pairing mode is changed. */
#define SYSTEM_CURRENTLY_ADVERTISING (1 << 1)

//MARK: Private types

//MARK: Declaration of the private opaque structs

//MARK: Public global variables

//MARK: Private global variables
static uint16_t hid_conn_id = 0;
static bool sec_conn = false;
static bool send_volum_up = false;

/** Timestamp of last sent HID packet, used for idle sending timer callback 
 * @see periodicHIDCallback */
uint64_t timestampLastSent;

static void hidd_event_callback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param);

static config_data_t config;

//a list of active HID connections.
//index is the hid_conn_id.
esp_bd_addr_t active_connections[CONFIG_BT_ACL_CONNECTIONS] = {0};

static uint8_t manufacturer[12] = {'B', 'l', 'a', 'c', 'k', 'B', 'r', 'i', 'c', 'k', 's'};

static uint8_t hidd_service_uuid128[] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    //first uuid, 16bit, [12],[13] is the value
    0xfb,
    0x34,
    0x9b,
    0x5f,
    0x80,
    0x00,
    0x00,
    0x80,
    0x00,
    0x10,
    0x00,
    0x00,
    0x12,
    0x18,
    0x00,
    0x00,
};

/** @brief Event group for system status */
EventGroupHandle_t eventgroup_system;

static esp_ble_adv_data_t hidd_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x000A, //slave connection min interval, Time = min_interval * 1.25 msec
    .max_interval = 0x0010, //slave connection max interval, Time = max_interval * 1.25 msec
    .appearance = 0x03c0,   //HID Generic,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(hidd_service_uuid128),
    .p_service_uuid = hidd_service_uuid128,
    .flag = 0x6,
};

static esp_ble_scan_params_t scan_params = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x00A0, /* 100ms (n*0.625ms)*/
    .scan_window = 0x0090,   /* 90ms */
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
};

// config scan response data
///@todo Scan response is currently not used. If used, add state handling (adv start) according to ble/gatt_security_server example of Espressif
static esp_ble_adv_data_t hidd_adv_resp = {
    .set_scan_rsp = true,
    .include_name = true,
    .manufacturer_len = sizeof(manufacturer),
    .p_manufacturer_data = manufacturer,
};

static esp_ble_adv_params_t hidd_adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x30,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    //.peer_addr            =
    //.peer_addr_type       =
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

//MARK: Private functions

/** Periodic sending of empty HID reports if no updates are sent via API */
static void periodicHIDCallback(void *arg)
{
    if (abs(esp_timer_get_time() - timestampLastSent) > HID_IDLE_UPDATE_RATE)
    {
        //send empty report (but with last known button state)
        // esp_hidd_send_mouse_value(hid_conn_id, mouseButtons, 0, 0, 0);
        //save timestamp for next call
        timestampLastSent = esp_timer_get_time();
        ESP_LOGI(TAG, "Idle...");
    }
}

static void hidd_event_callback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param)
{
    switch (event)
    {
    case ESP_HIDD_EVENT_REG_FINISH:
    {
        if (param->init_finish.state == ESP_HIDD_INIT_OK)
        {
            esp_ble_gap_set_device_name(config.bt_device_name);
            esp_ble_gap_config_adv_data(&hidd_adv_data);
        }
        break;
    }
    case ESP_BAT_EVENT_REG:
    {
        break;
    }
    case ESP_HIDD_EVENT_DEINIT_FINISH:
        break;
    case ESP_HIDD_EVENT_BLE_CONNECT:
    {
        ESP_LOGI(TAG, "ESP_HIDD_EVENT_BLE_CONNECT");
        hid_conn_id = param->connect.conn_id;

        //save currently connecting device to the list.
        //this list is used to switch between connected devices when sending HID packets
        if (hid_conn_id < CONFIG_BT_ACL_CONNECTIONS)
        {
            memcpy(active_connections[hid_conn_id], param->connect.remote_bda, sizeof(esp_bd_addr_t));
        }
        else
        {
            ESP_LOGE(TAG, "Oups, hid_conn_id too high!");
        }

        //because some devices do connect with a quite high connection
        //interval, we might have a congested channel...
        //to overcome this issue, we update the connection parameters here
        //to use a very low connection interval.
        esp_ble_conn_update_params_t new;
        memcpy(new.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        new.min_int = 6;
        new.max_int = 6;
        new.latency = 0;
        new.timeout = 500;
        esp_ble_gap_update_conn_params(&new);

        //to allow more connections, we simply restart the adv process.
        esp_ble_gap_start_advertising(&hidd_adv_params);
        //xEventGroupClearBits(eventgroup_system, SYSTEM_CURRENTLY_ADVERTISING);
        break;
    }
    case ESP_HIDD_EVENT_BLE_DISCONNECT:
    {
        for (uint8_t i = 0; i < CONFIG_BT_ACL_CONNECTIONS; i++)
        {
            //check if this addr is in the array
            if (memcmp(active_connections[i], param->disconnect.remote_bda, sizeof(esp_bd_addr_t)) == 0)
            {
                //clear element
                memset(active_connections[i], 0, sizeof(esp_bd_addr_t));
                //last connection
                if (i == 0)
                    sec_conn = false;

                //TODO: currently we the first connection id after disconnect.
                //maybe we should do it differently?
                if (i != 0)
                    hid_conn_id = 0;

                ESP_LOGI(TAG, "ESP_HIDD_EVENT_BLE_DISCONNECT: removed from array @%d", i);
                break;
            }
        }
        ESP_LOGI(TAG, "ESP_HIDD_EVENT_BLE_DISCONNECT");
        esp_ble_gap_start_advertising(&hidd_adv_params);
        xEventGroupSetBits(eventgroup_system, SYSTEM_CURRENTLY_ADVERTISING);
        break;
    }
    case ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT:
    {
        ESP_LOGI(TAG, "%s, ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT", __func__);
        ESP_LOG_BUFFER_HEX(TAG, param->vendor_write.data, param->vendor_write.length);
        break;
    }
    case ESP_HIDD_EVENT_BLE_LED_OUT_WRITE_EVT:
    {
        ESP_LOGI(TAG, "%s, ESP_HIDD_EVENT_BLE_LED_OUT_WRITE_EVT, keyboard LED value: %d", __func__, param->vendor_write.data[0]);
        break;
    }

    case ESP_HIDD_EVENT_BLE_CONGEST:
    {
        if (param->congest.congested)
        {
            ESP_LOGI(TAG, "Congest: %d, conn: %d", param->congest.congested, param->congest.conn_id);
            esp_gap_conn_params_t current;
            esp_ble_get_current_conn_params(active_connections[param->congest.conn_id], &current);
            ESP_LOGI(TAG, "Interval: %d, latency: %d, timeout: %d", current.interval, current.latency, current.timeout);
        }
        break;
    }
    default:
        break;
    }
    return;
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&hidd_adv_params);
        xEventGroupSetBits(eventgroup_system, SYSTEM_CURRENTLY_ADVERTISING);
        break;
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (esp_ble_gap_start_scanning(3600) != ESP_OK)
            ESP_LOGE(TAG, "Cannot start scan");
        else
            ESP_LOGI(TAG, "Start scan");
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        for (int i = 0; i < ESP_BD_ADDR_LEN; i++)
        {
            ESP_LOGD(TAG, "%x:", param->ble_security.ble_req.bd_addr[i]);
        }
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        sec_conn = true;
        esp_bd_addr_t bd_addr;
        memcpy(bd_addr, param->ble_security.auth_cmpl.bd_addr, sizeof(esp_bd_addr_t));
        ESP_LOGI(TAG, "remote BD_ADDR: %08x%04x",
                 (bd_addr[0] << 24) + (bd_addr[1] << 16) + (bd_addr[2] << 8) + bd_addr[3],
                 (bd_addr[4] << 8) + bd_addr[5]);
        ESP_LOGI(TAG, "address type = %d", param->ble_security.auth_cmpl.addr_type);
        ESP_LOGI(TAG, "pair status = %s", param->ble_security.auth_cmpl.success ? "success" : "fail");
        if (!param->ble_security.auth_cmpl.success)
        {
            ESP_LOGE(TAG, "fail reason = 0x%x", param->ble_security.auth_cmpl.fail_reason);
        }
        else
        {
            xEventGroupClearBits(eventgroup_system, SYSTEM_CURRENTLY_ADVERTISING);
        }
#if CONFIG_MODULE_BT_PAIRING
        //add connected device to whitelist (necessary if whitelist connections only).
        if (esp_ble_gap_update_whitelist(true, bd_addr, BLE_WL_ADDR_TYPE_PUBLIC) != ESP_OK)
        {
            ESP_LOGW(TAG, "cannot add device to whitelist, with public address");
        }
        else
        {
            ESP_LOGI(TAG, "added device to whitelist");
        }
        if (esp_ble_gap_update_whitelist(true, bd_addr, BLE_WL_ADDR_TYPE_RANDOM) != ESP_OK)
        {
            ESP_LOGW(TAG, "cannot add device to whitelist, with random address");
        }
#endif
        break;
    //handle scan responses here...
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
    {
        uint8_t *adv_name = NULL;
        uint8_t adv_name_len = 0;
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
        switch (scan_result->scan_rst.search_evt)
        {
        case ESP_GAP_SEARCH_INQ_RES_EVT:
            adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);
            if (adv_name_len == 0)
                adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_SHORT, &adv_name_len);
            if (adv_name != NULL)
            {
                //store name to BT addr...
                esp_log_buffer_hex(TAG, scan_result->scan_rst.bda, 6);
                esp_log_buffer_char(TAG, adv_name, adv_name_len);
                adv_name[adv_name_len] = '\0';
                char key[13];
                sprintf(key, "%02X%02X%02X%02X%02X%02X", scan_result->scan_rst.bda[0], scan_result->scan_rst.bda[1],
                        scan_result->scan_rst.bda[2], scan_result->scan_rst.bda[3], scan_result->scan_rst.bda[4], scan_result->scan_rst.bda[5]);
            }
            break;
        case ESP_GAP_SEARCH_INQ_CMPL_EVT:
            break;
        default:
            break;
        }
    }
    break;

    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        //scan start complete event to indicate scan start successfully or failed
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(TAG, "scan start failed, error status = %x", param->scan_start_cmpl.status);
            break;
        }
        ESP_LOGI(TAG, "Scan start success");
        break;

    default:
        break;
    }
}

void ble_hid_keyboard_send_report(key_mask_t special_key, uint8_t *keyboard_cmd, uint8_t num_key) {
    esp_hidd_send_keyboard_value(hid_conn_id, special_key, keyboard_cmd, num_key);
}

esp_err_t ble_init() {

    esp_err_t ret;

    // Initialize FreeRTOS elements
    eventgroup_system = xEventGroupCreate();
    if (eventgroup_system == NULL) ESP_LOGE(TAG, "Cannot initialize event group");

#if CONFIG_MODULE_BT_PAIRING
    ESP_LOGI(TAG,"pairing disabled by default");
    xEventGroupClearBits(eventgroup_system,SYSTEM_PAIRING_ENABLED);
    hidd_adv_params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_WLST;
#else
    ESP_LOGI(TAG,"pairing enabled by default");
    xEventGroupSetBits(eventgroup_system,SYSTEM_PAIRING_ENABLED);
    hidd_adv_params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
#endif

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "%s initialize controller failed\n", __func__);
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "%s enable controller failed\n", __func__);
        return ret;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "%s init bluedroid failed\n", __func__);
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "%s init bluedroid failed\n", __func__);
        return ret;
    }

    if ((ret = esp_hidd_profile_init()) != ESP_OK) {
        ESP_LOGE(TAG, "%s init bluedroid failed\n", __func__);
    }

    strcpy(config.bt_device_name, BT_DEVICE_NAME);

    ///register the callback function to the gap module
    esp_ble_gap_register_callback(gap_event_handler);
    esp_hidd_register_callbacks(hidd_event_callback);

    /* set the security iocap & auth_req & key size & init key response key parameters to the stack*/
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_BOND;     //bonding with peer device after authentication
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;           //set the IO capability to No output No input
    uint8_t key_size = 16;      //the key size should be 7~16 bytes
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    /* If your BLE device act as a Slave, the init_key means you hope which types of key of the master should distribute to you,
    and the response key means which key you can distribute to the Master;
    If your BLE device act as a master, the response key means you hope which types of key of the slave should distribute to you,
    and the init key means which key you can distribute to the slave. */
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));

    //start periodic timer to send HID reports
    const esp_timer_create_args_t periodic_timer_args = {
            .callback = &periodicHIDCallback,
            /* name is optional, but may help identify the timer when debugging */
            .name = "HIDidle"
    };
    esp_timer_handle_t periodic_timer;
    esp_timer_create(&periodic_timer_args, &periodic_timer);
    //call every 100ms
    esp_timer_start_periodic(periodic_timer, 100000);

    return ret;

}