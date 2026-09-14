#include "surface_haptic_settings.h"
#include "SYS/device_config.h"
#include "cs40l25_surface.h"
#include "NVS/nvs_handle.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "HAPTIC_SETTINGS"
#define KEY "haptic_surface"
#define LEGACY_KEY "haptic_click"
static uint8_t setting = 63;
static portMUX_TYPE value_lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t write_lock;

uint8_t ptp_haptic_click_intensity_get(void)
{
    if (device_config_ready()) return device_config_value(CFG_INTENSITY);
    taskENTER_CRITICAL(&value_lock);
    uint8_t value = setting;
    taskEXIT_CRITICAL(&value_lock);
    return value;
}

esp_err_t ptp_haptic_click_intensity_set(uint8_t value, bool persist)
{
    if (device_config_ready()) return device_config_set_legacy(CFG_INTENSITY, value, persist);
    if (value > 100U) return ESP_ERR_INVALID_ARG;
    // Created during load before connection/input tasks start.
    if (write_lock == NULL || xSemaphoreTake(write_lock, portMAX_DELAY) != pdTRUE) return ESP_ERR_INVALID_STATE;
    esp_err_t err = persist ? nvs_write_int(KEY, value) : ESP_OK;
    if (err == ESP_OK) {
        taskENTER_CRITICAL(&value_lock);
        setting = value;
        taskEXIT_CRITICAL(&value_lock);
        if (value == 0) cs40l25_surface_cancel_click();
        ESP_LOGI(TAG, "Surface strength=%u", value);
    } else {
        ESP_LOGE(TAG, "Could not save strength: %s; active setting unchanged", esp_err_to_name(err));
    }
    xSemaphoreGive(write_lock);
    return err;
}

esp_err_t ptp_haptic_click_intensity_set_report(const uint8_t *data, size_t length, bool persist)
{
    if (data == NULL || length != 1) return ESP_ERR_INVALID_ARG;
    return ptp_haptic_click_intensity_set(data[0], persist);
}

void ptp_haptic_click_intensity_load_from_nvs(void)
{
    if (device_config_ready()) return;
    if (write_lock == NULL) write_lock = xSemaphoreCreateMutex();
    if (write_lock == NULL) {
        ESP_LOGE(TAG, "Settings mutex allocation failed; default strength=63");
        return;
    }
    int32_t value = 63;
    esp_err_t err = nvs_read_int(KEY, &value);
    bool save = false;
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        static const uint8_t migrated[] = {0, 25, 63, 75, 100};
        int32_t legacy;
        value = nvs_read_int(LEGACY_KEY, &legacy) == ESP_OK && legacy >= 0 && legacy <= 4
                    ? migrated[legacy] : 63;
        save = true;
    } else if (err != ESP_OK || value < 0 || value > 100) {
        value = 63;
        save = err == ESP_OK || err == ESP_ERR_NVS_TYPE_MISMATCH;
        // Repair invalid data/type, not an arbitrary storage read error.
    }
    (void)ptp_haptic_click_intensity_set((uint8_t)value, false);
    if (save) (void)ptp_haptic_click_intensity_set((uint8_t)value, true);
}
