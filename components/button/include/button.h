//
// Created by Egor Kryndach on 2020-06-12.
//

#ifndef BLACK_BRICKS_ESP_BASE_BUTTON_H
#define BLACK_BRICKS_ESP_BASE_BUTTON_H

//MARK: Import common headers
#include "esp_err.h"
#include "driver/gpio.h"

//MARK: Macros and constants

//MARK: Types
typedef enum {
    BUTTON_DOWN = 0,
    BUTTON_UP = 1
} button_event_type_t;

typedef esp_err_t (*on_button_event_cb_t)(gpio_num_t pin, button_event_type_t event);

//MARK: Function prototypes
extern esp_err_t button_component_init(on_button_event_cb_t on_button_event_cb);
extern esp_err_t button_init(gpio_num_t pin);

#endif //BLACK_BRICKS_ESP_BASE_BUTTON_H