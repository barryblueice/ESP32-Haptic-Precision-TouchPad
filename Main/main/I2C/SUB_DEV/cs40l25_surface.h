#ifndef CS40L25_SURFACE_H
#define CS40L25_SURFACE_H

#include <stdbool.h>
#include <stdint.h>

void cs40l25_surface_init(void);
void cs40l25_surface_trigger_click(void);
void cs40l25_surface_trigger_manual(uint8_t waveform,
                                    uint8_t intensity,
                                    uint8_t repeat_count,
                                    uint16_t retrigger_period_ms,
                                    uint16_t cutoff_time_ms);
void cs40l25_surface_trigger_scaled(uint8_t waveform,
                                    uint16_t cp_dig_scale,
                                    uint16_t duration_ms);
bool cs40l25_surface_is_ready(void);

#endif
