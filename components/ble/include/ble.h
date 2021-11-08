#ifndef BLE_H
#define BLE_H

//MARK: Import common headers
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_gap_ble_api.h"
#include "esp_hidd_prf_api.h"

//MARK: Macros and constants
#define BT_DEVICE_NAME "BLE_KEYBOARD"
#define MAX_BT_DEVICENAME_LENGTH 40

//MARK: Types
typedef struct config_data {
    char bt_device_name[MAX_BT_DEVICENAME_LENGTH];
    uint8_t locale;
} config_data_t;

//MARK: Types (Core)

//MARK: Global variables

//MARK: Function prototypes
esp_err_t ble_init();

//MARK: Function prototypes (Config)

//MARK: Function prototypes (Scan)

//MARK: Function prototypes (Client)
void ble_hid_keyboard_send_report(key_mask_t special_key, uint8_t *keyboard_cmd, uint8_t num_key);

//MARK: Function prototypes (Server)


#endif //BLE_H
