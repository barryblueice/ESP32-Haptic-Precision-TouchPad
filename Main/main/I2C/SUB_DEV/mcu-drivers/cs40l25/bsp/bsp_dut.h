/**
 * @file bsp_dut.h
 *
 * @brief Functions and prototypes exported by the BSP module for the cs40l25 platform.
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
 */

#ifndef BSP_DUT_H
#define BSP_DUT_H

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/
#include "I2C/SUB_DEV/mcu-drivers/common/bsp_driver_if.h"
#include <stdbool.h>
#include <stdint.h>

/***********************************************************************************************************************
 * LITERALS & CONSTANTS
 **********************************************************************************************************************/
#define BSP_DUT_I2C_ADDRESS_8BIT                            (0x80)

#define BSP_DUT_TRIGGER_HAPTIC_POWER_ON                     (0xFF)

/***********************************************************************************************************************
 * MACROS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * ENUMS, STRUCTS, UNIONS, TYPEDEFS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * GLOBAL VARIABLES
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/
uint32_t bsp_dut_initialize(void);
uint32_t bsp_dut_reset(void);
uint32_t bsp_dut_boot(bool cal_boot);
uint32_t bsp_dut_calibrate(void);
uint32_t bsp_dut_power_up(void);
uint32_t bsp_dut_power_down(void);
uint32_t bsp_dut_hibernate(void);
uint32_t bsp_dut_wake(void);
uint32_t bsp_dut_start_i2s(void);
uint32_t bsp_dut_stop_i2s(void);
uint32_t bsp_dut_trigger_haptic(uint8_t waveform, uint32_t duration_ms);
uint32_t bsp_dut_has_processed(bool *has_processed);
uint32_t bsp_dut_update_haptic_config(uint8_t config_index);
uint32_t bsp_dut_apply_haptic_mapping(uint8_t press_waveform,
                                      uint8_t release_waveform,
                                      uint16_t control_gain,
                                      uint16_t gpi_gain,
                                      bool gpio_enable);
uint32_t bsp_dut_enable_haptic_processing(bool enable);
uint32_t bsp_dut_set_click_compensation(bool f0_enable, bool redc_enable);
uint32_t bsp_dut_set_clab_enable(bool enable);
uint32_t bsp_dut_set_clab_peak_amplitude(uint32_t amplitude);
uint32_t bsp_dut_dynamic_calibrate(void);
uint32_t bsp_dut_process(void);
void bsp_dut_dump_diagnostics(void);
void bsp_dut_dump_trigger_diagnostics(uint8_t waveform, uint32_t duration_ms);

uint32_t bsp_dut_enable_vamp(bool is_enabled);
uint32_t bsp_dut_discharge_vamp(void);
uint32_t bsp_dut_trigger_gpio1(uint32_t duration_ms);

/**********************************************************************************************************************/
#ifdef __cplusplus
}
#endif

#endif // BSP_DUT_H
