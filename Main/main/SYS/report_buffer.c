#include "report_buffer.h"
#include <string.h>
#include <limits.h>

void report_buffer_reset(report_buffer_t *b, uint8_t mode)
{
    /* Release both the previous and new logical devices on a mode change. */
    b->release_mask |= (1U << b->mode) | (1U << mode);
    b->mode = mode;
    b->count = 0;
    ++b->generation;
    ++b->stats.recoveries;
    b->recovering = true;
    b->all_up = false;
    memset(&b->last, 0, sizeof(b->last));
    b->last.mode = mode;
}

bool report_buffer_observe(report_buffer_t *b, bool all_up)
{
    b->all_up = all_up;
    if (b->recovering && all_up && !b->release_mask) b->recovering = false;
    return !b->recovering;
}

static bool same_state(const input_report_t *a, const input_report_t *c)
{
    if (a->mode != c->mode) return false;
    if (a->mode == MOUSE_MODE) return a->data.mouse.buttons == c->data.mouse.buttons;
    if (a->data.ptp.buttons != c->data.ptp.buttons ||
        a->data.ptp.contact_count != c->data.ptp.contact_count) return false;
    for (unsigned i = 0; i < a->data.ptp.contact_count && i < 5; ++i)
        if (a->data.ptp.fingers[i].tip_conf_id != c->data.ptp.fingers[i].tip_conf_id) return false;
    return true;
}

static bool add_axis(int32_t *acc, int32_t delta)
{
    if ((delta > 0 && *acc > INT32_MAX - delta) ||
        (delta < 0 && *acc < INT32_MIN - delta)) return false;
    *acc += delta;
    return true;
}

static bool append(report_buffer_t *b, const input_report_t *r)
{
    bool edge = !same_state(&b->last, r);
    report_entry_t *e = b->count ? &b->entries[b->count - 1] : NULL;
    /* Never move an edge's coordinates or deltas to a later sample. */
    if (!edge && e && !e->edge && same_state(&e->report, r)) {
        if (r->mode == PTP_MODE) e->report.data.ptp = r->data.ptp;
        else if (!add_axis(&e->x, r->data.mouse.x) || !add_axis(&e->y, r->data.mouse.y) ||
                 !add_axis(&e->wheel, r->data.mouse.wheel) || !add_axis(&e->pan, r->data.mouse.pan)) return false;
        ++b->stats.merged;
    } else {
        if (b->count == REPORT_BUFFER_CAPACITY) return false;
        e = &b->entries[b->count++];
        *e = (report_entry_t){.report = *r, .edge = edge};
        e->report.generation = b->generation;
        if (r->mode == MOUSE_MODE) {
            e->x = r->data.mouse.x; e->y = r->data.mouse.y;
            e->wheel = r->data.mouse.wheel; e->pan = r->data.mouse.pan;
        }
    }
    b->last = *r;
    if (b->count > b->stats.peak) b->stats.peak = b->count;
    return true;
}

bool report_buffer_push(report_buffer_t *b, const input_report_t *r, bool tap)
{
    if (b->recovering || r->mode != b->mode) return false;
    /* Reserve the complete tap before publishing either half. */
    if ((tap && b->count > REPORT_BUFFER_CAPACITY - 2) || !append(b, r)) {
        report_buffer_reset(b, b->mode);
        return false;
    }
    if (tap) {
        input_report_t release = {.mode = MOUSE_MODE, .time_ms = r->time_ms};
        if (!append(b, &release)) {
            report_buffer_reset(b, b->mode);
            return false;
        }
    }
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
    if (b->count) {
        uint32_t age = now - b->entries[0].report.time_ms;
        if (age > b->stats.longest_wait_ms) b->stats.longest_wait_ms = age;
        if (age > REPORT_MAX_AGE_MS) report_buffer_reset(b, b->mode);
    }
    if (b->release_mask) {
        *out = (input_report_t){.mode = (b->release_mask & 1) ? MOUSE_MODE : PTP_MODE,
            .generation = b->generation, .time_ms = now, .release = true};
        if (out->mode == PTP_MODE) {
            out->data.ptp.contact_count = 5;
            for (unsigned i = 0; i < 5; ++i) out->data.ptp.fingers[i].tip_conf_id = (i << 2) | 1;
        }
        return true;
    }
    if (!b->count) return false;
    report_entry_t *e = &b->entries[0];
    *out = e->report;
    if (out->mode == MOUSE_MODE) {
        out->data.mouse.x = split_axis(&e->x); out->data.mouse.y = split_axis(&e->y);
        out->data.mouse.wheel = split_axis(&e->wheel); out->data.mouse.pan = split_axis(&e->pan);
        if (e->x || e->y || e->wheel || e->pan) return true;
    }
    --b->count;
    memmove(b->entries, b->entries + 1, b->count * sizeof(*e));
    return true;
}

bool report_buffer_current(const report_buffer_t *b, const input_report_t *r)
{
    return b->generation == r->generation;
}

void report_buffer_ack(report_buffer_t *b, const input_report_t *r)
{
    if (!report_buffer_current(b, r)) return;
    if (r->release) b->release_mask &= ~(1U << r->mode);
    if (!b->release_mask && b->all_up) b->recovering = false;
}
