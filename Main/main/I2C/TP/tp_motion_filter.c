#include "tp_motion_filter.h"

/* Coordinates are sensor counts after rotation, cutoffs Hz, speed counts/s.
 * A common XY coefficient avoids axis-dependent lag. These defaults balance
 * light stationary smoothing with minimal moving delay. The controller
 * already supplies coordinates: do not add a whole frame of median latency. */
#define MIN_CUTOFF 20.0f
#define MAX_CUTOFF 120.0f
#define SPEED_CUTOFF 20.0f
#define SPEED_BETA 0.12f
#define RESET_GAP_MS 100U
#define JUMP_DISTANCE 300

static float alpha(float cutoff, float dt)
{
    float step = 6.283185307f * cutoff * dt;
    return step / (1.0f + step);
}

void tp_motion_filter_update(tp_motion_filter_t *s, uint16_t x, uint16_t y,
                             uint32_t time_ms, uint16_t *out_x, uint16_t *out_y)
{
    uint32_t elapsed = time_ms - s->time_ms;
    if (!s->active || elapsed > RESET_GAP_MS) {
        s->guarded_x = x;
        s->guarded_y = y;
        s->x = x;
        s->y = y;
        s->speed = 0;
        s->jump_count = 0;
        s->active = true;
        s->time_ms = time_ms;
        *out_x = x;
        *out_y = y;
        return;
    }
    s->time_ms = time_ms;
    /* Capture time also handles buffered replay. Tolerate equal timestamps,
     * bound a delayed sample's influence, and allow uint32_t wraparound. */
    float dt = (elapsed < 1U ? 1U : elapsed > 32U ? 32U : elapsed) * 0.001f;
    uint16_t mx = x, my = y;
    int32_t dx = (int32_t)mx - s->guarded_x;
    int32_t dy = (int32_t)my - s->guarded_y;
    /* Compare accepted samples, not the lagging low-pass output. Wide products
     * also avoid overflow for malformed full-range uint16_t coordinates. */
    if ((int64_t)dx * dx + (int64_t)dy * dy > JUMP_DISTANCE * JUMP_DISTANCE) {
        if (++s->jump_count < 3U) {
            *out_x = (uint16_t)(s->x + 0.5f);
            *out_y = (uint16_t)(s->y + 0.5f);
            return;
        }
    }
    s->jump_count = 0;
    s->guarded_x = mx;
    s->guarded_y = my;
    float vx = (float)(dx < 0 ? -dx : dx) / dt;
    float vy = (float)(dy < 0 ? -dy : dy) / dt;
    float speed = vx > vy ? vx : vy;
    s->speed += alpha(SPEED_CUTOFF, dt) * (speed - s->speed);
    float cutoff = MIN_CUTOFF + SPEED_BETA * s->speed;
    if (cutoff > MAX_CUTOFF) cutoff = MAX_CUTOFF;
    float a = alpha(cutoff, dt);
    s->x += a * ((float)mx - s->x);
    s->y += a * ((float)my - s->y);
    *out_x = (uint16_t)(s->x + 0.5f);
    *out_y = (uint16_t)(s->y + 0.5f);
}
