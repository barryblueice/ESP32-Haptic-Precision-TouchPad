#pragma once
#include "report_buffer.h"
#include "freertos/task.h"

typedef struct { uint8_t bytes[64]; uint32_t generation, time_ms; } input_frame_t;

void input_pipeline_init(void);
void input_register_parser(void);
void input_register_sender(void);
void input_wake_sender(void);
void input_set_link(uint8_t ready_mask);
void input_recover(void);
void input_capture(const uint8_t *bytes, bool success, uint32_t generation, uint32_t time_ms);
bool input_next_frame(input_frame_t *frame);
uint32_t input_generation(void);
uint8_t input_mode(void);
bool input_observe(uint32_t generation, bool all_up);
bool input_publish(uint32_t generation, input_report_t *report, bool tap);
bool input_take_report(input_report_t *report);
bool input_report_current(const input_report_t *report);
void input_report_ack(const input_report_t *report);
void input_submit_failed(void);
void input_get_stats(input_stats_t *stats);
void input_log_stats(void);
void input_request_mode(uint8_t mode);
bool input_apply_mode_request(void);
