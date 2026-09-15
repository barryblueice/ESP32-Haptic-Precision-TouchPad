#include "aux_output.h"
#ifdef AUX_OUTPUT_RECEIVER
#include "input/input_pipeline.h"
#else
#include "SYS/input_pipeline.h"
#endif
#include "freertos/FreeRTOS.h"
#include <string.h>

typedef struct { uint8_t action; int steps; uint32_t generation, time_ms; bool discrete, hold; } aux_entry_t;
static aux_entry_t entries[32];
static unsigned count;
static bool flight;
/* Bit 0: Consumer (7), bit 1: keyboard (8). Kept until completion. */
static uint8_t release_due, neutral_due, flight_release;
static uint8_t held_mask;
static uint32_t aux_generation;
static uint32_t flight_generation, aux_epoch, flight_epoch;
static uint8_t flight_action;
static int flight_steps;
static uint32_t radio_epoch;
static portMUX_TYPE aux_lock = portMUX_INITIALIZER_UNLOCKED;

/* Called with aux_lock held. A completed hold has no queued work until release. */
static void release_held(void)
{
    release_due |= held_mask;
    held_mask = 0;
}

static void set_generation(uint32_t generation)
{
    if (aux_generation != generation) {
        release_held(); count = 0; ++aux_epoch; aux_generation = generation;
    }
}

static bool enqueue(uint8_t action, int steps, uint32_t generation, uint32_t time_ms, bool discrete, bool repeat, bool hold)
{
    if (!steps) return true;
    if (action < 1 || action > 6) return false;
    taskENTER_CRITICAL(&aux_lock);
    set_generation(generation);
    if (repeat && (count || flight || release_due)) {
        taskEXIT_CRITICAL(&aux_lock);
        return true;
    }
    bool ok = count < 32;
    if (ok) entries[count++] = (aux_entry_t){action, steps, generation, time_ms, discrete, hold};
    else { release_held(); count = 0; ++aux_epoch; }
    taskEXIT_CRITICAL(&aux_lock);
    input_wake_sender();
    return ok;
}
bool aux_output_steps(uint8_t action, int steps, uint32_t generation, uint32_t time_ms)
{ return enqueue(action, steps, generation, time_ms, false, false, false); }
bool aux_output_once(uint8_t action, int steps, uint32_t generation, uint32_t time_ms)
{ return enqueue(action, steps, generation, time_ms, true, false, false); }
bool aux_output_hold(uint8_t action, int steps, uint32_t generation, uint32_t time_ms)
{
    if ((action != 1 && action != 2 && action != 5 && action != 6) || (steps != 1 && steps != -1)) return false;
    return enqueue(action, steps, generation, time_ms, true, false, true);
}
bool aux_output_repeat(uint8_t action, int steps, uint32_t generation, uint32_t time_ms)
{ return enqueue(action, steps, generation, time_ms, false, true, false); }
void aux_output_cancel_gesture(void)
{
    taskENTER_CRITICAL(&aux_lock);
    bool retain_flight = flight && flight_action && count && entries[0].discrete &&
        flight_epoch == aux_epoch && flight_generation == aux_generation;
    unsigned kept = 0;
    for (unsigned i = 0; i < count; ++i) if (entries[i].discrete) {
        /* A quick lift preserves one tap, never a deferred stuck hold. */
        entries[i].hold = false;
        entries[kept++] = entries[i];
    }
    release_held();
    count = kept; ++aux_epoch;
    if (retain_flight) flight_epoch = aux_epoch;
    taskEXIT_CRITICAL(&aux_lock); input_wake_sender();
}
bool aux_output_take(aux_output_report_t *out, uint32_t generation, uint32_t time_ms)
{
    taskENTER_CRITICAL(&aux_lock);
    set_generation(generation);
    unsigned expired = 0;
    while (expired < count && time_ms - entries[expired].time_ms > 100) ++expired;
    if (expired) {
        count -= expired; ++aux_epoch;
        memmove(entries, entries + expired, count * sizeof(*entries));
    }
    if (!flight && count) release_held();
    bool ok = !flight && (release_due || count);
    if (ok) {
        *out = (aux_output_report_t){0};
        out->generation = generation; out->epoch = aux_epoch; out->release = release_due != 0;
        flight_release = release_due & 1 ? 1 : release_due & 2;
        flight_generation = generation;
        flight_epoch = aux_epoch;
        flight_action = 0;
        flight_steps = 0;
        if (release_due) { out->id = flight_release == 1 ? 7 : 8; out->length = flight_release == 1 ? 2 : 8; }
        else {
            aux_entry_t *e = &entries[0];
            flight_action = e->action;
            int sign = e->steps > 0 ? 1 : -1;
            flight_steps = sign;
            if (e->action <= 2) {
                out->id = 7; out->length = 2;
                out->data[0] = e->action == 1 ? (sign > 0 ? 0x6f : 0x70) : (sign > 0 ? 0xe9 : 0xea);
            } else if (e->action <= 4) {
                out->id = 2; out->length = 5;
                /* Keep each captured movement together instead of draining one
                 * wheel unit per USB poll. Never combine opposite directions. */
                flight_steps = e->steps > 127 ? 127 : e->steps < -127 ? -127 : e->steps;
                out->data[e->action == 3 ? 3 : 4] = (uint8_t)(int8_t)flight_steps;
            } else {
                out->id = 8; out->length = 8;
                out->data[2] = e->action == 5 ? (sign > 0 ? 0x52 : 0x51) : (sign > 0 ? 0x4f : 0x50);
            }
        }
        flight = true;
    }
    taskEXIT_CRITICAL(&aux_lock);
    return ok;
}
void aux_output_unsubmitted(void)
{ taskENTER_CRITICAL(&aux_lock); flight = false; taskEXIT_CRITICAL(&aux_lock); }
void aux_output_complete(bool success)
{
    taskENTER_CRITICAL(&aux_lock);
    if (flight) {
        if (flight_release) {
            if (success) { release_due &= ~flight_release; neutral_due &= ~flight_release; }
        }
        else {
            uint8_t mask = flight_action <= 2 ? 1 : flight_action >= 5 ? 2 : 0;
            bool current = success && count && flight_generation == aux_generation && flight_epoch == aux_epoch;
            /* Cancellation converts even an in-flight first hold into a tap.
             * Failed/stale accepted transfers may have reached the host: release. */
            if (current && entries[0].hold) held_mask |= mask;
            else release_due |= mask;
            if (current) {
                aux_entry_t *e = &entries[0];
                e->steps -= flight_steps;
                if (!e->steps) { --count; memmove(entries, entries + 1, count * sizeof(*entries)); }
            }
        }
        if (!success) { release_held(); count = 0; ++aux_epoch; }
        flight = false;
    }
    taskEXIT_CRITICAL(&aux_lock);
    input_wake_sender();
}
void aux_output_reset(bool connected)
{
    taskENTER_CRITICAL(&aux_lock);
    count = 0; ++aux_epoch; flight = false; release_due = connected ? 3 : 0;
    neutral_due = release_due; flight_release = 0;
    held_mask = 0;
    taskEXIT_CRITICAL(&aux_lock);
}
bool aux_output_active(void)
{ taskENTER_CRITICAL(&aux_lock); bool any = flight || release_due || count; taskEXIT_CRITICAL(&aux_lock); return any; }
bool aux_output_release_pending(void)
{ taskENTER_CRITICAL(&aux_lock); bool due = release_due; taskEXIT_CRITICAL(&aux_lock); return due; }

