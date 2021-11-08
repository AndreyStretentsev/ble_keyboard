//
// Created by Egor Kryndach on 2020-06-12.
//

//MARK: Import common headers
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <esp_sleep.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"

//MARK: Import component header
#include "button.h"

//MARK: Private macros and constants
#define TAG "BUTTON"

#define BUTTON_PIN_BIT(x) (1ULL<<x)
#define ESP_INTR_FLAG_DEFAULT 0

//MARK: Private types
typedef struct {
    xQueueHandle queue;
    xTaskHandle task;
    on_button_event_cb_t on_button_event_cb;
    int levels[GPIO_NUM_MAX];
} component_t;

//MARK: Declaration of the private opaque structs

//MARK: Public global variables

//MARK: Private global variables
static component_t component = {
        .queue = NULL,
};

//MARK: Private functions
static void IRAM_ATTR gpio_isr_handler(void *arg) {
    uint32_t gpio_num = (uint32_t) arg;

    gpio_set_intr_type(gpio_num, GPIO_INTR_DISABLE);
    gpio_wakeup_disable(gpio_num);

    xQueueSendFromISR(component.queue, &gpio_num, NULL);
}

static void gpio_task(void *arg) {
    uint32_t io_num;
    bool inf_loop = true;
    while (inf_loop) {
        if (xQueueReceive(component.queue, &io_num, portMAX_DELAY)) {
            int new_level = gpio_get_level(io_num);
            component.levels[io_num] = new_level;

            button_event_type_t event = new_level ? BUTTON_UP : BUTTON_DOWN;

            if (event == BUTTON_UP) {
                ESP_LOGI(TAG, "%d UP", io_num);
                gpio_set_intr_type(io_num, GPIO_INTR_LOW_LEVEL);
                gpio_wakeup_enable(io_num, GPIO_INTR_LOW_LEVEL);
            } else {
                ESP_LOGI(TAG, "%d DOWN", io_num);
                gpio_set_intr_type(io_num, GPIO_INTR_HIGH_LEVEL);
                gpio_wakeup_enable(io_num, GPIO_INTR_HIGH_LEVEL);
            }

            if (component.on_button_event_cb != NULL) {
                component.on_button_event_cb(io_num, event);
            }
        }
    }
}

//MARK: Implementation of the public functions
extern esp_err_t button_component_init(on_button_event_cb_t on_button_event_cb) {
    component.on_button_event_cb = on_button_event_cb;
    component.queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(&gpio_task, "gpio_task", 4096, NULL, 10, &component.task);
    esp_sleep_enable_gpio_wakeup();
    return ESP_OK;
}

extern esp_err_t button_init(gpio_num_t pin) {

    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = BUTTON_PIN_BIT(pin);
    io_conf.pull_up_en = 1;
    io_conf.pull_down_en = 0;
    gpio_config(&io_conf);

    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(pin, gpio_isr_handler, (void *) pin);

    int level = gpio_get_level(pin);
    component.levels[pin] = level;
    if (level) {
        gpio_set_intr_type(pin, GPIO_INTR_LOW_LEVEL);
        gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL);
    } else {
        gpio_set_intr_type(pin, GPIO_INTR_HIGH_LEVEL);
        gpio_wakeup_enable(pin, GPIO_INTR_HIGH_LEVEL);
    }

    return ESP_OK;
}