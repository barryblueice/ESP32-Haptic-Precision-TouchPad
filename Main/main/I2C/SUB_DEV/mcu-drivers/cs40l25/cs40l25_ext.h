/**
 * @file "I2C/SUB_DEV/mcu-drivers/cs40l25/cs40l25_ext.h"
 *
 * @brief Functions and prototypes exported by the CS40L25 Driver Extended API module
 *
 * @copyright
 * Copyright (c) Cirrus Logic 2020-2021 All Rights Reserved, http://www.cirrus.com/
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

#ifndef CS40L25_EXT_H
#define CS40L25_EXT_H

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/
#include "I2C/SUB_DEV/mcu-drivers/cs40l25/cs40l25.h"

/***********************************************************************************************************************
 * LITERALS & CONSTANTS
 **********************************************************************************************************************/

#define CS40L25_DYNAMIC_F0_TABLE_ENTRY_DEFAULT  (0x007FE000)

/***********************************************************************************************************************
 * MACROS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * ENUMS, STRUCTS, UNIONS, TYPEDEFS
 **********************************************************************************************************************/
typedef union
{
    uint32_t word;

    struct
    {
        uint32_t              : 4;
        uint32_t control_gain : 10;
        uint32_t gpi_gain     : 10;
        uint32_t reserved     : 8;
    };
} fw_ctrl_gain_control_t;

typedef union
{
    uint32_t word;

    struct
    {
        uint32_t gpio_enable : 1;
        uint32_t reserved     : 31;
    };
} fw_ctrl_gpio_enable_t;

/**
 * Configuration of HALO FW Haptic controls
 *
 * @see cs40l25_update_haptic_config
 */
typedef struct
{
    uint32_t index_button_press[4];                     ///< Indeces in wavetable of wave to play upon button press
    uint32_t index_button_release[4];                   ///< Indeces in wavetable of wave to play upon button release
    fw_ctrl_gain_control_t gain_control;                ///< Gain for Control Port and GPIO triggered effects
    fw_ctrl_gpio_enable_t gpio_enable;                  ///< Global enable for triggering via GPIO
} cs40l25_haptic_config_t;

typedef struct
{
    union
    {
        uint32_t word;
        struct
        {
            uint32_t f0                         : 13; ///< F0 in Q10.3 format
            uint32_t index                      : 10; ///< Index in Wave Table
            uint32_t reserved                   : 9;
        };
    };
} cs40l25_dynamic_f0_table_entry_t;

/***********************************************************************************************************************
 * GLOBAL VARIABLES
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/
/**
 * Get the HALO HEARTBEAT
 *
 * Get the current value of the FW control HALO HEARTBEAT.  If running in ROM mode (BHM), the ROM HALO HEARTBEAT will
 * be returned.  If running in RAM mode, the loaded FW's HALO HEARTBEAT will be returned.
 *
 * @param [in] driver           Pointer to the driver state
 * @param [in/out] hb           Pointer to heartbeat count
 *
 * @return
 * - CS40L25_STATUS_FAIL        if get of heartbeat failed, or if hb is NULL
 * - CS40L25_STATUS_OK          otherwise
 *
 */
uint32_t cs40l25_get_halo_heartbeat(cs40l25_t *driver, uint32_t *hb);

/**
 * Update the HALO FW Haptic Configuration
 *
 * Update all the required HALO FW controls to set up for the specific haptic configuration.
 *
 * @param [in] driver           Pointer to the driver state
 * @param [in] config           Pointer to haptic configuration to use for update
 *
 * @return
 * - CS40L25_STATUS_FAIL        if update of any HALO FW control fails, or if config is NULL
 * - CS40L25_STATUS_OK          otherwise
 *
 */
uint32_t cs40l25_update_haptic_config(cs40l25_t *driver, cs40l25_haptic_config_t *config);

/**
 * Trigger the ROM Mode (BHM) Haptic Effect
 *
 * @attention This call will write to the required ROM Mode FW Control whether or not the L25 is currently in either
 * ROM or RAM modes.  If in RAM mode, the user should expect no effect from calls to this function.
 *
 * @param [in] driver           Pointer to the driver state
 *
 * @return
 * - CS40L25_STATUS_FAIL        if update of any HALO FW control fails
 * - CS40L25_STATUS_OK          otherwise
 *
 */
uint32_t cs40l25_trigger_bhm(cs40l25_t *driver);

