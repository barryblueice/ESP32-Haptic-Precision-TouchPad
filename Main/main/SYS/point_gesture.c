#include "point_gesture.h"
#include <string.h>
#include <stdlib.h>

enum { POINT_IDLE, POINT_ACTIVE, POINT_PASSTHROUGH, POINT_SUPPRESSED };
void point_gesture_reset(point_gesture_t *s) { memset(s, 0, sizeof(*s)); }
bool point_gesture_inside(unsigned p, unsigned radius, uint16_t x, uint16_t y,
    uint16_t xmax, uint16_t ymax, uint16_t width, uint16_t height)
{
    if (p >= 4 || !xmax || !ymax || !width || !height || x > xmax || y > ymax) return false;
    /* Compare in physical units without rounding positions onto the circle.
     * These products are bounded for the surface's 2302/1532 logical ranges. */
    double dx = (double)((p & 1) ? xmax - x : x) * width / xmax;
    double dy = (double)((p & 2) ? ymax - y : y) * height / ymax;
    double r = (double)(width < height ? width : height) * radius / 100;
    return dx * dx + dy * dy <= r * r;
}
static point_result_t action(const point_gesture_t *s)
{
    int sign = (s->action & 1) ? 1 : -1;
    return (point_result_t){.suppress = true, .action = (s->action + 1) / 2,
        .steps = s->reverse ? -sign : sign};
}
bool point_gesture_repeating(const point_gesture_t *s) { return s->state == POINT_ACTIVE && s->repeat; }
point_result_t point_gesture_tick(point_gesture_t *s, uint32_t now)
{
    point_result_t r = {.suppress = s->state == POINT_ACTIVE || s->state == POINT_SUPPRESSED};
    if (point_gesture_repeating(s) && (int32_t)(now - s->repeat_at) >= 0) {
        s->repeat_at = now + 100; r = action(s);
    }
    return r;
}
point_result_t point_gesture_update(point_gesture_t *s, const device_config_t *c,
    const tp_multi_msg_t *m, uint16_t xmax, uint16_t ymax, uint16_t width, uint16_t height, uint32_t now)
{
    point_result_t r = {0};
    unsigned count = 0, id = 0;
    for (unsigned i = 0; i < 5; ++i) if (m->fingers[i].tip_switch) { ++count; id = i; }
    if (!count) {
        r.cancel = s->state == POINT_ACTIVE || s->state == POINT_SUPPRESSED;
        point_gesture_reset(s); return r;
    }
    if (s->state == POINT_PASSTHROUGH) return r;
    if (s->state == POINT_SUPPRESSED) { r.suppress = true; return r; }
    if (count != 1 || !m->fingers[id].confidence ||
        m->fingers[id].x > xmax || m->fingers[id].y > ymax ||
        (s->state == POINT_ACTIVE && id != s->id)) {
        r.cancel = r.suppress = s->state == POINT_ACTIVE;
        s->state = r.suppress ? POINT_SUPPRESSED : POINT_PASSTHROUGH; return r;
    }
    uint16_t x = m->fingers[id].x, y = m->fingers[id].y;
    if (s->state == POINT_IDLE) {
        for (unsigned p = 0; p < 4; ++p) {
            const uint8_t *record = c->bytes + CFG_POINTS + p * 5;
            if (!record[0] || !point_gesture_inside(p, record[3], x, y, xmax, ymax, width, height)) continue;
            *s = (point_gesture_t){.state = POINT_ACTIVE, .owned = true, .point = p, .id = id,
                .action = record[1], .reverse = record[2], .step = record[4],
                .repeat = (c->bytes[7] & (0x10U << p)) != 0,
                .convert = device_config_point_to_edge(c, p), .start_x = x, .start_y = y,
                .repeat_at = now + 400};
            r = action(s); r.initial = true; return r;
        }
        s->state = POINT_PASSTHROUGH; return r;
    }
    if (s->convert && ((uint32_t)abs((int)x - s->start_x) * 100 >= (uint32_t)s->step * xmax ||
                       (uint32_t)abs((int)y - s->start_y) * 100 >= (uint32_t)s->step * ymax)) {
        s->state = POINT_PASSTHROUGH;
        r.cancel = r.handoff = true; return r;
    }
    return point_gesture_tick(s, now);
}
