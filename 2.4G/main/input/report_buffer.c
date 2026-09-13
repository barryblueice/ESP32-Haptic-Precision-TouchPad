/* Adapted from Main's report buffer; adds legacy PTP and an O(1) ring dequeue. */
#include "report_buffer.h"
#include <limits.h>
#include <string.h>

void report_buffer_reset(report_buffer_t *b)
{
    b->head = b->count = 0;
    ++b->generation;
    ++b->stats.recoveries;
    b->release_mask = REPORT_RELEASE_MASK;
    b->recovering = true;
    b->all_up = false;
    b->last = (input_report_t){0};
}

bool report_all_up(const input_report_t *r)
{
    if (r->kind == REPORT_MOUSE) return r->data.mouse.buttons == 0;
    if (r->kind == REPORT_HAPTIC) {
        if (r->data.haptic_ptp.buttons) return false;
        for (unsigned i = 0; i < 5; ++i)
            if (r->data.haptic_ptp.fingers[i].tip_conf_id & 2) return false;
        return true;
    }
    if (r->kind == REPORT_LEGACY) {
        if (r->data.legacy_ptp.buttons) return false;
        for (unsigned i = 0; i < 5; ++i)
            if (r->data.legacy_ptp.fingers[i].tip_conf_id & 2) return false;
        return true;
    }
    return false;
}

static bool same_state(const input_report_t *a, const input_report_t *r)
{
    if (a->kind != r->kind) return false;
    if (r->kind == REPORT_MOUSE) return a->data.mouse.buttons == r->data.mouse.buttons;
    if (r->kind == REPORT_HAPTIC) {
        if (a->data.haptic_ptp.buttons != r->data.haptic_ptp.buttons ||
            a->data.haptic_ptp.contact_count != r->data.haptic_ptp.contact_count) return false;
        for (unsigned i = 0; i < 5; ++i)
            if (a->data.haptic_ptp.fingers[i].tip_conf_id != r->data.haptic_ptp.fingers[i].tip_conf_id) return false;
    } else {
        if (a->data.legacy_ptp.buttons != r->data.legacy_ptp.buttons ||
            a->data.legacy_ptp.contact_count != r->data.legacy_ptp.contact_count) return false;
        for (unsigned i = 0; i < 5; ++i)
            if (a->data.legacy_ptp.fingers[i].tip_conf_id != r->data.legacy_ptp.fingers[i].tip_conf_id) return false;
    }
    return true;
}

static bool can_add(int32_t a, int32_t d)
{
    return !((d > 0 && a > INT32_MAX - d) || (d < 0 && a < INT32_MIN - d));
}

bool report_buffer_push(report_buffer_t *b, const input_report_t *r)
{
    if (!report_size(r->kind) || r->generation != b->generation) return false;
    if (b->active && b->active != r->kind) {
        b->active = r->kind;
        report_buffer_reset(b);
        return false;
    }
    b->active = r->kind;
    b->all_up = report_all_up(r);
    if (b->recovering) {
        if (b->all_up && !b->release_mask) b->recovering = false;
        /* The neutral observation ends recovery; never replay its mouse delta. */
        return false;
    }
    bool edge = !same_state(&b->last, r);
    report_entry_t *e = b->count ? &b->entries[(b->head + b->count - 1) % REPORT_BUFFER_CAPACITY] : NULL;
    if (!edge && e && !e->edge && same_state(&e->report, r)) {
        if (r->kind == REPORT_MOUSE) {
            const mouse_hid_report_t *m = &r->data.mouse;
            if (!can_add(e->x, m->x) || !can_add(e->y, m->y) ||
                !can_add(e->wheel, m->wheel) || !can_add(e->pan, m->pan)) {
                report_buffer_reset(b);
                return false;
            }
            e->x += m->x; e->y += m->y; e->wheel += m->wheel; e->pan += m->pan;
        } else e->report.data = r->data;
        ++b->stats.merged;
    } else {
        if (b->count == REPORT_BUFFER_CAPACITY) {
            report_buffer_reset(b);
            return false;
        }
        e = &b->entries[(b->head + b->count++) % REPORT_BUFFER_CAPACITY];
        *e = (report_entry_t){.report = *r, .edge = edge};
        if (r->kind == REPORT_MOUSE) {
            e->x = r->data.mouse.x; e->y = r->data.mouse.y;
            e->wheel = r->data.mouse.wheel; e->pan = r->data.mouse.pan;
        }
        if (b->count > b->stats.peak) b->stats.peak = b->count;
    }
    b->last = *r;
    return true;
}

