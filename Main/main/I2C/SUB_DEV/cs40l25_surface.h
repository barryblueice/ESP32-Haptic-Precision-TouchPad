#ifndef CS40L25_SURFACE_H
#define CS40L25_SURFACE_H
#include "surface_haptic_runtime.h"

void cs40l25_surface_init(void);
/* Nonblocking task-context APIs; producers never access the bus. */
void cs40l25_surface_button_update(bool down, uint8_t setting);
void cs40l25_surface_cancel_click(void);
void cs40l25_surface_set_modern_sleep(bool sleep_active);
bool cs40l25_surface_is_modern_sleep(void);
bool cs40l25_surface_is_ready(void);
surface_haptic_state_t cs40l25_surface_get_state(void);
#endif
