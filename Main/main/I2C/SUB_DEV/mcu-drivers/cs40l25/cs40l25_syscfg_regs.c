/**
 * @file cs40l25_syscfg_regs.c
 *
 * @brief Register values to be applied after CS40L25 Driver boot().
 *
 * @copyright
 * Copyright (c) Cirrus Logic 2026 All Rights Reserved, http://www.cirrus.com/
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
 */
/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/
#include "I2C/SUB_DEV/mcu-drivers/cs40l25/cs40l25_syscfg_regs.h"
#include "I2C/SUB_DEV/mcu-drivers/cs40l25/cs40l25_spec.h"

/***********************************************************************************************************************
 * GLOBAL VARIABLES
 **********************************************************************************************************************/

uint32_t cs40l25_syscfg_regs[] =
{
    0x6000, 0x81f0,
    0x4c40, 0x0008,
    0x4c44, 0x0018,
    0x4c48, 0x0019,
    0x4c4c, 0x0028,
    0x4c00, 0x0032,
    0x242c, 0x1010000,
    0x2c04, 0x0010,
    0x2018, 0x3301,
    0x201c, 0x1000010,
    0x4808, 0x20180200,
    0x4820, 0x0100,
    0x4840, 0x0018,
    0x2d10, 0x2b01b,
    0x4804, 0x0021,
    0x2904, 0x0098
};



