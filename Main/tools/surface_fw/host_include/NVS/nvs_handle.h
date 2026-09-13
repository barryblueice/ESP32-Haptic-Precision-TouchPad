#pragma once
#include <stdint.h>
#include "esp_err.h"
esp_err_t nvs_read_int(const char *key, int32_t *value);
esp_err_t nvs_write_int(const char *key, int32_t value);
