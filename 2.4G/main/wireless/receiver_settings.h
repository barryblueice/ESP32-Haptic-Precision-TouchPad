#pragma once
#include "SYS/wireless_extension.h"
uint8_t receiver_settings_get(uint8_t field);
/* Returns false when the value is out of range and was not recorded. An
 * accepted value is kept as pending state and flushed to the touchpad. */
bool receiver_settings_set(uint8_t field, uint8_t value);
bool receiver_settings_next(uint8_t packet[38], uint8_t mac[6], uint32_t now);
void receiver_settings_receive(const uint8_t mac[6], const uint8_t packet[38], uint32_t now);
