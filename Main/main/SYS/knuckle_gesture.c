#include "knuckle_gesture.h"
#include "knuckle_model.h"
#include <string.h>
#include <stdlib.h>

void knuckle_gesture_reset(knuckle_gesture_t *s) { memset(s, 0, sizeof(*s)); }

static bool knuckle_model_accepts(const knuckle_model_head_t *model, float pressure, float area)
{
    if (pressure < model->low[0] || pressure > model->high[0] ||
        area < model->low[1] || area > model->high[1]) return false;
    return model->bias + model->weight[0]*pressure + model->weight[1]*area >= 0.0f;
}

knuckle_result_t knuckle_gesture_update(knuckle_gesture_t *s, const uint8_t p[64], uint32_t now)
{
    knuckle_result_t r = {0};
    if (!KNUCKLE_ENABLED || p[0] != 0x40 || p[1]) {
        knuckle_gesture_reset(s); return r;
    }
    unsigned count = 0, id = 0;
    for (unsigned i = 0; i < 5; ++i) if (p[4 + 8*i] & 1) { ++count; id = i; }
    if (!s->active && s->pending && now - s->lifted_at > KNUCKLE_GAP_MAX_MS) s->pending = false;
    if (!s->active) {
        if (!count) return r;
        s->active = true; s->down_at = s->last_at = now;
        s->id = id; s->claimed = false;
        /* Onset admission uses the learned classifier and support envelope.
         * Once passed through, a contact is never stolen from the host later. */
        unsigned pressure = p[4 + 8*id + 5];
        unsigned area = (unsigned)p[4 + 8*id + 6] * p[4 + 8*id + 7];
        s->rejected = count != 1 || !knuckle_model_accepts(&knuckle_model_onset, pressure, area);
        s->pressure_sum = s->weighted_area_sum = 0;
        s->peak_pressure = s->samples = 0;
        s->x = p[5 + 8*id] | ((uint16_t)p[6 + 8*id] << 8);
        s->y = p[7 + 8*id] | ((uint16_t)p[8 + 8*id] << 8);
    }
    if (now - s->last_at > KNUCKLE_SAMPLE_GAP_MS ||
        now - s->down_at > KNUCKLE_DOWN_MAX_MS) s->rejected = true;
    s->last_at = now;
    if (count) {
        const uint8_t *f = p + 4 + 8*id;
        uint16_t x = f[1] | ((uint16_t)f[2] << 8), y = f[3] | ((uint16_t)f[4] << 8);
        unsigned area = (unsigned)f[6] * f[7];
        if (count != 1 || id != s->id || x > 2302 || y > 1532 || !f[6] || !f[7] ||
            s->samples >= KNUCKLE_MAX_SAMPLES ||
            abs((int)x - s->x) > KNUCKLE_MOVE_MAX ||
            abs((int)y - s->y) > KNUCKLE_MOVE_MAX) s->rejected = true;
        if (!s->rejected) {
            s->claimed = true;
            ++s->samples;
            s->pressure_sum += f[5];
            s->weighted_area_sum += (uint32_t)f[5] * area;
            if (f[5] > s->peak_pressure) s->peak_pressure = f[5];
        }
    }
    r.suppress = s->claimed;
    if (s->rejected) s->pending = false;
    if (count) return r;
    /* Classify the complete contact, not each frame. A low-Z, large-area
     * release tail contributes less to weighted area instead of vetoing it.
     * With <=64 samples, even 255*255 area and Z=255 fit uint32_t sums. */
    if (!s->rejected && now - s->down_at >= KNUCKLE_DOWN_MIN_MS && s->pressure_sum &&
        knuckle_model_accepts(&knuckle_model_complete, s->peak_pressure,
            (float)s->weighted_area_sum / s->pressure_sum)) {
        if (s->pending && s->down_at - s->lifted_at >= KNUCKLE_GAP_MIN_MS &&
            abs((int)s->x - s->first_x) <= KNUCKLE_PAIR_DISTANCE &&
            abs((int)s->y - s->first_y) <= KNUCKLE_PAIR_DISTANCE) {
            r.screenshot = true; s->pending = false;
        } else {
            s->pending = true; s->first_x = s->x; s->first_y = s->y; s->lifted_at = now;
        }
    } else s->pending = false;
    /* A rejected claimed contact remains hidden until this final lift; it
     * must not turn into a delayed mouse click or drag. */
    s->active = s->claimed = false;
    return r;
}
