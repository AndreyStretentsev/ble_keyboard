//
// Created by Egor Kryndach on 2020-06-12.
//

//MARK: Import common headers
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

//MARK: Import component header
#include "data_storage.h"

//MARK: Private macros and constants
#define TAG "DATA_STORAGE"

//MARK: Private types

//MARK: Declaration of the private opaque structs

//MARK: Public global variables

//MARK: Private global variables

//MARK: Private functions

//MARK: Public functions
esp_err_t data_storage_init() {
    esp_err_t ret = nvs_flash_init();

    if (ret != ESP_ERR_NVS_NO_FREE_PAGES && ret != ESP_ERR_NVS_NEW_VERSION_FOUND) {
        return ret;
    }

    ret = nvs_flash_erase();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_flash_init();
    return ret;
}

esp_err_t data_storage_write(const char *key, const uint8_t *data, size_t length) {
    nvs_handle_t my_handle;
    esp_err_t err;

    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t required_size = 0;
    err = nvs_get_blob(my_handle, key, NULL, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    err = nvs_set_blob(my_handle, key, data, length);

    if (err != ESP_OK) {
        return err;
    }

    // Commit
    err = nvs_commit(my_handle);
    if (err != ESP_OK) {
        return err;
    }

    // Close
    nvs_close(my_handle);
    return ESP_OK;
}

esp_err_t data_storage_read_alloc(const char *key, uint8_t **out_data, size_t *length) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t required_size = 0;
    err = nvs_get_blob(nvs_handle, key, NULL, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }
    if (required_size == 0) {
        ESP_LOGI(__FILE__, "Nothing saved yet!");
        *length = 0;
    } else {
        uint8_t *data = malloc(required_size);
        err = nvs_get_blob(nvs_handle, key, data, &required_size);
        if (err != ESP_OK) {
            free(data);
            nvs_close(nvs_handle);
            return err;
        }

        *length = required_size;
        *out_data = data;
    }

    // Close
    nvs_close(nvs_handle);
    return ESP_OK;
}

extern esp_err_t data_storage_write_i32(const char *key, int32_t value) {
    esp_err_t err;
    nvs_handle_t nvs_handle;

    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(nvs_handle, key, value);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);

    return ESP_OK;
}

extern esp_err_t data_storage_read_i32(const char *key, int32_t *value) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_i32(nvs_handle, key, value);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    // Close
    nvs_close(nvs_handle);
    return ESP_OK;
}