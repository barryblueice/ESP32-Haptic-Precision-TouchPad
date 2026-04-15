#ifndef NVS_HANDLE_H
#define NVS_HANDLE_H

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

esp_err_t nvs_write_int(const char* key, int32_t value);
esp_err_t nvs_read_int(const char* key, int32_t *out_value);
esp_err_t nvs_write_str(const char* key, const char* value);
esp_err_t nvs_read_str(const char* key, char* out_buf, size_t buf_len);
esp_err_t nvs_init(void);

#endif // NVS_HANDLE_H