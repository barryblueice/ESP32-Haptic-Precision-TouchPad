
/**
 * @file cs40l25_cal_sym.h
 *
 * @brief Master table of known firmware symbols for the CS40L25_CAL Driver module
 *
 * @copyright
 * Copyright (c) Cirrus Logic 2020 All Rights Reserved, http://www.cirrus.com/
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
 * firmware_converter.py version: 3.0.0
 * Command:  ..\..\tools\firmware_converter\firmware_converter.py --command fw_img_v1 --part cs40l25 --suffix cal --sym-input ..\cs40l25_cal_sym.h --sym-output ..\cs40l25_cal_sym.h --wmfw prince_haptics_ctrl_ram_remap_calib_0A0101.wmfw
 *
 *
 */

#ifndef CS40L25_CAL_SYM_H
#define CS40L25_CAL_SYM_H

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************************************************************
 * LITERALS & CONSTANTS
 **********************************************************************************************************************/

/**
 * @defgroup CS40L25_CAL_ALGORITHMS
 * @brief Defines indicating presence of HALO Core Algorithms
 *
 * @{
 */
#define CS40L25_CAL_ALGORITHM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_CALIB
#define CS40L25_CAL_ALGORITHM_F0_TRACKING
#define CS40L25_CAL_ALGORITHM_Q_ESTIMATION
/** @} */

/**
 * @defgroup CS40L25_CAL_SYM_
 * @brief Single source of truth for firmware symbols known to the driver.
 *
 * @{
 */
// FIRMWARE
#define CS40L25_CAL_SYM_FIRMWARE_HALO_STATE                                     (0x1)
#define CS40L25_CAL_SYM_FIRMWARE_HALO_HEARTBEAT                                 (0x2)
#define CS40L25_CAL_SYM_FIRMWARE_SHUTDOWNREQUEST                                (0x3)
// F0_TRACKING
#define CS40L25_CAL_SYM_F0_TRACKING_F0                                          (0x4)
#define CS40L25_CAL_SYM_F0_TRACKING_CLOSED_LOOP                                 (0x5)
#define CS40L25_CAL_SYM_F0_TRACKING_REDC                                        (0x6)
#define CS40L25_CAL_SYM_F0_TRACKING_F0_TRACKING_ENABLE                          (0x7)
#define CS40L25_CAL_SYM_F0_TRACKING_MAXBACKEMF                                  (0x8)
// Q_ESTIMATION
#define CS40L25_CAL_SYM_Q_ESTIMATION_Q_EST                                      (0x9)

/** @} */

/**********************************************************************************************************************/
#ifdef __cplusplus
}
#endif

#endif // CS40L25_CAL_SYM_H

