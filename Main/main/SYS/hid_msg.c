#include "hid_msg.h"
#include "device_config.h"

#include <inttypes.h>

#include "esp_log.h"
#include "NVS/nvs_handle.h"

#define TAG "hid_msg"
#define NVS_KEY_BUTTON_PRESS_THRESHOLD "btn_press_th"

int32_t current_mode = WIRED_MODE;
uint8_t current_tp_mode = PTP_MODE;
uint8_t ptp_button_press_threshold = 0x02;

uint8_t ptp_button_press_threshold_clamp(uint8_t threshold)
{
    if (threshold < 0x01U) {
        return 0x01U;
    }

    if (threshold > 0x03U) {
        return 0x03U;
    }

    return threshold;
}

void ptp_button_press_threshold_set(uint8_t threshold, bool persist)
{
    uint8_t sanitized = ptp_button_press_threshold_clamp(threshold);
    if (device_config_ready()) {
        esp_err_t err = device_config_set_legacy(CFG_LEVEL, sanitized, persist);
        if (err != ESP_OK) ESP_LOGW(TAG, "Threshold update rejected: %s", esp_err_to_name(err));
        return;
    }

    ptp_button_press_threshold = sanitized;

    if (persist) {
        esp_err_t err = nvs_write_int(NVS_KEY_BUTTON_PRESS_THRESHOLD, sanitized);
        if (err != ESP_OK) {
            ESP_LOGW(TAG,
                     "Failed to persist button press threshold=%u err=0x%x",
                     sanitized,
                     err);
        }
    }
}

void ptp_button_press_threshold_load_from_nvs(void)
{
    if (device_config_ready()) return;
    int32_t value = 0;
    esp_err_t err = nvs_read_int(NVS_KEY_BUTTON_PRESS_THRESHOLD, &value);

    if (err == ESP_OK) {
        bool needs_rewrite = (value < 0x01) || (value > 0x03);

        if (value < 0) {
            value = 0;
        } else if (value > 0xFF) {
            value = 0xFF;
        }

        ptp_button_press_threshold_set((uint8_t)value, false);

        if (needs_rewrite) {
            (void)nvs_write_int(NVS_KEY_BUTTON_PRESS_THRESHOLD, ptp_button_press_threshold);
        }

        ESP_LOGI(TAG, "Loaded button press threshold=%u", ptp_button_press_threshold);
        return;
    }

    ptp_button_press_threshold_set(ptp_button_press_threshold, false);
    (void)nvs_write_int(NVS_KEY_BUTTON_PRESS_THRESHOLD, ptp_button_press_threshold);
    ESP_LOGI(TAG, "Using default button press threshold=%u", ptp_button_press_threshold);
}