void aux_output_cancel(void)
{
    taskENTER_CRITICAL(&aux_lock); release_held(); count = 0; ++aux_epoch; taskEXIT_CRITICAL(&aux_lock);
    input_wake_sender();
}
bool aux_output_report_current(const aux_output_report_t *report)
{
    taskENTER_CRITICAL(&aux_lock);
    bool current = report->release || report->epoch == aux_epoch;
    taskEXIT_CRITICAL(&aux_lock);
    return current && (report->release || report->generation == input_generation());
}
void aux_output_resume(void)
{
    taskENTER_CRITICAL(&aux_lock);
    count = 0; ++aux_epoch; release_due |= 3; neutral_due |= 3; held_mask = 0;
    taskEXIT_CRITICAL(&aux_lock);
    input_wake_sender();
}
bool aux_output_neutral_pending(void)
{ taskENTER_CRITICAL(&aux_lock); bool due = neutral_due != 0; taskEXIT_CRITICAL(&aux_lock); return due; }

bool aux_output_take_event(aux_output_event_t *out, uint32_t generation, uint32_t now)
{
    taskENTER_CRITICAL(&aux_lock);
    set_generation(generation);
    while (count && now - entries[0].time_ms > 100) {
        --count; ++aux_epoch; memmove(entries, entries + 1, count * sizeof(*entries));
    }
    bool ok = !flight && (radio_epoch != aux_epoch || count);
    if (ok) {
        *out = (aux_output_event_t){.generation = generation, .epoch = aux_epoch};
        flight_action = 0; flight_steps = 0;
        if (radio_epoch == aux_epoch && count) {
            flight_action = out->action = entries[0].action;
            out->hold = entries[0].hold;
            flight_steps = out->steps = entries[0].steps > 127 ? 127 : entries[0].steps < -127 ? -127 : entries[0].steps;
        }
        flight_generation = generation; flight_epoch = aux_epoch; flight = true;
    }
    taskEXIT_CRITICAL(&aux_lock);
    return ok;
}
bool aux_output_event_current(const aux_output_event_t *e)
{
    taskENTER_CRITICAL(&aux_lock); bool ok = e->generation == aux_generation && e->epoch == aux_epoch;
    taskEXIT_CRITICAL(&aux_lock); return ok;
}
void aux_output_event_complete(bool success)
{
    taskENTER_CRITICAL(&aux_lock);
    if (flight) {
        if (success && flight_epoch == aux_epoch && flight_generation == aux_generation) {
            if (!flight_action) radio_epoch = flight_epoch;
            else if (count) {
                entries[0].steps -= flight_steps;
                if (!entries[0].steps) { --count; memmove(entries, entries + 1, count * sizeof(*entries)); }
            }
        }
        if (!success) { count = 0; ++aux_epoch; }
        flight = false;
    }
    taskEXIT_CRITICAL(&aux_lock); input_wake_sender();
}
