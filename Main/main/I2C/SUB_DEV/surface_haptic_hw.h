#ifndef SURFACE_HAPTIC_HW_H
#define SURFACE_HAPTIC_HW_H
#include <stdbool.h>
#include <stdint.h>

/* Caller owns CS40L25 and has registered both I2C devices + GPIO ISR service. */
bool surface_haptic_hw_initialize(void);
bool surface_haptic_hw_wake(void);
/* Only a lone boost undervoltage is recoverable; all other faults stay latched. */
bool surface_haptic_hw_recover_undervoltage(void);
uint32_t surface_haptic_hw_process(void);
bool surface_haptic_hw_power_off(void);
void surface_haptic_hw_diagnostics(uint8_t waveform);

#endif
