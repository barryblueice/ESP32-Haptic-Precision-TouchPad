#pragma once
#include "protocol.h"

#define REPORT_BUFFER_CAPACITY 32U
#define REPORT_MAX_AGE_MS 100U
#define REPORT_RELEASE_MASK ((1U << REPORT_HAPTIC) | (1U << REPORT_LEGACY) | (1U << REPORT_MOUSE))

typedef struct {
    report_kind_t kind;
    uint32_t generation, time_ms;
    bool release;
    report_payload_t data;
} input_report_t;
typedef struct {
    input_report_t report;
    int32_t x, y, wheel, pan;
    bool edge;
} report_entry_t;
typedef struct {
    uint32_t invalid_packets, rx_overflows, merged, recoveries, usb_failures;
    uint32_t peak, longest_wait_ms;
} input_stats_t;
typedef struct {
    report_entry_t entries[REPORT_BUFFER_CAPACITY];
    unsigned head, count;
    uint32_t generation;
    report_kind_t active;
    uint8_t release_mask;
    bool recovering, all_up;
    input_report_t last, submitted[4];
    input_stats_t stats;
} report_buffer_t;

void report_buffer_reset(report_buffer_t *b);
bool report_all_up(const input_report_t *r);
bool report_buffer_push(report_buffer_t *b, const input_report_t *r);
bool report_buffer_take(report_buffer_t *b, uint32_t now, input_report_t *out);
bool report_buffer_current(report_buffer_t *b, const input_report_t *r, uint32_t now);
void report_buffer_ack(report_buffer_t *b, const input_report_t *r);
void report_buffer_submitted(report_buffer_t *b, const input_report_t *r);
