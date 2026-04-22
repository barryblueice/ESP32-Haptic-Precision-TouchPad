
/**
 * @file cs40l25_sym.h
 *
 * @brief Master table of known firmware symbols for the CS40L25 Driver module
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
 * Command:  ..\..\tools\firmware_converter\firmware_converter.py --command fw_img_v1 --part cs40l25 --sym-input ..\cs40l25_sym.h --sym-output ..\cs40l25_sym.h --generic-sym --wmfw prince_haptics_ctrl_ram_remap_clab_0A0101.wmfw --wmdr default_wt.bin
 *
 *
 */

#ifndef CS40L25_SYM_H
#define CS40L25_SYM_H

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************************************************************
 * LITERALS & CONSTANTS
 **********************************************************************************************************************/

/**
 * @defgroup CS40L25_ALGORITHMS
 * @brief Defines indicating presence of HALO Core Algorithms
 *
 * @{
 */
#define CS40L25_ALGORITHM_FIRMWARE
#define CS40L25_ALGORITHM_VIBEGEN
#define CS40L25_ALGORITHM_CLAB
#define CS40L25_ALGORITHM_DVL
/** @} */

/**
 * @defgroup CS40L25_SYM_
 * @brief Single source of truth for firmware symbols known to the driver.
 *
 * @{
 */
// FIRMWARE
#define CS40L25_SYM_FIRMWARE_HALO_STATE             (0x1)
#define CS40L25_SYM_FIRMWARE_HALO_HEARTBEAT         (0x2)
#define CS40L25_SYM_FIRMWARE_EVENTCONTROL           (0x3)
#define CS40L25_SYM_FIRMWARE_GPIO1EVENT             (0x4)
#define CS40L25_SYM_FIRMWARE_GPIO2EVENT             (0x5)
#define CS40L25_SYM_FIRMWARE_GPIO3EVENT             (0x6)
#define CS40L25_SYM_FIRMWARE_GPIO4EVENT             (0x7)
#define CS40L25_SYM_FIRMWARE_GPIOPLAYBACKEVENT      (0x8)
#define CS40L25_SYM_FIRMWARE_TRIGGERPLAYBACKEVENT   (0x9)
#define CS40L25_SYM_FIRMWARE_RXREADYEVENT           (0xa)
#define CS40L25_SYM_FIRMWARE_ACTIVETOSTANDBYEVENT   (0xb)
#define CS40L25_SYM_FIRMWARE_HARDWAREEVENT          (0xc)
#define CS40L25_SYM_FIRMWARE_INDEXBUTTONPRESS       (0xd)
#define CS40L25_SYM_FIRMWARE_INDEXBUTTONRELEASE     (0xe)
#define CS40L25_SYM_FIRMWARE_GPIO_BUTTONDETECT      (0xf)
#define CS40L25_SYM_FIRMWARE_GAIN_CONTROL           (0x10)
#define CS40L25_SYM_FIRMWARE_GPIO_ENABLE            (0x11)
#define CS40L25_SYM_FIRMWARE_POWERSTATE             (0x12)
#define CS40L25_SYM_FIRMWARE_POWERONSEQUENCE        (0x13)
#define CS40L25_SYM_FIRMWARE_USER_CONTROL_IPDATA    (0x14)
#define CS40L25_SYM_FIRMWARE_F0_STORED              (0x15)
#define CS40L25_SYM_FIRMWARE_REDC_STORED            (0x16)
#define CS40L25_SYM_FIRMWARE_IRQMASKSEQUENCE        (0x17)
#define CS40L25_SYM_FIRMWARE_IRQMASKSEQUENCE_VALID  (0x18)
#define CS40L25_SYM_FIRMWARE_Q_STORED               (0x19)
#define CS40L25_SYM_FIRMWARE_FEATURE_BITMAP         (0x1a)
#define CS40L25_SYM_FIRMWARE_USE_EXT_BOOST          (0x1b)
#define CS40L25_SYM_FIRMWARE_GPI_PLAYBACK_DELAY     (0x1c)
// VIBEGEN
#define CS40L25_SYM_VIBEGEN_TIMEOUT_MS              (0x1d)
#define CS40L25_SYM_VIBEGEN_COMPENSATION_ENABLE     (0x1e)
// CLAB
#define CS40L25_SYM_CLAB_CLAB_ENABLED               (0x1f)
#define CS40L25_SYM_CLAB_PEAK_AMPLITUDE_CONTROL     (0x20)
// DVL
#define CS40L25_SYM_DVL_EN                          (0x101)
#define CS40L25_SYM_DVL_GAINLIMITERCOEF             (0x102)
#define CS40L25_SYM_DVL_LRAOVERMAX                  (0x103)
#define CS40L25_SYM_DVL_LRASAT                      (0x104)
#define CS40L25_SYM_DVL_MAGRELCOEF                  (0x105)
// DYNAMIC_F0
#define CS40L25_SYM_DYNAMIC_F0_DYNAMIC_F0_ENABLED   (0x201)
#define CS40L25_SYM_DYNAMIC_F0_DYN_F0_TABLE         (0x202)
#define CS40L25_SYM_DYNAMIC_F0_DYNAMIC_REDC         (0x203)
/** @} */

/**********************************************************************************************************************/
#ifdef __cplusplus
}
#endif

#endif // CS40L25_SYM_H

