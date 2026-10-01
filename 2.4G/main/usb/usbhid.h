#pragma once
#include <stdint.h>
void usbhid_init(void);
void usbhid_task(void *arg);
/* Ask the USB task to signal remote wakeup once per host suspend. Safe to call
 * from any task, including while holding the input pipeline lock. */
void usbhid_remote_wakeup_request(void);
extern uint8_t haptic_ptp_hid_report_descriptor[];
extern const uint8_t legacy_ptp_hid_report_descriptor[];
extern const uint8_t mouse_hid_report_descriptor[];
extern const uint8_t generic_hid_report_descriptor[];
extern const uint8_t desc_configuration[];

void receiver_descriptor_rotation(uint8_t rotation);
