#pragma once
#include <stdint.h>

/* Temporary diagnostics: 0=off, 1=all frames for calibration,
 * 2=contact transitions, 3=every frame and every slot regardless of status.
 * Modes 2/3 include gesture decisions. Set to 0 after diagnosis. */
#define TP_RAW_TRACE_ENABLED 0

void tp_raw_trace_init(void);
/* Called by the I2C reader task, before input queues/filtering/recognition.
 * Copies the packet with a zero-wait enqueue; never prints in the reader. */
void tp_raw_trace_capture(const uint8_t bytes[64], uint32_t time_ms,
                          uint32_t generation, uint32_t output_generation);
