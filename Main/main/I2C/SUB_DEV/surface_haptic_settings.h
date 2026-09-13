#ifndef SURFACE_HAPTIC_SETTINGS_H
#define SURFACE_HAPTIC_SETTINGS_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

uint8_t ptp_haptic_click_intensity_get(void);
esp_err_t ptp_haptic_click_intensity_set(uint8_t setting, bool persist);
esp_err_t ptp_haptic_click_intensity_set_report(const uint8_t *data, size_t length, bool persist);
void ptp_haptic_click_intensity_load_from_nvs(void);
#endif
