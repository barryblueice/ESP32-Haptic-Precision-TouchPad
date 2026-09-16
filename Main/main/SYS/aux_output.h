#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { uint8_t id, length, data[8]; uint32_t generation, epoch; bool release; } aux_output_report_t;
/* Actions 1..6 are directional edge operations; 13..47 are RSTP function
 * bindings with positive steps and a complete press/release per step.
 * Values 7..12 are reserved here, not the legacy RSTP point bindings. */
bool aux_output_steps(uint8_t action, int steps, uint32_t generation, uint32_t time_ms);
bool aux_output_once(uint8_t action, int steps, uint32_t generation, uint32_t time_ms);
/* Hold one Consumer/keyboard usage until gesture cancellation or reset. */
bool aux_output_hold(uint8_t action, int steps, uint32_t generation, uint32_t time_ms);
/* Enqueue a repeat only when the previous action/release has drained.
 * Busy output skips this repeat successfully, without building a backlog. */
bool aux_output_repeat(uint8_t action, int steps, uint32_t generation, uint32_t time_ms);
void aux_output_cancel_gesture(void);
bool aux_output_take(aux_output_report_t *out, uint32_t generation, uint32_t time_ms);
void aux_output_complete(bool success);
void aux_output_unsubmitted(void);
void aux_output_reset(bool connected);
bool aux_output_active(void);
bool aux_output_release_pending(void);
/* Cancel queued steps without forgetting an accepted transfer or its release. */
void aux_output_cancel(void);
bool aux_output_report_current(const aux_output_report_t *report);
void aux_output_resume(void);
bool aux_output_neutral_pending(void);
/* Radio preserves hold semantics; the receiver releases on cancellation. */
typedef struct { uint8_t action; int16_t steps; uint32_t generation, epoch; bool hold; } aux_output_event_t;
bool aux_output_take_event(aux_output_event_t *event, uint32_t generation, uint32_t now);
void aux_output_event_complete(bool success);
bool aux_output_event_current(const aux_output_event_t *event);
