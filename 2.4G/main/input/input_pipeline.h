#pragma once
#include "report_buffer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void input_init(void);
void input_register_sender(void);
void input_wake_sender(void);
uint32_t input_now_ms(void);
uint32_t input_generation(void);
void input_recover(void);
void input_invalid_packet(void);
void input_rx_overflow(void);
void input_set_usb(bool ready);
void input_set_mode(current_input_mode_t mode);
current_input_mode_t input_mode(void);
void input_link_seen(uint32_t time_ms);
void input_check_link(uint32_t now);
void input_receive(const wireless_msg_t *packet, uint32_t generation, uint32_t time_ms);
bool input_take(input_report_t *report);
bool input_current(const input_report_t *report);
void input_submitted(const input_report_t *report);
void input_complete(const input_report_t *report, bool success);
void input_submit_failed(void);
void input_get_stats(input_stats_t *out);
void input_log_stats(void);
