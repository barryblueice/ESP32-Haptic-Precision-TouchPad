#include "surface_haptic_runtime.h"

void surface_runtime_cancel(surface_haptic_runtime_t *r)
{
    r->head = r->count = 0;
    ++r->generation;
    r->input_id = r->active_id = 0;
    r->blocked = r->down;
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

bool surface_runtime_current(const surface_haptic_runtime_t *r, const surface_haptic_event_t *event)
{
    return r->state == SURFACE_READY && event->generation == r->generation;
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
    return false;
}
