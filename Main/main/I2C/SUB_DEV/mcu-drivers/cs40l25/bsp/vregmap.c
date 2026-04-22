/**
 * @file vregmap.c
 *
 * @brief Virtual regmap operations implementation.
 *
 * @copyright
 * Copyright (c) Cirrus Logic 2021 All Rights Reserved, http://www.cirrus.com/
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * vregmap_generator.py SDK version: 4.3.0 - internal
 * Command:  ../../tools/vregmap_generator/vregmap_generator.py -c export -i bridge_wisce_device.xml -o ./
 *
 */
/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/
#include <stddef.h>
#include "I2C/SUB_DEV/mcu-drivers/cs40l25/bsp/vregmap.h"

// Add includes for modules needed for vregmap handlers
#include "I2C/SUB_DEV/mcu-drivers/sdk_version.h"
#include "I2C/SUB_DEV/mcu-drivers/common/platform_bsp/platform_bsp.h"

/***********************************************************************************************************************
 * LOCAL LITERAL SUBSTITUTIONS
 **********************************************************************************************************************/
#define VREGMAP_CONTROL_ADDRESS (0x00000000)
#define VREGMAP_CONTROL_DEFAULT (0x00000000)
#define VREGMAP_CONTROL_TOGGLE_LED_MASK (0x00000001)

#define VREGMAP_SDK_VERSION_ADDRESS (0x0000ffff)
#define VREGMAP_SDK_VERSION_DEFAULT ((SDK_VERSION_MAJOR << 24) | (SDK_VERSION_MINOR << 16) | (SDK_VERSION_UPDATE << 8))
#define VREGMAP_SDK_VERSION_MAJOR_MASK (0xff000000)
#define VREGMAP_SDK_VERSION_MINOR_MASK (0x00ff0000)
#define VREGMAP_SDK_VERSION_UPDATE_MASK (0x0000ff00)

/***********************************************************************************************************************
 * MACROS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * LOCAL VARIABLES
 **********************************************************************************************************************/
static uint32_t vregmap_read_0(void *self, uint32_t *val);
static uint32_t vregmap_write_0(void *self, uint32_t val);
static uint32_t vregmap_read_1(void *self, uint32_t *val);

/***********************************************************************************************************************
 * GLOBAL VARIABLES
 **********************************************************************************************************************/
const regmap_cp_config_t vregmap_cp =
{
    .dev_id = (uint32_t) vregmap,
    .bus_type = REGMAP_BUS_TYPE_VIRTUAL,
    .receive_max = (uint16_t) VREGMAP_LENGTH_REGS,
};

regmap_virtual_register_t vregmap[] =
{
    {
        .address = VREGMAP_CONTROL_ADDRESS,
        .default_value = VREGMAP_CONTROL_DEFAULT,
        .value = VREGMAP_CONTROL_DEFAULT,
        .on_read = vregmap_read_0,
        .on_write = vregmap_write_0,
    },
    {
        .address = VREGMAP_SDK_VERSION_ADDRESS,
        .default_value = VREGMAP_SDK_VERSION_DEFAULT,
        .value = VREGMAP_SDK_VERSION_DEFAULT,
        .on_read = vregmap_read_1,
        .on_write = NULL,
    },
};

/***********************************************************************************************************************
 * LOCAL FUNCTIONS
 **********************************************************************************************************************/
static uint32_t vregmap_read_0(void *self, uint32_t *val)
{
    regmap_virtual_register_t *s = (regmap_virtual_register_t *) self;
    uint32_t temp_value = 0;

    // Read CONTROL.TOGGLE_LED bitfield
    temp_value &= ~VREGMAP_CONTROL_TOGGLE_LED_MASK;
    temp_value |= (s->value & VREGMAP_CONTROL_TOGGLE_LED_MASK);

    *val = s->value;

    return BSP_STATUS_OK;
}

static uint32_t vregmap_write_0(void *self, uint32_t val)
{
    regmap_virtual_register_t *s = (regmap_virtual_register_t *) self;
    uint32_t changed_bits = s->value ^ val;

    s->value = val;

    // Write CONTROL.TOGGLE_LED bitfield
    if (changed_bits & VREGMAP_CONTROL_TOGGLE_LED_MASK)
    {
        uint32_t temp_buffer;
        if (s->value & VREGMAP_CONTROL_TOGGLE_LED_MASK)
        {
            temp_buffer = __builtin_bswap32(0x00B900FF);
        }
        else
        {
            temp_buffer = __builtin_bswap32(0x00B900FE);
        }
        bsp_i2c_write(BSP_LN2_DEV_ID, (uint8_t *)&temp_buffer, 4, NULL, NULL);
    }

    return BSP_STATUS_OK;
}

static uint32_t vregmap_read_1(void *self, uint32_t *val)
{
    regmap_virtual_register_t *s = (regmap_virtual_register_t *) self;
    uint32_t temp_value = 0;

    // Read SDK_VERSION.MAJOR bitfield
    temp_value &= ~VREGMAP_SDK_VERSION_MAJOR_MASK;
    temp_value |= (s->value & VREGMAP_SDK_VERSION_MAJOR_MASK);

    // Read SDK_VERSION.MINOR bitfield
    temp_value &= ~VREGMAP_SDK_VERSION_MINOR_MASK;
    temp_value |= (s->value & VREGMAP_SDK_VERSION_MINOR_MASK);

    // Read SDK_VERSION.UPDATE bitfield
    temp_value &= ~VREGMAP_SDK_VERSION_UPDATE_MASK;
    temp_value |= (s->value & VREGMAP_SDK_VERSION_UPDATE_MASK);

    *val = s->value;

    return BSP_STATUS_OK;
}

