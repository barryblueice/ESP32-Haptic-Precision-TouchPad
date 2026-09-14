#pragma once
#include "rstp_protocol.h"
#include "hid_msg.h"
typedef struct {
    uint8_t state, mask, id, edge;
    uint16_t start_x, start_y, last;
    int32_t remainder;
    tp_multi_msg_t start;
} edge_gesture_t;
typedef struct { bool suppress, tap; uint8_t action; int steps; tp_multi_msg_t tap_down; } edge_result_t;
void edge_gesture_reset(edge_gesture_t *state);
edge_result_t edge_gesture_update(edge_gesture_t *state, const device_config_t *config,
                                  const tp_multi_msg_t *msg, uint16_t xmax, uint16_t ymax);
