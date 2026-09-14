#include "edge_gesture.h"
#include <stdlib.h>
#include <string.h>
enum { EDGE_IDLE, EDGE_CANDIDATE, EDGE_CAPTURED, EDGE_BLOCKED, EDGE_SUPPRESSED };
void edge_gesture_reset(edge_gesture_t *s) { memset(s, 0, sizeof(*s)); }
static bool inside(unsigned edge, const uint8_t *record, uint16_t x, uint16_t y, uint16_t xmax, uint16_t ymax)
{
    uint32_t distance = edge == 0 ? y : edge == 1 ? ymax - y : edge == 2 ? x : xmax - x;
    return distance * 100 <= (uint32_t)record[3] * (edge < 2 ? ymax : xmax);
}
edge_result_t edge_gesture_update(edge_gesture_t *s, const device_config_t *c,
                                  const tp_multi_msg_t *m, uint16_t xmax, uint16_t ymax)
{
    edge_result_t result = {0};
    unsigned count = 0, id = 0;
    for (unsigned i = 0; i < 5; ++i) if (m->fingers[i].tip_switch) { ++count; id = i; }
    if (!count) {
        if (s->state == EDGE_CANDIDATE) {
            result.tap = true; result.tap_down = s->start;
            result.tap_down.scan_time = m->scan_time - 1;
        }
        edge_gesture_reset(s); return result;
    }
    if (s->state == EDGE_BLOCKED || s->state == EDGE_SUPPRESSED) {
        result.suppress = s->state == EDGE_SUPPRESSED; return result;
    }
    if (count != 1 || !m->fingers[id].confidence ||
        (s->state != EDGE_IDLE && id != s->id)) {
        result.suppress = s->state == EDGE_CAPTURED;
        s->state = result.suppress ? EDGE_SUPPRESSED : EDGE_BLOCKED; return result;
    }
    uint16_t x = m->fingers[id].x, y = m->fingers[id].y;
    if (s->state == EDGE_IDLE) {
        for (unsigned e = 0; e < 4; ++e) {
            const uint8_t *record = c->bytes + CFG_EDGES + 5 * e;
            if (record[0] && inside(e, record, x, y, xmax, ymax)) s->mask |= 1U << e;
        }
        if (!s->mask) { s->state = EDGE_BLOCKED; return result; }
        s->state = EDGE_CANDIDATE; s->id = id; s->start_x = x; s->start_y = y; s->start = *m;
    }
    if (s->state == EDGE_CANDIDATE) {
        uint8_t available = 0;
        for (unsigned e = 0; e < 4; ++e)
            if ((s->mask & (1U << e)) && inside(e, c->bytes + CFG_EDGES + 5 * e, x, y, xmax, ymax)) available |= 1U << e;
        if (!available) { s->state = EDGE_BLOCKED; return result; }
        s->mask = available;
        uint32_t dx = abs((int)x - s->start_x) * (uint32_t)ymax;
        uint32_t dy = abs((int)y - s->start_y) * (uint32_t)xmax;
        if ((available & 3) && (available & 12)) {
            if (dx == dy) { result.suppress = true; return result; }
            available &= dx > dy ? 3 : 12;
        }
        unsigned e = 0; while (!(available & (1U << e))) ++e;
        s->edge = e; s->last = e < 2 ? s->start_x : s->start_y;
        int delta = e < 2 ? (int)x - s->last : (int)s->last - y;
        int step = c->bytes[CFG_EDGES + e * 5 + 4] * (e < 2 ? xmax : ymax);
        if (abs(delta) * 100 < step) { result.suppress = true; return result; }
        s->state = EDGE_CAPTURED;
    }
    const uint8_t *record = c->bytes + CFG_EDGES + s->edge * 5;
    result.suppress = true;
    if (!inside(s->edge, record, x, y, xmax, ymax)) { s->state = EDGE_SUPPRESSED; return result; }
    int pos = s->edge < 2 ? x : y;
    int delta = s->edge < 2 ? pos - s->last : s->last - pos;
    s->last = pos; s->remainder += delta * 100;
    int step = record[4] * (s->edge < 2 ? xmax : ymax);
    result.steps = s->remainder / step; s->remainder %= step;
    if (record[2]) result.steps = -result.steps;
    result.action = record[1];
    return result;
}
