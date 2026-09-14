#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t usb_config_init(void);
void usb_config_receive(const uint8_t *data, uint16_t size);
void usb_config_legacy(uint8_t id, uint8_t value);
void usb_config_dfu(void);
void usb_config_send(void);
void usb_config_complete(bool success);
void usb_config_detach(void);
bool usb_config_active(void);
