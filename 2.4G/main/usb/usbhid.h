#pragma once
#include <stdint.h>
void usbhid_init(void);
void usbhid_task(void *arg);
extern const uint8_t haptic_ptp_hid_report_descriptor[];
extern const uint8_t legacy_ptp_hid_report_descriptor[];
extern const uint8_t mouse_hid_report_descriptor[];
extern const uint8_t generic_hid_report_descriptor[];
extern const uint8_t desc_configuration[];
