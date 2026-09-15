#pragma once
#include "SYS/wireless_extension.h"
void receiver_ext_receive(const uint8_t *mac, const uint8_t *packet, uint32_t now);
bool receiver_ext_accept(const uint8_t *mac);
bool receiver_ext_apply(wire_surface_t *surface);
void receiver_ext_applied(const wire_surface_t *surface);
void receiver_ext_usb_ready(bool ready);
bool receiver_ext_ack(uint8_t packet[38], uint8_t mac[6]);
void receiver_ext_ack_complete(bool success);
