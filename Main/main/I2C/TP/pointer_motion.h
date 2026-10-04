#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Internal trajectory state; no HID or controller wire-format changes. */
typedef struct {
    float x, y, raw_x, raw_y, speed;
    float candidate_x, candidate_y;
    uint16_t time;
    bool active, candidate;
} pointer_motion_t;
typedef struct { float dx, dy, speed; bool reanchored; } pointer_step_t;
pointer_step_t pointer_motion_update(pointer_motion_t *s, float x, float y, uint16_t time);
