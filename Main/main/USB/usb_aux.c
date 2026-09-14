#include "usb_aux.h"
#include "SYS/input_pipeline.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

typedef struct { uint8_t action; int steps; uint32_t generation, time_ms; } aux_entry_t;
static aux_entry_t entries[32];
static unsigned count;
static bool flight, release_due, flight_release;
static uint32_t aux_generation;
static uint32_t flight_generation;
static uint8_t flight_action;
static portMUX_TYPE aux_lock = portMUX_INITIALIZER_UNLOCKED;

bool usb_aux_steps(uint8_t action, int steps, uint32_t generation, uint32_t time_ms)
{
    if (!steps) return true;
    if (action < 1 || action > 4) return false;
    taskENTER_CRITICAL(&aux_lock);
    if (aux_generation != generation) { count = 0; aux_generation = generation; }
    bool ok = count < 32;
    if (ok) entries[count++] = (aux_entry_t){action, steps, generation, time_ms};
    else count = 0;
    taskEXIT_CRITICAL(&aux_lock);
    input_wake_sender();
    return ok;
}
bool usb_aux_take(usb_aux_report_t *out, uint32_t generation, uint32_t time_ms)
{
    taskENTER_CRITICAL(&aux_lock);
    if (generation != aux_generation) { count = 0; aux_generation = generation; }
    if (count && time_ms - entries[0].time_ms > 100) count = 0;
    bool ok = !flight && (release_due || count);
    if (ok) {
        *out = (usb_aux_report_t){0};
        out->generation = generation; out->release = release_due;
        flight_release = release_due;
        flight_generation = generation;
        flight_action = 0;
        if (release_due) { out->id = 7; out->length = 2; }
        else {
            aux_entry_t *e = &entries[0];
            flight_action = e->action;
            int sign = e->steps > 0 ? 1 : -1;
            if (e->action <= 2) {
                out->id = 7; out->length = 2;
                out->data[0] = e->action == 1 ? (sign > 0 ? 0x6f : 0x70) : (sign > 0 ? 0xe9 : 0xea);
            } else {
                out->id = 2; out->length = 5;
                out->data[e->action == 3 ? 3 : 4] = (uint8_t)(int8_t)sign;
            }
        }
        flight = true;
    }
    taskEXIT_CRITICAL(&aux_lock);
    return ok;
}
void usb_aux_unsubmitted(void)
{ taskENTER_CRITICAL(&aux_lock); flight = false; taskEXIT_CRITICAL(&aux_lock); }
void usb_aux_complete(bool success)
{
    taskENTER_CRITICAL(&aux_lock);
    if (flight) {
        if (flight_release) release_due = !success;
        else {
            /* A failed accepted transfer may have reached the host. Always release. */
            release_due = flight_action <= 2;
            if (success && count && flight_generation == aux_generation) {
                aux_entry_t *e = &entries[0];
                e->steps += e->steps > 0 ? -1 : 1;
                if (!e->steps) { --count; memmove(entries, entries + 1, count * sizeof(*entries)); }
            }
        }
        if (!success) count = 0;
        flight = false;
    }
    taskEXIT_CRITICAL(&aux_lock);
    input_wake_sender();
}
void usb_aux_reset(bool connected)
{
    taskENTER_CRITICAL(&aux_lock);
    count = 0; flight = false; release_due = connected; flight_release = false;
    taskEXIT_CRITICAL(&aux_lock);
}
bool usb_aux_active(void)
{ taskENTER_CRITICAL(&aux_lock); bool any = flight || release_due || count; taskEXIT_CRITICAL(&aux_lock); return any; }
bool usb_aux_release_pending(void)
{ taskENTER_CRITICAL(&aux_lock); bool due = release_due; taskEXIT_CRITICAL(&aux_lock); return due; }
