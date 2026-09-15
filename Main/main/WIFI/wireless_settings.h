#pragma once
#include "SYS/wireless_extension.h"
#include "esp_err.h"

esp_err_t wireless_settings_init(uint32_t session);
void wireless_settings_peer(const uint8_t mac[6]);
void wireless_settings_receive(const uint8_t *mac, const uint8_t *packet, unsigned size);
bool wireless_settings_reply(uint8_t packet[38], uint8_t mac[6]);
void wireless_settings_reply_complete(bool success);
