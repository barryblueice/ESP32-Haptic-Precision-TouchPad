#pragma once
#include "edge_gesture.h"

enum { POINT_WHEEL_HOLD_DELAY_MS = 600, POINT_WHEEL_REPEAT_MS = 200 };
typedef struct {
    uint8_t state, id, point, action, step;
    bool repeat, convert, owned;
    uint16_t start_x, start_y;
    uint32_t repeat_at;
} point_gesture_t;
typedef struct { bool suppress, cancel, handoff, initial, hold; uint8_t action; int steps; } point_result_t;
void point_gesture_reset(point_gesture_t *state);
bool point_gesture_inside(unsigned point, unsigned radius, uint16_t x, uint16_t y,
                          uint16_t xmax, uint16_t ymax, uint16_t width, uint16_t height);
point_result_t point_gesture_update(point_gesture_t *state, const device_config_t *config,
    const tp_multi_msg_t *msg, uint16_t xmax, uint16_t ymax, uint16_t width, uint16_t height,
    uint32_t now);
point_result_t point_gesture_tick(point_gesture_t *state, uint32_t now);
bool point_gesture_repeating(const point_gesture_t *state);
