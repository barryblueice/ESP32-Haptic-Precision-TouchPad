#include "surface_haptic_runtime.h"

void surface_runtime_cancel(surface_haptic_runtime_t *r)
{
    r->head = r->count = 0;
    ++r->generation;
    r->input_id = r->active_id = 0;
    r->blocked = r->down;
    r->gesture_pending = r->gesture_throttled = false;
}

void surface_runtime_state(surface_haptic_runtime_t *r, surface_haptic_state_t state)
{
    if (r->state == SURFACE_FAULT || r->state == state) {
        return;
    }
    surface_runtime_cancel(r);
    r->state = state;
}

void surface_runtime_button(surface_haptic_runtime_t *r, bool down, uint8_t setting, uint32_t now)
{
    if (r->down == down) {
        return;
    }
    r->down = down;
    if (down) r->gesture_pending = false;
    if (r->blocked || r->state != SURFACE_READY) {
        r->blocked = down;
        r->input_id = 0;
        return;
    }
    if (down) {
        if (!surface_haptic_resolve(setting, &r->pair) || !r->pair.enabled) {
            r->input_id = 0;
            return;
        }
        if (++r->next_id == 0) {
            ++r->next_id;
        }
        r->input_id = r->next_id;
        r->setting = setting;
    } else if (r->input_id == 0) {
        return;
    }
    if (r->count == SURFACE_EVENT_CAPACITY) {
        ++r->dropped;
        surface_runtime_cancel(r);
        return;
    }
    surface_haptic_event_t event = {
        .click_id = r->input_id, .generation = r->generation, .time_ms = now,
        .pair = r->pair, .setting = r->setting, .release = !down,
    };
    r->events[(r->head + r->count) % SURFACE_EVENT_CAPACITY] = event;
    ++r->count;
    if (!down) {
        r->input_id = 0;
    }
}

void surface_runtime_gesture(surface_haptic_runtime_t *r, bool point, uint32_t now)
{
    /* A single best-effort slot cannot fill or cancel the click queue. */
    if (r->state != SURFACE_READY || r->blocked || r->down || r->active_id || r->count ||
        r->gesture_pending || (r->gesture_throttled &&
        (uint32_t)(now - r->gesture_at) < SURFACE_GESTURE_INTERVAL_MS)) return;
    uint8_t wave = point ? SURFACE_GESTURE_POINT_WAVE : SURFACE_GESTURE_EDGE_WAVE;
    r->gesture_event = (surface_haptic_event_t){
        .generation = r->generation, .time_ms = now, .gesture = true,
        .pair = {.enabled = true, .press_index = wave, .release_index = wave},
    };
    r->gesture_pending = r->gesture_throttled = true;
    r->gesture_at = now;
}

bool surface_runtime_current(const surface_haptic_runtime_t *r, const surface_haptic_event_t *event)
{
    return r->state == SURFACE_READY && event->generation == r->generation &&
        (!event->gesture || (!r->blocked && !r->down && !r->active_id && !r->count));
}

bool surface_runtime_pop(surface_haptic_runtime_t *r, uint32_t now, surface_haptic_event_t *event)
{
    while (r->state == SURFACE_READY && r->count != 0) {
        *event = r->events[r->head];
        r->head = (r->head + 1) % SURFACE_EVENT_CAPACITY;
        --r->count;
        if ((uint32_t)(now - event->time_ms) > SURFACE_EVENT_MAX_AGE_MS) {
            ++r->dropped;
            surface_runtime_cancel(r);
            return false;
        }
        if (!surface_runtime_current(r, event)) {
            continue;
        }
        if (event->release) {
            if (r->active_id != event->click_id) {
                continue;
            }
            r->active_id = 0;
        } else {
            r->active_id = event->click_id;
        }
        return true;
    }
    if (r->gesture_pending) {
        *event = r->gesture_event;
        r->gesture_pending = false;
        return surface_runtime_current(r, event) &&
            (uint32_t)(now - event->time_ms) <= SURFACE_EVENT_MAX_AGE_MS;
    }
    return false;
}
