#pragma once
#include <stdint.h>
#include "sdkconfig.h"

static inline void tp_rotate_coordinates(uint16_t raw_x, uint16_t raw_y, uint16_t *x, uint16_t *y)
{
    raw_x = raw_x > 2302U ? 2302U : raw_x;
    raw_y = raw_y > 1532U ? 1532U : raw_y;
#if CONFIG_TP_ROTATION_LANDSCAPE
    *x = raw_x; *y = 1532U - raw_y;
#elif CONFIG_TP_ROTATION_LANDSCAPE_FLIPPED
    *x = 2302U - raw_x; *y = raw_y;
#elif CONFIG_TP_ROTATION_PORTRAIT
    *x = 1532U - raw_y; *y = 2302U - raw_x;
#elif CONFIG_TP_ROTATION_PORTRAIT_FLIPPED
    *x = raw_y; *y = raw_x;
#endif
}
