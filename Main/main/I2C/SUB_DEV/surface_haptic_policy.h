#ifndef SURFACE_HAPTIC_POLICY_H
#define SURFACE_HAPTIC_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool enabled;
    uint8_t press_index;
    uint8_t release_index;
} surface_haptic_pair_t;

/* Resolve the SAM setting (0..100). Invalid input leaves *out unchanged.
 * Setting zero retains the observed 100/100 pair, but disables host playback.
 * Reference: tools/surface_fw/policy_reference/ (SAM routine 0xD859A).
 */
bool surface_haptic_resolve(uint8_t setting, surface_haptic_pair_t *out);

/* Synchronous: call only after BSP/firmware initialization, from the single
 * task owning CS40L25. Returns BSP_STATUS_*; disabled pairs perform no I/O.
 * Each event restores both indices, zero attenuation and disabled GPIO triggers,
 * then uses MBOX1 index playback (duration argument 0, not a zero-length effect).
 */
uint32_t surface_haptic_play_event(const surface_haptic_pair_t *pair, bool release);

#ifdef __cplusplus
}
#endif

#endif
