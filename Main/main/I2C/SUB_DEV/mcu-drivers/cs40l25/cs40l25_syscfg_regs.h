/**
 * @file cs40l25_syscfg_regs.h
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
 * wisce_to_syscfg_reg_converter.py SDK version: 4.29.4 - d93ccd246a11f6feeeb7af99ddc7a1ec1e769338
 * Command:  .\\wisce_script_converter.py -c c_array -p cs40l25 -i wisce_init_l25b_ext_boost.txt
 *
 */

#ifndef CS40L25_SYSCFG_REGS_H
#define CS40L25_SYSCFG_REGS_H

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/
#include "stdint.h"
#ifndef __ZEPHYR__
#include "I2C/SUB_DEV/mcu-drivers/common/regmap.h"
#endif

/***********************************************************************************************************************
 * LITERALS & CONSTANTS
 **********************************************************************************************************************/
#define CS40L25_SYSCFG_REGS_TOTAL (32)

/***********************************************************************************************************************
 * ENUMS, STRUCTS, UNIONS, TYPEDEFS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * GLOBAL VARIABLES
 **********************************************************************************************************************/
extern uint32_t cs40l25_syscfg_regs[];

#ifdef __cplusplus
}
#endif

#endif // CS40L25_SYSCFG_REGS_H

