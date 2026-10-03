#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Parser-owned state, one instance per hardware contact slot. Zero to reset. */
typedef struct {
    uint16_t guarded_x, guarded_y;
    float x, y, speed;
    uint32_t time_ms;
    uint8_t jump_count;
    bool active;
} tp_motion_filter_t;

void tp_motion_filter_update(tp_motion_filter_t *state, uint16_t x, uint16_t y,
                             uint32_t time_ms, uint16_t *out_x, uint16_t *out_y);