/**
 * Trigger RAM Mode Haptic Effects
 *
 * @param [in] driver           Pointer to the driver state
 * @param [in] index            Index into the HALO FW Wavetable
 * @param [in] duration_ms      Duration of effect playback in milliseconds
 *
 * @return
 * - CS40L25_STATUS_FAIL        if update of any HALO FW control fails
 * - CS40L25_STATUS_OK          otherwise
 *
 */
uint32_t cs40l25_trigger(cs40l25_t *driver, uint32_t index, uint32_t duration_ms);

/**
 * Enable the HALO FW Click Compensation
 *
 * @param [in] driver           Pointer to the driver state
 * @param [in] f0_enable        true to enable F0 Compensation, false to disable F0 Compensation
 * @param [in] redc_enable      true to enable ReDC Compensation, false to disable ReDC Compensation
 *
 * @return
 * - CS40L25_STATUS_FAIL        if update of any HALO FW control fails
 * - CS40L25_STATUS_OK          otherwise
 *
 */
uint32_t cs40l25_set_click_compensation_enable(cs40l25_t *driver, bool f0_enable, bool redc_enable);

/**
 * Enable the HALO FW CLAB Algorithm
 *
 * @param [in] driver           Pointer to the driver state
 * @param [in] enable           true to enable CLAB, false to disable CLAB
 *
 * @return
 * - CS40L25_STATUS_FAIL        if update of any HALO FW control fails
 * - CS40L25_STATUS_OK          otherwise
 *
 */
uint32_t cs40l25_set_clab_enable(cs40l25_t *driver, bool enable);

/**
 * Set the CLAB Peak Amplitude Control
 *
 * @param [in] driver           Pointer to the driver state
 * @param [in] amplitude        setting for Peak Amplitude Control
 *
 * @return
 * - CS40L25_STATUS_FAIL        if update of any HALO FW control fails
 * - CS40L25_STATUS_OK          otherwise
 *
 */
uint32_t cs40l25_set_clab_peak_amplitude(cs40l25_t *driver, uint32_t amplitude);

/**
 * Enable the HALO FW Dynamic F0 Algorithm
 *
 * @param [in] driver           Pointer to the driver state
 * @param [in] enable           true to enable Dynamic F0, false to disable Dynamic F0
 *
 * @return
 * - CS40L25_STATUS_FAIL        if update of any HALO FW control fails
 * - CS40L25_STATUS_OK          otherwise
 *
 */
uint32_t cs40l25_set_dynamic_f0_enable(cs40l25_t *driver, bool enable);

/**
 * Get the Dynamic F0
 *
 * Get the current value of the F0 for a specific index into the WaveTable.  The index is specified in the 'f0_entry'
 * member 'index'.  The current F0 for WaveTable entries are stored in a Dynamic F0 table in FW, which only contains
 * a Dynamic F0 for WaveTable entries that have been played since power up.  This table has a maximum size of 20. If
 * the index specified is not found in the FW table, the table default CS40L25_DYNAMIC_F0_TABLE_ENTRY_DEFAULT is
 * returned.
 *
 * @param [in] driver           Pointer to the driver state
 * @param [in/out] f0_entry     Pointer to Dynamic F0 structure
 *
 * @return
 * - CS40L25_STATUS_FAIL
 *      - if any call to cs40l25_control fails
 *      - if the specified WaveTable index is >= 20
 * - CS40L25_STATUS_OK          otherwise
 *
 */
uint32_t cs40l25_get_dynamic_f0(cs40l25_t *driver, cs40l25_dynamic_f0_table_entry_t *f0_entry);

/**
 * Get the Dynamic ReDC
 *
 * Get the current value of the Dynamic ReDC for the attached actuator.  If an invalid value is read, the driver will
 * wait 10 milliseconds before reading again.  It will attempt 30 reads of ReDC before failing.
 *
 * @param [in] driver           Pointer to the driver state
 * @param [in/out] redc         Pointer to Dynamic ReDC value
 *
 * @return
 * - CS40L25_STATUS_FAIL
 *      - if any call to cs40l25_control fails
 *      - if the value for ReDC is invalid
 * - CS40L25_STATUS_OK          otherwise
 *
 */
uint32_t cs40l25_get_dynamic_redc(cs40l25_t *driver, uint32_t *redc);

/**********************************************************************************************************************/
#ifdef __cplusplus
}
#endif

#endif // CS40L25_EXT_H
