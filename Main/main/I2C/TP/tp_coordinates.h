#pragma once
#include <stdint.h>
#include "sdkconfig.h"
#include "SYS/device_config.h"

static inline void tp_rotate_coordinates(uint16_t raw_x, uint16_t raw_y, uint16_t *x, uint16_t *y)
{
    raw_x = raw_x > 2302U ? 2302U : raw_x;
    raw_y = raw_y > 1532U ? 1532U : raw_y;
    switch (device_config_rotation()) {
    case 1: *x = 1532U - raw_y; *y = 2302U - raw_x; break;
    case 2: *x = 2302U - raw_x; *y = raw_y; break;
    case 3: *x = raw_y; *y = raw_x; break;
    default: *x = raw_x; *y = 1532U - raw_y; break;
    }
}
