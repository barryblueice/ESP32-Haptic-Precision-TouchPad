#include "pointer_motion.h"
#include <math.h>

#define MIN_CUTOFF 15.0f
#define MAX_CUTOFF 120.0f
#define SPEED_CUTOFF 20.0f
#define SPEED_BETA 0.04f
#define JUMP_DISTANCE 300.0f
#define CANDIDATE_RADIUS 30.0f

static float alpha(float hz, float dt)
{
    float a = 6.283185307f * hz * dt;
    return a / (1.0f + a);
}
static pointer_step_t anchor(pointer_motion_t *s, float x, float y, uint16_t time)
{
    *s = (pointer_motion_t){.active = true, .x = x, .y = y,
        .raw_x = x, .raw_y = y, .time = time};
    return (pointer_step_t){.reanchored = true};
}
pointer_step_t pointer_motion_update(pointer_motion_t *s, float x, float y, uint16_t time)
{
    if (!isfinite(x) || !isfinite(y) || x < 0 || y < 0 || x > 2302 || y > 2302)
        return (pointer_step_t){0};
    uint16_t elapsed = (uint16_t)(time - s->time);
    if (!s->active || elapsed > 1000U) return anchor(s, x, y, time);
    float dx = x - s->raw_x, dy = y - s->raw_y;
    if (dx * dx + dy * dy > JUMP_DISTANCE * JUMP_DISTANCE) {
        float cx = x - s->candidate_x, cy = y - s->candidate_y;
        if (s->candidate && cx * cx + cy * cy <= CANDIDATE_RADIUS * CANDIDATE_RADIUS)
            return anchor(s, x, y, time);
        s->candidate = true;
        s->candidate_x = x; s->candidate_y = y;
        /* Rejected samples advance neither the trajectory nor its clock. */
        return (pointer_step_t){0};
    }
    s->candidate = false;
    float dt = (elapsed ? elapsed : 10U) * 0.0001f;
    if (dt > 0.05f) dt = 0.05f;
    s->time = time;
    s->raw_x = x; s->raw_y = y;
    float speed = sqrtf(dx * dx + dy * dy) / dt;
    s->speed += alpha(SPEED_CUTOFF, dt) * (speed - s->speed);
    float cutoff = MIN_CUTOFF + SPEED_BETA * s->speed;
    if (cutoff > MAX_CUTOFF) cutoff = MAX_CUTOFF;
    float a = alpha(cutoff, dt);
    float prev_x = s->x, prev_y = s->y;
    s->x += a * (x - s->x); s->y += a * (y - s->y);
    return (pointer_step_t){.dx = s->x - prev_x, .dy = s->y - prev_y, .speed = s->speed};
}
