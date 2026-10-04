#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "sdkconfig.h"
typedef enum {
    INPUT_DIAG_READ, INPUT_DIAG_RAW_WAIT, INPUT_DIAG_PARSE, INPUT_DIAG_FORCE,
    INPUT_DIAG_SUBMIT, INPUT_DIAG_COMPLETE, INPUT_DIAG_USB_FLIGHT,
    INPUT_DIAG_CAPTURE_INTERVAL, INPUT_DIAG_COUNT
} input_diag_stage_t;
#if CONFIG_INPUT_LATENCY_DIAGNOSTICS
uint32_t input_diag_now(void);
void input_diag_sample(input_diag_stage_t stage, uint32_t us);
void input_diag_depth(unsigned raw, unsigned reports);
void input_diag_recovery(const char *reason);
void input_diag_init(void);
void input_diag_read_result(const uint8_t *bytes, bool success);
/* Single producer: valid raw reports, before parser/gesture buffering. */
void input_diag_capture(uint32_t generation, bool active, uint32_t read_done_us);
#else
static inline uint32_t input_diag_now(void) { return 0; }
static inline void input_diag_sample(input_diag_stage_t s, uint32_t us) { (void)s; (void)us; }
static inline void input_diag_depth(unsigned r, unsigned o) { (void)r; (void)o; }
static inline void input_diag_recovery(const char *r) { (void)r; }
static inline void input_diag_init(void) { }
static inline void input_diag_read_result(const uint8_t *b, bool s) { (void)b; (void)s; }
static inline void input_diag_capture(uint32_t g, bool a, uint32_t t) { (void)g; (void)a; (void)t; }
#endif
