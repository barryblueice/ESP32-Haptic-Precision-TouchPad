#pragma once
#include "protocol.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ESPNOW_CHANNEL 1
#define RECEIVE_CAPACITY 16
extern const uint8_t broadcast_mac[6];
void wireless_init(void);
void broadcast_init(void);
void wireless_request_mode(void);
void wireless_control_step(uint32_t now);
void wireless_register_worker(void);
void wireless_wake_worker(void);
void monitor_link_task(void *arg);
void wireless_heartbeat_seen(TickType_t tick);
void wireless_led_init(void);
