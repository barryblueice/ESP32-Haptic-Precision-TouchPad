#include "hid_msg.h"

#include <inttypes.h>

#include "esp_log.h"
#include "NVS/nvs_handle.h"

#define TAG "hid_msg"
#define NVS_KEY_BUTTON_PRESS_THRESHOLD "btn_press_th"
#define NVS_KEY_HAPTIC_CLICK "haptic_click"

int32_t current_mode = WIRED_MODE;
uint8_t current_tp_mode = PTP_MODE;
uint8_t ptp_button_press_threshold = 0x02;
uint8_t ptp_haptic_click_intensity = 0x02;

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

uint8_t ptp_haptic_click_intensity_clamp(uint8_t intensity)
{
    if (intensity > 0x04U) {
        return 0x04U;
    }

    return intensity;
}

uint32_t ptp_haptic_click_duration_ms_from_intensity(uint8_t intensity)
{
    switch (ptp_haptic_click_intensity_clamp(intensity)) {
        case 0x01:
            return 12U;

        case 0x02:
            return 20U;

        case 0x03:
            return 32U;

        case 0x04:
            return 48U;

        default:
            return 0U;
    }
}

void ptp_haptic_click_intensity_set(uint8_t intensity, bool persist)
{
    uint8_t sanitized = ptp_haptic_click_intensity_clamp(intensity);

    ptp_haptic_click_intensity = sanitized;

    if (persist) {
        esp_err_t err = nvs_write_int(NVS_KEY_HAPTIC_CLICK, sanitized);
        if (err != ESP_OK) {
            ESP_LOGW(TAG,
                     "Failed to persist haptic intensity=%u err=0x%x",
                     sanitized,
                     err);
        }
    }
}

void ptp_haptic_click_intensity_load_from_nvs(void)
{
    int32_t value = 0;
    esp_err_t err = nvs_read_int(NVS_KEY_HAPTIC_CLICK, &value);

    if (err == ESP_OK) {
        if (value < 0) {
            value = 0;
        }
        ptp_haptic_click_intensity_set((uint8_t)value, false);
        ESP_LOGI(TAG,
                 "Loaded haptic intensity=%u duration_ms=%" PRIu32,
                 ptp_haptic_click_intensity,
                 ptp_haptic_click_duration_ms_from_intensity(ptp_haptic_click_intensity));
        return;
    }

    ptp_haptic_click_intensity_set(ptp_haptic_click_intensity, false);
    (void)nvs_write_int(NVS_KEY_HAPTIC_CLICK, ptp_haptic_click_intensity);
    ESP_LOGI(TAG,
             "Using default haptic intensity=%u duration_ms=%" PRIu32,
             ptp_haptic_click_intensity,
             ptp_haptic_click_duration_ms_from_intensity(ptp_haptic_click_intensity));
}
