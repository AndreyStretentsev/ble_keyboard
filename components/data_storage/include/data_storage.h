//
// Created by Egor Kryndach on 2020-06-18.
//

#ifndef BLACK_BRICKS_ESP_BASE_DATA_STORAGE_H
#define BLACK_BRICKS_ESP_BASE_DATA_STORAGE_H

//MARK: Import common headers
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

//MARK: Macros and constants

//MARK: Types

//MARK: Global variables

//MARK: Function prototypes
extern esp_err_t data_storage_init();
extern esp_err_t data_storage_write(const char *key, const uint8_t *data, size_t length);
extern esp_err_t data_storage_read_alloc(const char *key, uint8_t **out_data, size_t *length);
extern esp_err_t data_storage_write_i32(const char *key, int32_t value);
extern esp_err_t data_storage_read_i32(const char *key, int32_t *value);

#endif //BLACK_BRICKS_ESP_BASE_DATA_STORAGE_H
