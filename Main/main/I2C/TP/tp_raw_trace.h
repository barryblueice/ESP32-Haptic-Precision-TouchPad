#pragma once
#include <stdint.h>

/* Temporary serial capture. Set to 0 after collecting calibration data. */
#define TP_RAW_TRACE_ENABLED 0

void tp_raw_trace_init(void);
/* Called by the I2C reader task, before input queues/filtering/recognition.
 * Copies the packet with a zero-wait enqueue; never prints in the reader. */
void tp_raw_trace_capture(const uint8_t bytes[64], uint32_t time_ms,
                          uint32_t generation, uint32_t output_generation);