static void make_release(report_buffer_t *b, report_kind_t kind, uint32_t now, input_report_t *out)
{
    *out = b->submitted[kind];
    out->kind = kind;
    out->generation = b->generation;
    out->time_ms = now;
    out->release = true;
    if (kind == REPORT_MOUSE) out->data = (report_payload_t){0};
    else if (kind == REPORT_HAPTIC) {
        out->data.haptic_ptp.buttons = 0;
        out->data.haptic_ptp.contact_count = 5;
        for (unsigned i = 0; i < 5; ++i) {
            haptic_finger_t *f = &out->data.haptic_ptp.fingers[i];
            f->tip_conf_id = b->submitted[kind].kind ? (f->tip_conf_id & ~2U) : ((i << 2) | 1);
            f->pressure_z = 0;
        }
    } else {
        out->data.legacy_ptp.buttons = 0;
        out->data.legacy_ptp.contact_count = 5;
        for (unsigned i = 0; i < 5; ++i) {
            legacy_finger_t *f = &out->data.legacy_ptp.fingers[i];
            f->tip_conf_id = b->submitted[kind].kind ? (f->tip_conf_id & ~2U) : ((i << 2) | 1);
        }
    }
}

static bool expired(report_buffer_t *b, uint32_t time, uint32_t now)
{
    uint32_t age = now - time;
    if (age > b->stats.longest_wait_ms) b->stats.longest_wait_ms = age;
    if (age <= REPORT_MAX_AGE_MS) return false;
    report_buffer_reset(b);
    return true;
}

static int8_t split_axis(int32_t *value)
{
    int32_t part = *value > 127 ? 127 : (*value < -127 ? -127 : *value);
    *value -= part;
    return (int8_t)part;
}

bool report_buffer_take(report_buffer_t *b, uint32_t now, input_report_t *out)
{
    if (b->count) expired(b, b->entries[b->head].report.time_ms, now);
    for (report_kind_t k = REPORT_HAPTIC; k <= REPORT_MOUSE; ++k) {
        if (b->release_mask & (1U << k)) {
            make_release(b, k, now, out);
            return true;
        }
    }
    if (!b->count || b->recovering) return false;
    report_entry_t *e = &b->entries[b->head];
    *out = e->report;
    if (out->kind == REPORT_MOUSE) {
        out->data.mouse.x = split_axis(&e->x); out->data.mouse.y = split_axis(&e->y);
        out->data.mouse.wheel = split_axis(&e->wheel); out->data.mouse.pan = split_axis(&e->pan);
        if (e->x || e->y || e->wheel || e->pan) return true;
    }
    b->head = (b->head + 1) % REPORT_BUFFER_CAPACITY;
    --b->count;
    return true;
}

bool report_buffer_current(report_buffer_t *b, const input_report_t *r, uint32_t now)
{
    return r->generation == b->generation && (r->release || !expired(b, r->time_ms, now));
}

void report_buffer_ack(report_buffer_t *b, const input_report_t *r)
{
    if (r->generation != b->generation) return;
    if (r->release) b->release_mask &= ~(1U << r->kind);
    if (!b->release_mask && b->all_up) b->recovering = false;
}

void report_buffer_submitted(report_buffer_t *b, const input_report_t *r)
{
    /* Even a just-invalidated in-flight report may have reached the host. */
    if (report_size(r->kind)) b->submitted[r->kind] = *r;
}
