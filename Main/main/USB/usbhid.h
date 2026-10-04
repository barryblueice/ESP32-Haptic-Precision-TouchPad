#ifndef USBHID_H
#define USBHID_H
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

bool usbhid_notify_sender(TaskHandle_t task);
void usbhid_task(void *arg);
void usbhid_init(void);
void usbhid_remote_wakeup_request(void);

extern uint8_t ptp_hid_report_descriptor[];
void usb_descriptor_init(void);
extern const uint8_t mouse_hid_report_descriptor[];
extern const uint8_t generic_hid_report_descriptor[];
extern const uint8_t desc_configuration[];

#endif
