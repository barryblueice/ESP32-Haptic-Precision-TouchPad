#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Caller holds usb_tx_lock. Requests arriving during a queued/running pump
 * survive notifications consumed by the sender before that pump ends. */
typedef struct { uint32_t requested, scheduled; bool queued; } usb_pump_state_t;
static inline void usb_pump_request(usb_pump_state_t *s) { ++s->requested; }
static inline bool usb_pump_schedule(usb_pump_state_t *s)
{
    if (s->queued) return false;
    s->queued = true; s->scheduled = s->requested;
    return true;
}
static inline bool usb_pump_finish(usb_pump_state_t *s)
{
    s->queued = false;
    return s->scheduled != s->requested;
}
