
/**
 * @file cs40l25_sym.h
 *
 * @brief Master table of known firmware symbols for the CS40L25 Driver module
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
 * firmware_converter.py SDK version: 4.29.4 - d93ccd246a11f6feeeb7af99ddc7a1ec1e769338
 * Command:  D:\Documents\test\mcu-drivers\tools\firmware_converter\firmware_converter.py fw_img_v2 cs40l25 D:\Documents\test\mcu-drivers\cs40l25\fw\prince_haptics_ctrl_ram_remap_ext_boost_0A0603.wmfw --wmdr D:\Documents\test\mcu-drivers\cs40l25\fw\dvl.bin --output-directory D:\Documents\test\esp32-haptic-precision-touchpad\Main\_fw_screen\ext_boost_0A0603
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
#define CS40L25_ALGORITHM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST
#define CS40L25_ALGORITHM_VIBEGEN
#define CS40L25_ALGORITHM_DYNAMIC_F0
/** @} */

/**
 * @defgroup CS40L25_SYM_
 * @brief Single source of truth for firmware symbols known to the driver.
 *
 * @{
 */
// FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_SYSTEM_CONFIG_XM_STRUCT_T   (0x1)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_HALO_STATE                  (0x2)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_HALO_HEARTBEAT              (0x3)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_STATEMACHINE                (0x4)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_BUTTONTOUCHCOUNT            (0x5)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_BUTTONRELEASECOUNT          (0x6)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_RXIN                        (0x7)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_BUTTONDETECT                (0x8)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_RXBUFFER                    (0x9)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_RXACK                       (0xa)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_EVENTCONTROL                (0xb)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_GPIO1EVENT                  (0xc)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_GPIO2EVENT                  (0xd)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_GPIO3EVENT                  (0xe)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_GPIO4EVENT                  (0xf)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_GPIOPLAYBACKEVENT           (0x10)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_TRIGGERPLAYBACKEVENT        (0x11)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_RXREADYEVENT                (0x12)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_ACTIVETOSTANDBYEVENT        (0x13)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_HARDWAREEVENT               (0x14)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_INDEXBUTTONPRESS            (0x15)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_INDEXBUTTONRELEASE          (0x16)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_ENDPLAYBACK                 (0x17)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_AUDIO_BLK_SIZE              (0x18)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_BUILD_JOB_NAME              (0x19)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_BUILD_JOB_NUMBER            (0x1a)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_GPIO_BUTTONDETECT           (0x1b)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_EVENT_TIMEOUT               (0x1c)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_PRESS_RELEASE_TIMEOUT       (0x1d)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_GAIN_CONTROL                (0x1e)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_GPIO_ENABLE                 (0x1f)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_POWERSTATE                  (0x20)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_FALSEI2CTIMEOUT             (0x21)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_POWERONSEQUENCE             (0x22)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_VMONMAX                     (0x23)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_VMONMIN                     (0x24)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_IMONMAX                     (0x25)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_IMONMIN                     (0x26)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_USER_CONTROL_IPDATA         (0x27)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_USER_CONTROL_RESPONSE       (0x28)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_I2S_ENABLED                 (0x29)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_F0_STORED                   (0x2a)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_REDC_STORED                 (0x2b)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_F0_OFFSET                   (0x2c)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_IRQMASKSEQUENCE             (0x2d)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_IRQMASKSEQUENCE_VALID       (0x2e)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_Q_STORED                    (0x2f)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_VPMONMAX                    (0x30)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_VPMONMIN                    (0x31)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_VMON_IMON_OFFSET_ENABLE     (0x32)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_IMON_VMON_OFFSET_DELAY      (0x33)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_GPIO_GAIN                   (0x34)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_SPK_FORCE_TST_1_AUTO        (0x35)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_GPIO_POL                    (0x36)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_FORCEWATCHDOG               (0x37)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_FEATURE_BITMAP              (0x38)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_MAXBACKEMF                  (0x39)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_USE_EXT_BOOST               (0x3a)
#define CS40L25_SYM_FIRMWARE_PRINCE_HAPCTRL_RAM_REMAP_EXT_BOOST_GPI_PLAYBACK_DELAY          (0x3b)
// VIBEGEN
#define CS40L25_SYM_VIBEGEN_VIBEGEN_XM_STRUCT_T                                             (0x3c)
#define CS40L25_SYM_VIBEGEN_ENABLE                                                          (0x3d)
#define CS40L25_SYM_VIBEGEN_STATUS                                                          (0x3e)
#define CS40L25_SYM_VIBEGEN_TIMEOUT_MS                                                      (0x3f)
#define CS40L25_SYM_VIBEGEN_NUMBEROFWAVES                                                   (0x40)
#define CS40L25_SYM_VIBEGEN_COMPENSATION_ENABLE                                             (0x41)
#define CS40L25_SYM_VIBEGEN_WAVETABLE                                                       (0x42)
#define CS40L25_SYM_VIBEGEN_VIBEGEN_YM_STRUCT_T                                             (0x43)
#define CS40L25_SYM_VIBEGEN_WAVETABLEYM                                                     (0x44)
// DYNAMIC_F0
#define CS40L25_SYM_DYNAMIC_F0_DYNAMIC_F0_XM_STRUCT_T                                       (0x45)
#define CS40L25_SYM_DYNAMIC_F0_DYNAMIC_F0_ENABLED                                           (0x46)
#define CS40L25_SYM_DYNAMIC_F0_IMONRINGPPTHRESHOLD                                          (0x47)
#define CS40L25_SYM_DYNAMIC_F0_FRME_SKIP                                                    (0x48)
#define CS40L25_SYM_DYNAMIC_F0_NUM_PEAKS_TOFIND                                             (0x49)
#define CS40L25_SYM_DYNAMIC_F0_DYN_F0_TABLE                                                 (0x4a)
#define CS40L25_SYM_DYNAMIC_F0_DYNAMIC_REDC                                                 (0x4b)

/** @} */

/**********************************************************************************************************************/
#ifdef __cplusplus
}
#endif

#endif // CS40L25_SYM_H

