#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Experimental trained profile; see knuckle_training/README.md. */
#define KNUCKLE_ENABLED 1
enum {
    KNUCKLE_BUFFER_FRAMES = 16,
    /* Protocol/gesture safety limits remain independent of classification. */
    KNUCKLE_MAX_SAMPLES = 64,
    KNUCKLE_DOWN_MIN_MS = 8, KNUCKLE_DOWN_MAX_MS = 120,
    KNUCKLE_SAMPLE_GAP_MS = 60,
    KNUCKLE_GAP_MIN_MS = 60, KNUCKLE_GAP_MAX_MS = 350,
    KNUCKLE_MOVE_MAX = 12, KNUCKLE_PAIR_DISTANCE = 160
};
/* V2 features: peak Z, weighted area, duration ms, first peak ms,
 * maximum adjacent positive dZ/ms, pressure-weighted area standard deviation. */
typedef struct {
    uint64_t area_square_sum;
    uint32_t pressure_sum, area_sum, start_at, last_at, peak_at;
    float max_rise;
    uint8_t peak, previous;
    bool started;
} knuckle_features_t;
typedef struct { float weight[6], bias, low[6], high[6]; } knuckle_candidate_head_t;
typedef struct { knuckle_candidate_head_t onset, complete; } knuckle_classifier_t;
void knuckle_features_add(knuckle_features_t *f, unsigned z, unsigned area, uint32_t now);
void knuckle_features_finish(const knuckle_features_t *f, uint32_t lift_at, float out[6]);
bool knuckle_classifier_accepts(const knuckle_classifier_t *model, bool complete, const float *features);
typedef struct {
    bool active, claimed, rejected, pending, cancelled;
    uint8_t id;
    uint16_t x, y, first_x, first_y;
    uint32_t down_at, last_at, lifted_at;
    uint32_t pressure_sum, weighted_area_sum;
    uint8_t peak_pressure, samples;
    knuckle_features_t features; /* Accumulated for V2 profiles. */
} knuckle_gesture_t;
/* replay returns the previously withheld prefix to ordinary gesture parsing. */
typedef struct { bool suppress, screenshot, replay; } knuckle_result_t;
void knuckle_gesture_reset(knuckle_gesture_t *state);
/* Raw 64-byte Surface packet, before filtering or force-click synthesis.
 * Only a single contact is supported; screenshot fires on the second lift. */
knuckle_result_t knuckle_gesture_update(knuckle_gesture_t *state,
                                      const uint8_t packet[64], uint32_t now);
/* Firmware parser selects the explicitly installed V2 profile. */
const knuckle_classifier_t *knuckle_gesture_active_classifier(void);
/* NULL retains V1 for reference replay and compatibility callers. */
knuckle_result_t knuckle_gesture_update_with_classifier(knuckle_gesture_t *state,
    const uint8_t packet[64], uint32_t now, const knuckle_classifier_t *model);
knuckle_result_t knuckle_gesture_update_buffered(knuckle_gesture_t *state,
    const uint8_t packet[64], uint32_t now, unsigned buffered, const knuckle_classifier_t *model);
bool knuckle_buffer_expired(uint32_t first_at, uint32_t now);
