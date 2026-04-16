#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#define TAG "NVS"

nvs_handle_t handle;

esp_err_t nvs_write_int(const char* key, int32_t value) {
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_i32(handle, key, value);
    nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_read_int(const char* key, int32_t *out_value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    err = nvs_get_i32(handle, key, out_value);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_write_str(const char* key, const char* value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, key, value);
    nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_read_str(const char* key, char* out_buf, size_t buf_len) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    err = nvs_get_str(handle, key, out_buf, &buf_len);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_init(void) {

    // nvs_flash_erase();

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated or format unrecognized, erasing...");
        
        ESP_ERROR_CHECK(nvs_flash_erase());
        
        ret = nvs_flash_init();
    }
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NVS initialized successfully.");
    } else {
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(ret));
    }

    return ret;
}