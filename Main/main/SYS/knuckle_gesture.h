#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Experimental trained profile; see knuckle_training/README.md. */
#define KNUCKLE_ENABLED 1
enum {
    /* Protocol/gesture safety limits remain independent of classification. */
    KNUCKLE_MAX_SAMPLES = 64,
    KNUCKLE_DOWN_MIN_MS = 8, KNUCKLE_DOWN_MAX_MS = 120,
    KNUCKLE_SAMPLE_GAP_MS = 60,
    KNUCKLE_GAP_MIN_MS = 60, KNUCKLE_GAP_MAX_MS = 350,
    KNUCKLE_MOVE_MAX = 60, KNUCKLE_PAIR_DISTANCE = 160
};
typedef struct {
    bool active, claimed, rejected, pending;
    uint8_t id;
    uint16_t x, y, first_x, first_y;
    uint32_t down_at, last_at, lifted_at;
    uint32_t pressure_sum, weighted_area_sum;
    uint8_t peak_pressure, samples;
} knuckle_gesture_t;
typedef struct { bool suppress, screenshot; } knuckle_result_t;
void knuckle_gesture_reset(knuckle_gesture_t *state);
/* Raw 64-byte Surface packet, before filtering or force-click synthesis.
 * Only a single contact is supported; screenshot fires on the second lift. */
knuckle_result_t knuckle_gesture_update(knuckle_gesture_t *state,
                                      const uint8_t packet[64], uint32_t now);
