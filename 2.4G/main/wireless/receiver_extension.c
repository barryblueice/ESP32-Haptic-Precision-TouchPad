#include "wireless/receiver_extension.h"
#include "input/input_pipeline.h"
#include "SYS/aux_output.h"
#include "wireless/wireless.h"
#include "freertos/FreeRTOS.h"

static portMUX_TYPE extension_lock = portMUX_INITIALIZER_UNLOCKED;
static wire_surface_t requested, applied, ack_flight;
static uint8_t peer[6];
static bool pending_apply, ready, ack_pending;
static uint32_t last_sequence, last_seen;

void receiver_ext_receive(const uint8_t *mac, const uint8_t *packet, uint32_t now)
{
    wire_surface_t surface; wire_action_t action;
    if (wire_surface_decode(packet,38,WIRE_SURFACE,&surface)) {
        taskENTER_CRITICAL(&extension_lock);
        bool changed = requested.session != surface.session || requested.rotation != surface.rotation || memcmp(peer,mac,6);
        /* A live session belongs to one transmitter. */
        bool other = requested.session && memcmp(peer,mac,6) && now - last_seen < 5000;
        if (!other) {
            last_seen = now;
            if (changed) { requested = surface; memcpy(peer,mac,6); pending_apply = true; ready = false; last_sequence = 0; }
            ack_pending = true;
        }
        taskEXIT_CRITICAL(&extension_lock);
        if (!other) { input_link_seen(now); input_wake_sender(); wireless_wake_worker(); }
    } else if (wire_action_decode(packet,38,&action)) {
        taskENTER_CRITICAL(&extension_lock);
        bool accept = ready && action.session == applied.session && !memcmp(peer,mac,6) &&
            (!last_sequence || (int32_t)(action.sequence - last_sequence) > 0);
        if (accept) { last_sequence = action.sequence; last_seen = now; }
        taskEXIT_CRITICAL(&extension_lock);
        if (accept) {
            input_link_seen(now);
            if (!action.action) aux_output_cancel();
            else if (input_mode() == TP_PTP_MODE) {
                bool ok = action.hold ?
                    aux_output_hold(action.action,action.steps,input_generation(),now) :
                    aux_output_steps(action.action,action.steps,input_generation(),now);
                if (!ok) input_recover();
            }
        }
    }
}
bool receiver_ext_accept(const uint8_t *mac)
{
    taskENTER_CRITICAL(&extension_lock);
    bool ok = ready && !memcmp(peer,mac,6);
    taskEXIT_CRITICAL(&extension_lock); return ok;
}
bool receiver_ext_apply(wire_surface_t *surface)
{
    taskENTER_CRITICAL(&extension_lock);
    bool pending = pending_apply;
    if (pending) { *surface = requested; pending_apply = false; }
    taskEXIT_CRITICAL(&extension_lock); return pending;
}
void receiver_ext_applied(const wire_surface_t *surface)
{
    taskENTER_CRITICAL(&extension_lock); applied = *surface; taskEXIT_CRITICAL(&extension_lock);
}
void receiver_ext_usb_ready(bool mounted)
{
    taskENTER_CRITICAL(&extension_lock);
    ready = mounted && applied.session && applied.session == requested.session &&
        applied.rotation == requested.rotation && !pending_apply;
    taskEXIT_CRITICAL(&extension_lock);
}
bool receiver_ext_ack(uint8_t packet[38], uint8_t mac[6])
{
    taskENTER_CRITICAL(&extension_lock);
    bool ok = ready && ack_pending;
    if (ok) { ack_flight = applied; memcpy(mac,peer,6); wire_surface_encode(packet,WIRE_SURFACE_ACK,&applied); }
    taskEXIT_CRITICAL(&extension_lock); return ok;
}
void receiver_ext_ack_complete(bool success)
{
    taskENTER_CRITICAL(&extension_lock);
    if (success && ack_flight.session == requested.session && ack_flight.rotation == requested.rotation) ack_pending = false;
    taskEXIT_CRITICAL(&extension_lock);
}
