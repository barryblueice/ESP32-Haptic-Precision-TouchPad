#include "usb_aux.h"
#include "SYS/input_pipeline.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

typedef struct { uint8_t action; int steps; uint32_t generation, time_ms; } aux_entry_t;
static aux_entry_t entries[32];
static unsigned count;
static bool flight;
/* Bit 0: Consumer (7), bit 1: keyboard (8). Kept until completion. */
static uint8_t release_due, neutral_due, flight_release;
static uint32_t aux_generation;
static uint32_t flight_generation, aux_epoch, flight_epoch;
static uint8_t flight_action;
static int flight_steps;
static portMUX_TYPE aux_lock = portMUX_INITIALIZER_UNLOCKED;

bool usb_aux_steps(uint8_t action, int steps, uint32_t generation, uint32_t time_ms)
{
    if (!steps) return true;
    if (action < 1 || action > 6) return false;
    taskENTER_CRITICAL(&aux_lock);
    if (aux_generation != generation) { count = 0; ++aux_epoch; aux_generation = generation; }
    bool ok = count < 32;
    if (ok) entries[count++] = (aux_entry_t){action, steps, generation, time_ms};
    else { count = 0; ++aux_epoch; }
    taskEXIT_CRITICAL(&aux_lock);
    input_wake_sender();
    return ok;
}
bool usb_aux_take(usb_aux_report_t *out, uint32_t generation, uint32_t time_ms)
{
    taskENTER_CRITICAL(&aux_lock);
    if (generation != aux_generation) { count = 0; ++aux_epoch; aux_generation = generation; }
    unsigned expired = 0;
    while (expired < count && time_ms - entries[expired].time_ms > 100) ++expired;
    if (expired) {
        count -= expired; ++aux_epoch;
        memmove(entries, entries + expired, count * sizeof(*entries));
    }
    bool ok = !flight && (release_due || count);
    if (ok) {
        *out = (usb_aux_report_t){0};
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
void usb_aux_unsubmitted(void)
{ taskENTER_CRITICAL(&aux_lock); flight = false; taskEXIT_CRITICAL(&aux_lock); }
void usb_aux_complete(bool success)
{
    taskENTER_CRITICAL(&aux_lock);
    if (flight) {
        if (flight_release) {
            if (success) { release_due &= ~flight_release; neutral_due &= ~flight_release; }
        }
        else {
            /* A failed accepted transfer may have reached the host. Always release. */
            release_due |= flight_action <= 2 ? 1 : flight_action >= 5 ? 2 : 0;
            if (success && count && flight_generation == aux_generation && flight_epoch == aux_epoch) {
                aux_entry_t *e = &entries[0];
                e->steps -= flight_steps;
                if (!e->steps) { --count; memmove(entries, entries + 1, count * sizeof(*entries)); }
            }
        }
        if (!success) { count = 0; ++aux_epoch; }
        flight = false;
    }
    taskEXIT_CRITICAL(&aux_lock);
    input_wake_sender();
}
void usb_aux_reset(bool connected)
{
    taskENTER_CRITICAL(&aux_lock);
    count = 0; ++aux_epoch; flight = false; release_due = connected ? 3 : 0;
    neutral_due = release_due; flight_release = 0;
    taskEXIT_CRITICAL(&aux_lock);
}
bool usb_aux_active(void)
{ taskENTER_CRITICAL(&aux_lock); bool any = flight || release_due || count; taskEXIT_CRITICAL(&aux_lock); return any; }
bool usb_aux_release_pending(void)
{ taskENTER_CRITICAL(&aux_lock); bool due = release_due; taskEXIT_CRITICAL(&aux_lock); return due; }

void usb_aux_cancel(void)
{
    taskENTER_CRITICAL(&aux_lock); count = 0; ++aux_epoch; taskEXIT_CRITICAL(&aux_lock);
    input_wake_sender();
}
bool usb_aux_report_current(const usb_aux_report_t *report)
{
    taskENTER_CRITICAL(&aux_lock);
    bool current = report->release || report->epoch == aux_epoch;
    taskEXIT_CRITICAL(&aux_lock);
    return current && (report->release || report->generation == input_generation());
}
void usb_aux_resume(void)
{
    taskENTER_CRITICAL(&aux_lock);
    count = 0; ++aux_epoch; release_due |= 3; neutral_due |= 3;
    taskEXIT_CRITICAL(&aux_lock);
    input_wake_sender();
}
bool usb_aux_neutral_pending(void)
{ taskENTER_CRITICAL(&aux_lock); bool due = neutral_due != 0; taskEXIT_CRITICAL(&aux_lock); return due; }
