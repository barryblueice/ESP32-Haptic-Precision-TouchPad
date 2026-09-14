/* Storage faults and legacy/new keys, without an ESP-IDF installation. */
#include "NVS/nvs_handle.h"
#include "SYS/device_config.h"
/* This suite exercises standalone settings; unified transactions have their own suite. */
bool device_config_ready(void) { return false; }
uint8_t device_config_value(unsigned offset) { (void)offset; return 0; }
esp_err_t device_config_set_legacy(unsigned offset, uint8_t value, bool persist)
{ (void)offset; (void)value; (void)persist; return ESP_FAIL; }
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
static int values[2], present[2], writes, cancels, fail_write;
static int key_id(const char *key) { return key[7] == 's' ? 0 : 1; }
EXPORT void settings_test_reset(int has_new, int new_value, int has_old, int old_value, int failure)
{
    present[0] = has_new; present[1] = has_old;
    values[0] = new_value; values[1] = old_value;
    writes = cancels = 0; fail_write = failure;
}
EXPORT int settings_test_field(int field)
{
    switch (field) {
    case 0: return writes;
    case 1: return values[0];
    case 2: return values[1];
    case 3: return cancels;
    default: return -1;
    }
}
esp_err_t nvs_read_int(const char *key, int32_t *value)
{
    int index = key_id(key);
    if (!present[index]) return ESP_ERR_NVS_NOT_FOUND;
    if (present[index] == 2) return ESP_ERR_NVS_TYPE_MISMATCH;
    *value = values[index];
    return ESP_OK;
}
esp_err_t nvs_write_int(const char *key, int32_t value)
{
    if (fail_write) return ESP_FAIL;
    int index = key_id(key);
    ++writes; present[index] = 1; values[index] = value;
    return ESP_OK;
}
void cs40l25_surface_cancel_click(void) { ++cancels; }
