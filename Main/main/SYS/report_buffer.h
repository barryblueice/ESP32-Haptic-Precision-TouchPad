#pragma once

#include "hid_msg.h"

#define REPORT_BUFFER_CAPACITY 32U
#define REPORT_MAX_AGE_MS 100U

typedef struct {
    uint8_t mode;
    uint32_t generation, time_ms;
    uint32_t queued_time_ms; /* Oldest origin retained after dequeue into a sender. */
    uint32_t read_done_us; /* Diagnostic origin; zero for synthetic/replayed input. */
    bool release;
    union { ptp_report_t ptp; mouse_hid_report_t mouse; } data;
} input_report_t;

typedef struct {
    input_report_t report;
    uint32_t first_time_ms; /* Oldest queued sample, independent of latest PTP data. */
    int32_t x, y, wheel, pan;
    bool edge;
} report_entry_t;

typedef struct {
    uint32_t merged, recoveries, submit_failures, raw_overflows, read_failures;
    uint32_t peak, longest_wait_ms;
} input_stats_t;

typedef struct {
    report_entry_t entries[REPORT_BUFFER_CAPACITY];
    unsigned count;
    uint32_t generation;
    uint8_t mode, release_mask;
    bool recovering, all_up, recovery_ready;
    input_report_t last;
    input_stats_t stats;
} report_buffer_t;

void report_buffer_reset(report_buffer_t *b, uint8_t mode);
bool report_buffer_observe(report_buffer_t *b, bool all_up);
bool report_buffer_push(report_buffer_t *b, const input_report_t *report, bool tap);
bool report_buffer_take(report_buffer_t *b, uint32_t now, input_report_t *out);
void report_buffer_ack(report_buffer_t *b, const input_report_t *report);
bool report_buffer_current(const report_buffer_t *b, const input_report_t *report);
