#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "main_config.h"
#include <freertos/task.h>
#include "ble.h"
#include "data_storage.h"
#include "button.h"

#define TAG "MAIN"

esp_err_t keyboard_callback(gpio_num_t pin, button_event_type_t event) {
    return ESP_OK;
}

void keyboard_init() {

    button_init(CONFIG_BUTTON_BACK);
    button_init(CONFIG_BUTTON_FORWARD);
    button_init(CONFIG_BUTTON_PLAY);
    button_init(CONFIG_BUTTON_SPEED);
    button_init(CONFIG_BUTTON_LOOP);

    button_component_init(keyboard_callback);
}

void app_main(void)
{
    ESP_LOGI(TAG, "%s", __func__);

    keyboard_init();

    ble_init();
    
}
