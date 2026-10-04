#pragma once
#include "sdkconfig.h"
#include "driver/i2c_master.h"
#include "SYS/input_pipeline.h"

#if CONFIG_SURFACE_FORCE_FORWARD_ENABLE
/* Register once, before starting the reader. Failure disables only forwarding. */
void force_forward_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t touchpad);
/* Task/timer context, including critical sections: bookkeeping only, no I2C. */
void force_forward_invalidate(void);
/* Touch reader task only; call once per NEW raw frame, never for gesture replays. */
void force_forward_report(const input_frame_t *frame);
#else
static inline void force_forward_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t touchpad)
{ (void)bus; (void)touchpad; }
static inline void force_forward_invalidate(void) { }
static inline void force_forward_report(const input_frame_t *frame) { (void)frame; }
#endif
