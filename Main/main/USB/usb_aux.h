#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { uint8_t id, length, data[5]; uint32_t generation; bool release; } usb_aux_report_t;
bool usb_aux_steps(uint8_t action, int steps, uint32_t generation, uint32_t time_ms);
bool usb_aux_take(usb_aux_report_t *out, uint32_t generation, uint32_t time_ms);
void usb_aux_complete(bool success);
void usb_aux_unsubmitted(void);
void usb_aux_reset(bool connected);
bool usb_aux_active(void);
bool usb_aux_release_pending(void);
