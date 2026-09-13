#ifndef SURFACE_HAPTIC_RUNTIME_H
#define SURFACE_HAPTIC_RUNTIME_H
#include "surface_haptic_policy.h"

#define SURFACE_EVENT_CAPACITY 8U
#define SURFACE_EVENT_MAX_AGE_MS 100U

typedef enum {
    SURFACE_INITIALIZING, SURFACE_READY, SURFACE_SLEEPING,
    SURFACE_WAKING, SURFACE_FAULT
} surface_haptic_state_t;

typedef struct {
    uint32_t click_id, generation, time_ms;
    surface_haptic_pair_t pair;
    uint8_t setting;
    bool release;
} surface_haptic_event_t;

/* Caller serializes access. No RTOS or hardware dependencies. */
typedef struct {
    surface_haptic_state_t state;
    surface_haptic_event_t events[SURFACE_EVENT_CAPACITY];
    unsigned int head, count;
    uint32_t generation, next_id, input_id, active_id, dropped;
    bool down, blocked;
    surface_haptic_pair_t pair;
    uint8_t setting;
} surface_haptic_runtime_t;

void surface_runtime_cancel(surface_haptic_runtime_t *r);
void surface_runtime_state(surface_haptic_runtime_t *r, surface_haptic_state_t state);
void surface_runtime_button(surface_haptic_runtime_t *r, bool down, uint8_t setting, uint32_t now);
bool surface_runtime_pop(surface_haptic_runtime_t *r, uint32_t now, surface_haptic_event_t *event);
bool surface_runtime_current(const surface_haptic_runtime_t *r, const surface_haptic_event_t *event);

#endif
