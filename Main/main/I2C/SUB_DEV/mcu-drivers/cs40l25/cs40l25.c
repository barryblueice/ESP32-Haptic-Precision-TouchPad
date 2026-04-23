/**
 * @file cs40l25.c
 *
 * @brief The CS40L25 Driver module
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
/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/
#include <stddef.h>
#include "I2C/SUB_DEV/mcu-drivers/cs40l25/cs40l25.h"
#include "I2C/SUB_DEV/mcu-drivers/common/bsp_driver_if.h"
#include "string.h"

/***********************************************************************************************************************
 * LOCAL LITERAL SUBSTITUTIONS
 **********************************************************************************************************************/

/**
 * Default Interrupt Mask for IRQ2_MASK_1 register
 *
 * The interrupts that are unmasked in Interrupt Status and Mask Control (IRQ2) are:
 * - b31 - AMP_ERR_MASK1
 * - b17 - TEMP_ERR_MASK1
 * - b15 - TEMP_WARN_RISE_MASK1
 * - b8  - BST_SHORT_ERR_MASK1
 * - b7  - BST_DCM_UVP_ERR_MASK1
 * - b6  - BST_OVP_ERR_MASK1
 *
 * @see IRQ2_IRQ2_MASK_1_REG
 *
 */
#define CS40L25_INT2_MASK_DEFAULT               (0x7FFD7E3F)
#define CS40L25_IRQ2_MASK1_DEFAULT              (CS40L25_INT2_MASK_DEFAULT)
#define CS40L25_IRQ2_MASK2_DEFAULT              (0xFFFFFFFF)
#define CS40L25_IRQ2_MASK3_DEFAULT              (0xFFFF87FF)
#define CS40L25_IRQ2_MASK4_DEFAULT              (0xFEFFFFFF)

/**
 * Event Flag Mask for IRQ2 Status Bits for Actuator-Safe Mode Boost-related Events
 *
 * If any of the bits in the mask below are set in IRQ2_EINT_1, the amplifier will have entered Actuator-Safe Mode
 * and will require additional steps to release from Actuator-Safe Mode.
 * - b8 - BST_SHORT_ERR_MASK1
 * - b7 - BST_DCM_UVP_ERR_MASK1
 * - b6 - BST_OVP_ERR_MASK1
 *
 * These bits correspond to the following flags in Event Flags:
 * - BST_SHORT_ERR_MASK1 - CS40L25_EVENT_FLAG_BOOST_INDUCTOR_SHORT
 * - BST_DCM_UVP_ERR_MASK1 - CS40L25_EVENT_FLAG_BOOST_UNDERVOLTAGE
 * - BST_OVP_ERR_MASK1 - CS40L25_EVENT_FLAG_BOOST_OVERVOLTAGE
 *
 * @see IRQ2_EINT_1
 * @see Datasheet Section 4.5.2
 *
 */
#define CS40L25_EVENT_FLAGS_BOOST_CYCLE         (CS40L25_EVENT_FLAG_BOOST_INDUCTOR_SHORT | \
                                                 CS40L25_EVENT_FLAG_BOOST_UNDERVOLTAGE | \
                                                 CS40L25_EVENT_FLAG_BOOST_OVERVOLTAGE)

/**
 * Toggle Mask for MSM_ERROR_RELEASE_REG to Release from Actuator-Safe Mode
 *
 * The relevant fields in MSM_ERROR_RELEASE_REG that require release sequence are:
 * - b6 - TEMP_ERR
 * - b5 - TEMP_WARN
 * - b4 - BST_UVP
 * - b3 - BST_OVP
 * - b2 - BST_SHORT
 * - b1 - AMP_SHORT
 *
 * @see MSM_ERROR_RELEASE_REG
 * @see Datasheet Section 4.5.2
 *
 */
#define CS40L25_ERR_RLS_ACTUATOR_SAFE_MODE_MASK (0x0000007E)

/**
 * @defgroup CS40L25_POWERSTATE_
 * @brief Valid values to read from DSP firmware control POWERSTATE
 *
 * @{
 */
#define CS40L25_POWERSTATE_BLANK                (0)
#define CS40L25_POWERSTATE_ACTIVE               (1)
#define CS40L25_POWERSTATE_STANDBY              (2)
#define CS40L25_POWERSTATE_HIBERNATE            (3)
/** @} */

/**
 * @defgroup CS40L2X_EVENT_
 * @brief Bitmasks for the EVENTCONTROL DSP firmware control
 *
 * @{
 */
#define CS40L2X_EVENT_DISABLED                  (0x000000)
#define CS40L2X_EVENT_GPIO1_ENABLED             (0x000001)
#define CS40L2X_EVENT_GPIO2_ENABLED             (0x000002)
#define CS40L2X_EVENT_GPIO3_ENABLED             (0x000004)
#define CS40L2X_EVENT_GPIO4_ENABLED             (0x000008)
#define CS40L2X_EVENT_START_ENABLED             (0x000010)
#define CS40L2X_EVENT_END_ENABLED               (0x000020)
#define CS40L2X_EVENT_READY_ENABLED             (0x000040)
#define CS40L2X_EVENT_ACTIVETOSTANDBY_ENABLED   (0x000080)
#define CS40L2X_EVENT_HARDWARE_ENABLED          (0x800000)
/** @} */

/**
 * @defgroup CS40L2X_EVENT_CTRL_
 * @brief Possible values for the *EVENT DSP firmware controls
 *
 * @{
 */
#define CS40L2X_EVENT_CTRL_GPIO1_FALL           (0)
#define CS40L2X_EVENT_CTRL_GPIO1_RISE           (1)
#define CS40L2X_EVENT_CTRL_GPIO2_FALL           (2)
#define CS40L2X_EVENT_CTRL_GPIO2_RISE           (3)
#define CS40L2X_EVENT_CTRL_GPIO3_FALL           (4)
#define CS40L2X_EVENT_CTRL_GPIO3_RISE           (5)
#define CS40L2X_EVENT_CTRL_GPIO4_FALL           (6)
#define CS40L2X_EVENT_CTRL_GPIO4_RISE           (7)
#define CS40L2X_EVENT_CTRL_TRIG_STOP            (10)
#define CS40L2X_EVENT_CTRL_GPIO_STOP            (11)
#define CS40L2X_EVENT_CTRL_READY                (12)
#define CS40L2X_EVENT_CTRL_HARDWARE             (13)
#define CS40L2X_EVENT_CTRL_TRIG_SUSP            (14)
#define CS40L2X_EVENT_CTRL_TRIG_RESM            (15)
#define CS40L2X_EVENT_CTRL_ACTIVETOSTANDBY      (16)
#define CS40L2X_EVENT_CTRL_NONE                 (0xFFFFFF)

#define CS40L2X_EVENT_CTRL_MAX                  (CS40L2X_EVENT_CTRL_ACTIVETOSTANDBY)
/** @} */

#define CS40L25_EVENT_SOURCES                   (9) ///< Total number of *EVENT DSP firmware controls to poll
#define CS40L25_EVENT_HW_SOURCES                (6) ///< Total number of HW registers to read for Hardware events

/**
 * @defgroup CS40L25_IMASKSEQ_WORD_
 * @brief Macros for conversion of IRQ Masks for the IRQ Mask Sequencer (IMASKSEQ)
 *
 * @{
 */
#define CS40L25_IMASKSEQ_WORD_1(B)              (((B & 0x000000FF) << 16))
#define CS40L25_IMASKSEQ_WORD_2(B)              ((B & 0xFFFFFF00) >> 8)
/** @} */

/**
 * @defgroup CS40L25_DSP_MBOX_STATUS_
 * @brief Statuses of the HALO Core DSP Mailbox
 *
 * @{
 */
#define CS40L25_DSP_MBOX_STATUS_RUNNING         (0)
#define CS40L25_DSP_MBOX_STATUS_PAUSED          (1)
#define CS40L25_DSP_MBOX_STATUS_RDY_FOR_REINIT  (2)
/** @} */

/**
 * @defgroup CS40L25_DSP_MBOX_CMD_
 * @brief HALO Core DSP Mailbox commands
 *
 * @see cs40l25_t member mbox_cmd
 *
 * @{
 */
#define CS40L25_DSP_MBOX_CMD_NONE               (0)
#define CS40L25_DSP_MBOX_CMD_PAUSE              (1)
#define CS40L25_DSP_MBOX_CMD_RESUME             (2)
#define CS40L25_DSP_MBOX_CMD_REINIT             (3)
#define CS40L25_DSP_MBOX_CMD_STOP_PRE_REINIT    (4)
#define CS40L25_DSP_MBOX_CMD_UNKNOWN            (-1)
/** @} */

/**
 * @defgroup CS40L25_FEATURE_BITMAP_
 * @brief HALO Core DSP Firmware Feature Bitmap values
 *
 * @{
 */
#define CS40L25_FEATURE_BITMAP_I2s              (0x1)
#define CS40L25_FEATURE_BITMAP_GPI              (0x2)
#define CS40L25_FEATURE_BITMAP_EV_PRI           (0x4)
#define CS40L25_FEATURE_BITMAP_REDC             (0x8)
/** @} */

#define CS40L25_CAL_STATUS_CALIB_SUCCESS        (0x1)           ///< Value of CS40L25_CAL_STATUS for Calibration success
#define CS40L25_FIRMWARE_ID_ADDR                (0x0280000C)    ///< Register address for Firmware ID
#define CS40L25_FIRMWARE_REVISION               (0x2800010)     ///< Register address for Firmware Revision
#define CS40L25_FWID_CAL                        (0x1400C6)      ///< Firmware ID for Calibration Firmware

/***********************************************************************************************************************
 * LOCAL VARIABLES
 **********************************************************************************************************************/

/**
 * CS40L25 RevB0 Register Patch Errata
 *
 * The array is in the form:
 * - word0 - Length of rest of patch in words (i.e. NOT including this word)
 * - word1 - 1st register address to patch
 * - word2 - 1st register value
 * - word3 - 2nd register address to patch
 * - word4 - 2nd register value
 * - ...
 *
 * @note To simplify cs40l25_reset, this includes the configuration for IRQ1 and INTb GPIO
 *
 */
static const uint32_t cs40l25_revb0_errata_patch[] =
{
    0x00003008, 0x000C1837,
    0x00003014, 0x03008E0E,
    CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG, CS40L25_TEST_KEY_CTRL_UNLOCK_1,
    CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG, CS40L25_TEST_KEY_CTRL_UNLOCK_2,
    0x0000391C, 0x004DC080,
    0x00004170, 0x002F0065,
    0x00004360, 0x00002B4F,
    0x00004100, 0x00000000,
    0x00004310, 0x00000000,
    CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG, CS40L25_TEST_KEY_CTRL_LOCK_1,
    CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG, CS40L25_TEST_KEY_CTRL_LOCK_2,
    0x00004400, 0x00000000,
};

/**
 * Register configuration to send just before the CS40L25 is powered up in cs40l25_power_up
 *
 * List is in the form:
 * - word1 - Address of first configuration register
 * - word2 - Value of first configuration register
 * - word3 - Address of second configuration register
 * - word4 - Value of second configuration register
 * - ...
 *
 */
static const uint32_t cs40l25_pup_patch[] =
{
    0x02BC2020, 0x00000000,
    0x02BC20E0, 0x00000000,
    0x00002900, 0x00000002,
};

/**
 * Register configuration to send during BHM disable
 *
 * List is in the form:
 * - word1 - Address of first configuration register
 * - word2 - Value of first configuration register
 * - word3 - Address of second configuration register
 * - word4 - Value of second configuration register
 * - ...
 *
 */
static const uint32_t cs40l25_bhm_revert_patch[] =
{
    0x00002014, 0x00000000,
    0x00002018, 0x00003321,
    0x00002418, 0x00000007,
    0x00002420, 0x00000007,
    0x00006000, 0x00008000,
    0x00010910, 0xFFFFFFFF,
    0x00010914, 0xFFFFFFFF,
};

/**
 * Register configuration to send during BHM disable
 *
 * List is in the form:
 * - word1 - Address of first configuration register
 * - word2 - Value of first configuration register
 * - word3 - Address of second configuration register
 * - word4 - Value of second configuration register
 * - ...
 *
 */
static const uint32_t cs40l25_ext_boost_bhm_revert_patch[] =
{
    0x00002014, 0x00000000,
    0x00002018, 0x00003301,
    0x00002418, 0x00000007,
    0x00002420, 0x00000007,
    0x00006000, 0x00008000,
    0x00010910, 0xFFFFFFFF,
    0x00010914, 0xFFFFFFFF,
};

/**
 * Register addresses to set all HALO Core DSP sample rates to the same value.
 *
 * Sent just before the CS40L25 is powered up in cs40l25_power_up.  All register values will be set to
 * CS40L25_DSP1_SAMPLE_RATE_G1R2.
 *
 * List is in the form:
 * - word0 - Address of first configuration register
 * - word1 - Address of second configuration register
 * - ...
 *
 * @see CS40L25_DSP1_SAMPLE_RATE_G1R2
 *
 */
static const uint32_t cs40l25_frame_sync_regs[] =
{
    XM_UNPACKED24_DSP1_SAMPLE_RATE_RX1_REG,
    XM_UNPACKED24_DSP1_SAMPLE_RATE_RX2_REG,
    XM_UNPACKED24_DSP1_SAMPLE_RATE_RX3_REG,
    XM_UNPACKED24_DSP1_SAMPLE_RATE_RX4_REG,
    XM_UNPACKED24_DSP1_SAMPLE_RATE_RX5_REG,
    XM_UNPACKED24_DSP1_SAMPLE_RATE_RX6_REG,
    XM_UNPACKED24_DSP1_SAMPLE_RATE_RX7_REG,
    XM_UNPACKED24_DSP1_SAMPLE_RATE_RX8_REG,
    XM_UNPACKED24_DSP1_SAMPLE_RATE_TX1_REG,
    XM_UNPACKED24_DSP1_SAMPLE_RATE_TX2_REG,
    XM_UNPACKED24_DSP1_SAMPLE_RATE_TX3_REG,
    XM_UNPACKED24_DSP1_SAMPLE_RATE_TX4_REG,
    XM_UNPACKED24_DSP1_SAMPLE_RATE_TX5_REG,
    XM_UNPACKED24_DSP1_SAMPLE_RATE_TX6_REG,
    XM_UNPACKED24_DSP1_SAMPLE_RATE_TX7_REG,
    XM_UNPACKED24_DSP1_SAMPLE_RATE_TX8_REG
};

/**
 * Set of registers and modified values to add to WSEQ by the time of cs40l25_boot
 *
 * List is in the form:
 * - word0 - Address of first wseq register
 * - word1 - Value for first wseq register
 * - ...
 *
 */
static const uint32_t cs40l25_wseq_regs[] =
{
    BOOST_VBST_CTL_1_REG, 0x00000000,
    BOOST_VBST_CTL_2_REG, 0x00000001,
    BOOST_BST_IPK_CTL_REG, 0x0000004A,
    BOOST_BST_LOOP_COEFF_REG, 0x00002424,
    BOOST_LBST_SLOPE_REG, 0x00007500,
    CS40L25_INTP_AMP_CTRL_REG, 0x00008000,
    CS40L25_WAKESRC_CTL_REG, 0x00000008,
    CS40L25_GPIO_PAD_CONTROL_REG, 0x03010000,
    CCM_REFCLK_INPUT_REG, 0x00000010,
    0x00003018, 0x00000000,
    0x00002D20, 0x00000000,
    DATAIF_ASP_ENABLES1_REG, 0x00000000,
    DATAIF_ASP_CONTROL1_REG, 0x00000028,
    CCM_FS_MON_0_REG, 0x00000000,
    DATAIF_ASP_CONTROL2_REG, 0x18180200,
    DATAIF_ASP_FRAME_CONTROL5_REG, 0x00000100,
    DATAIF_ASP_FRAME_CONTROL1_REG, 0x03020100,
    DATAIF_ASP_DATA_CONTROL5_REG, 0x00000018,
    DATAIF_ASP_DATA_CONTROL1_REG, 0x00000018,
    MSM_BLOCK_ENABLES2_REG, 0x10000010,
    CS40L25_MIXER_DACPCM1_INPUT_REG, CS40L25_INPUT_SRC_DSP1TX1,
    CS40L25_MIXER_DSP1RX1_INPUT_REG, CS40L25_INPUT_SRC_ASPRX1,
    CS40L25_MIXER_DSP1RX2_INPUT_REG, CS40L25_INPUT_SRC_VMON,
    CS40L25_MIXER_DSP1RX3_INPUT_REG, CS40L25_INPUT_SRC_VMON,
    CS40L25_MIXER_DSP1RX4_INPUT_REG, CS40L25_INPUT_SRC_VMON,
};

/**
 * DSP firmware controls to read for Event Control sources
 *
 * @see cs40l25_event_handler
 * @see CS40L2X_EVENT_CTRL_
 *
 * @attention CS40L25_SYM_FIRMWARE_HARDWAREEVENT must come first in the list
 *
 */
static const uint32_t cs40l2x_event_controls[CS40L25_EVENT_SOURCES] =
{
    CS40L25_SYM_FIRMWARE_HARDWAREEVENT,
    CS40L25_SYM_FIRMWARE_GPIO1EVENT,
    CS40L25_SYM_FIRMWARE_GPIO2EVENT,
    CS40L25_SYM_FIRMWARE_GPIO3EVENT,
    CS40L25_SYM_FIRMWARE_GPIO4EVENT,
    CS40L25_SYM_FIRMWARE_GPIOPLAYBACKEVENT,
    CS40L25_SYM_FIRMWARE_TRIGGERPLAYBACKEVENT,
    CS40L25_SYM_FIRMWARE_RXREADYEVENT,
    CS40L25_SYM_FIRMWARE_ACTIVETOSTANDBYEVENT,
};

/**
 * DSP firmware controls to read for Event Control sources
 *
 * @see CS40L2X_EVENT_
 *
 * @attention CS40L2X_EVENT_HARDWARE_ENABLED must come first in the list
 *
 * @warning Must stay in sync with the DSP firmware control list in cs40l2x_event_controls
 *
 */
static const unsigned int cs40l2x_event_masks[CS40L25_EVENT_SOURCES] =
{
    CS40L2X_EVENT_HARDWARE_ENABLED, // For cs40l25_event_handler, HW Event handling must be first
    CS40L2X_EVENT_GPIO1_ENABLED,
    CS40L2X_EVENT_GPIO2_ENABLED,
    CS40L2X_EVENT_GPIO3_ENABLED,
    CS40L2X_EVENT_GPIO4_ENABLED,
    CS40L2X_EVENT_START_ENABLED | CS40L2X_EVENT_END_ENABLED,
    CS40L2X_EVENT_START_ENABLED | CS40L2X_EVENT_END_ENABLED,
    CS40L2X_EVENT_READY_ENABLED,
    CS40L2X_EVENT_ACTIVETOSTANDBY_ENABLED,
};

/**
 * Map of DSP Firmware Event Control event value to driver event to report to the upper layer
 *
 * @see CS40L25_EVENT_FLAG_
 *
 */
static const uint32_t cs40l25_event_value_to_flag_map[] =
{
    CS40L25_EVENT_FLAG_GPIO_1_RELEASE,
    CS40L25_EVENT_FLAG_GPIO_1_PRESS,
    CS40L25_EVENT_FLAG_GPIO_2_RELEASE,
    CS40L25_EVENT_FLAG_GPIO_2_PRESS,
    CS40L25_EVENT_FLAG_GPIO_3_RELEASE,
    CS40L25_EVENT_FLAG_GPIO_3_PRESS,
    CS40L25_EVENT_FLAG_GPIO_4_RELEASE,
    CS40L25_EVENT_FLAG_GPIO_4_PRESS,
    0,
    0,
    CS40L25_EVENT_FLAG_CP_PLAYBACK_DONE,
    CS40L25_EVENT_FLAG_GPIO_PLAYBACK_DONE,
    CS40L25_EVENT_FLAG_READY_FOR_DATA,
    0,
    CS40L25_EVENT_FLAG_CP_PLAYBACK_SUSPEND,
    CS40L25_EVENT_FLAG_CP_PLAYBACK_RESUME,
    CS40L25_EVENT_FLAG_ACTIVE_TO_STANDBY
};

/**
 * Map of DSP Firmware Event Control hardware event value to driver event to report to the upper layer
 *
 * @see CS40L25_EVENT_FLAG_
 *
 */
static const uint32_t cs40l25_irq2_mask_1_to_event_flag_map[CS40L25_EVENT_HW_SOURCES * 2] =
{
    IRQ2_IRQ2_EINT_1_AMP_ERR_EINT2_BITMASK, CS40L25_EVENT_FLAG_AMP_SHORT,
    IRQ2_IRQ2_EINT_1_TEMP_ERR_EINT2_BITMASK, CS40L25_EVENT_FLAG_OVERTEMP_ERROR,
    IRQ2_IRQ2_EINT_1_TEMP_WARN_RISE_EINT2_BITMASK, CS40L25_EVENT_FLAG_OVERTEMP_WARNING,
    IRQ2_IRQ2_EINT_1_BST_SHORT_ERR_EINT2_BITMASK, CS40L25_EVENT_FLAG_BOOST_INDUCTOR_SHORT,
    IRQ2_IRQ2_EINT_1_BST_DCM_UVP_ERR_EINT2_BITMASK, CS40L25_EVENT_FLAG_BOOST_UNDERVOLTAGE,
    IRQ2_IRQ2_EINT_1_BST_OVP_ERR_EINT2_BITMASK, CS40L25_EVENT_FLAG_BOOST_OVERVOLTAGE,
};

/**
 * Patch to be loaded to the IMASKSEQ in DSP Firmware
 *
 * This is packed data for the IRQ Masks to be applied after the part wakes from hibernate.
 *
 */
static const uint32_t cs40l25_irqmaskseq_patch[] =
{
    CS40L25_IMASKSEQ_WORD_1(CS40L25_IRQ2_MASK1_DEFAULT),
    CS40L25_IMASKSEQ_WORD_2(CS40L25_IRQ2_MASK1_DEFAULT),
    CS40L25_IMASKSEQ_WORD_1(CS40L25_IRQ2_MASK2_DEFAULT),
    CS40L25_IMASKSEQ_WORD_2(CS40L25_IRQ2_MASK2_DEFAULT),
    CS40L25_IMASKSEQ_WORD_1(CS40L25_IRQ2_MASK3_DEFAULT),
    CS40L25_IMASKSEQ_WORD_2(CS40L25_IRQ2_MASK3_DEFAULT),
    CS40L25_IMASKSEQ_WORD_1(CS40L25_IRQ2_MASK4_DEFAULT),
    CS40L25_IMASKSEQ_WORD_2(CS40L25_IRQ2_MASK4_DEFAULT),
};

/***********************************************************************************************************************
 * LOCAL FUNCTIONS
 **********************************************************************************************************************/

/**
 * Notify the driver when the CS40L25 INTb GPIO drops low.
 *
 * This callback is registered with the BSP in the register_gpio_cb() API call.
 *
 * The primary task of this callback is to transition the driver mode from CS40L25_MODE_HANDLING_CONTROLS to
 * CS40L25_MODE_HANDLING_EVENTS, in order to signal to the main thread to process events.
 *
 * @param [in] status           BSP status for the INTb IRQ.
 * @param [in] cb_arg           A pointer to callback argument registered.  For the driver, this arg is used for a
 *                              pointer to the driver state cs40l25_t.
 *
 * @return none
 *
 * @see bsp_driver_if_t member register_gpio_cb.
 * @see bsp_callback_t
 *
 */
static void cs40l25_irq_callback(uint32_t status, void *cb_arg)
{
    cs40l25_t *d;

    d = (cs40l25_t *) cb_arg;

    if (status == BSP_STATUS_OK)
    {
        // Switch driver mode to CS40L25_MODE_HANDLING_EVENTS
        d->mode = CS40L25_MODE_HANDLING_EVENTS;
    }

    return;
}

/**
 * Add entry to the WSEQ Table
 *
 * A new entry of HW register address/value will be added to the WSEQ table in the correct pattern of bytes.
 *
 * @param [in] driver           Pointer to the driver state
 * @param [in] addr             32-bit address of new entry
 * @param [in] value            32-bit value of new entry
 *
 * @return
 * - CS40L25_STATUS_FAIL        if WSEQ table is full
 * - CS40L25_STATUS_OK          otherwise
 *
 */
static uint32_t cs40l25_wseq_table_add(cs40l25_t *driver, uint32_t address, uint32_t value)
{
    uint32_t ret = CS40L25_STATUS_OK;

    cs40l25_wseq_entry_t *table = (cs40l25_wseq_entry_t *) &driver->wseq_table;
    uint32_t num_entries = driver->wseq_num_entries;

    if (num_entries < CS40L25_WSEQ_MAX_ENTRIES)
    {
        table[num_entries].address_ms = (address & 0xFF00) >> 8;
        table[num_entries].address_ls = address & 0x00FF;
        table[num_entries].val_3 = (value & 0xFF000000) >> 24;
        table[num_entries].val_2 = (value & 0x00FF0000) >> 16;
        table[num_entries].val_1 = (value & 0x0000FF00) >> 8;
        table[num_entries].val_0 = value & 0x000000FF;
        table[num_entries].changed = 1;
        // Make sure reserved* members are 0
        table[num_entries].reserved_0 = 0;
        table[num_entries].reserved_1 = 0;

        driver->wseq_num_entries += 1;
    }
    else
    {
        ret = CS40L25_STATUS_FAIL;
    }

    return ret;
}

/**
 * Update WSEQ Table with a new HW register value
 *
 * The WSEQ Table will be updated with a new value.  If an entry for the HW register address already exists, the value
 * only will be updated.  If an entry does not exist, a new entry will be added to the WSEQ Table.
 *
 * @param [in] driver           Pointer to the driver state
 * @param [in] addr             32-bit address of new entry
 * @param [in] value            32-bit value of new entry
 *
 * @return
 * - CS40L25_STATUS_FAIL        if WSEQ table is full
 * - CS40L25_STATUS_OK          otherwise
 *
 */
static uint32_t cs40l25_write_wseq_reg(cs40l25_t *driver, uint32_t address, uint32_t value)
{
    uint32_t ret;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    ret = regmap_write(cp, address, value);
    if (ret)
    {
        return CS40L25_STATUS_FAIL;
    }

    ret = CS40L25_STATUS_OK;

    if (!driver->wseq_initialized)
    {
        return ret;
    }

    cs40l25_wseq_entry_t *table = (cs40l25_wseq_entry_t *) &driver->wseq_table;

    if (address < 0xFFFF)
    {
        uint32_t num_entries = driver->wseq_num_entries;
        bool address_found = false;
        for (uint32_t i = 0; i < num_entries; i++) {
            uint32_t temp_address = (table[i].address_ms << 8) | (table[i].address_ls);
            uint32_t temp_value = (table[i].val_3 << 24) | (table[i].val_2 << 16) |
                                  (table[i].val_1 << 8) | (table[i].val_0);
            if (temp_address == address)
            {
                if ((temp_value != value) &&
                    (address != CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG))
                {
                    table[i].val_3 = (value & 0xFF000000) >> 24;
                    table[i].val_2 = (value & 0x00FF0000) >> 16;
                    table[i].val_1 = (value & 0x0000FF00) >> 8;
                    table[i].val_0 = value & 0x000000FF;
                    table[i].changed = 1;
                }

                address_found = true;
            }
        }

        if (!address_found && address != CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG)
        {
            //Add new address to end of table if there is space
            ret = cs40l25_wseq_table_add(driver, address, value);
            num_entries = driver->wseq_num_entries;
            if (ret == CS40L25_STATUS_OK)
            {
                //Shift the locking entries ( the last two entries ) back to the end
                //after appending the new entry. Set all entries to changed = 1, as
                //they now have a new position in POWERONSEQUENCE and must be written
                //over the bus.
                cs40l25_wseq_entry_t temp;
                temp = table[num_entries - 1];
                table[num_entries - 1] = table[num_entries - 2];
                table[num_entries - 1].changed = 1;
                table[num_entries - 2] = table[num_entries - 3];
                table[num_entries - 2].changed = 1;
                table[num_entries - 3] = temp;
            }
        }
    }

    return ret;
}

/**
 * Add a block of WSEQ entries to the WSEQ Table
 *
 * A table of WSEQ entries (already formatted for WSEQ) will be added to the table.  Used to initialize the WSEQ Table
 * at boot.
 *
 * @param [in] driver           Pointer to the driver state
 * @param [in] entries          pointer to block of WSEQ entries
 * @param [in] num_entries      number of WSEQ entries to add
 *
 * @return
 * - CS40L25_STATUS_FAIL        if addition of any WSEQ entry failed
 * - CS40L25_STATUS_OK          otherwise
 *
 */
static uint32_t cs40l25_wseq_add_block(cs40l25_t *driver, uint32_t *entries, uint32_t num_entries)
{
    for (uint32_t i = 0; i < num_entries; i++) {
        uint32_t ret;
        if (entries[2 * i] == CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG)
        {
            continue;
        }
        ret = cs40l25_wseq_table_add(driver, entries[2 * i], entries[2 * i + 1]);
        if (ret != CS40L25_STATUS_OK) {
            return ret;
        }
    }
    return CS40L25_STATUS_OK;
}

/**
 * Write ACK-ed firmware control with CS40L25-specific polling tries and delay
 *
 * @param [in] driver           Pointer to the driver state
 * @param [in] id               symbol id of firmware control to write
 * @param [in] val              value to write
 *
 * @return
 * - CS40L25_STATUS_FAIL        if underlying regmap call fails
 * - CS40L25_STATUS_OK          otherwise
 *
 */
static uint32_t cs40l25_write_acked_fw_control(cs40l25_t *driver, uint32_t id, uint32_t val)
{
    uint32_t ret;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    ret = regmap_write_acked_fw_control(cp,
                                        driver->fw_info,
                                        id,
                                        val,
                                        0,
                                        CS40L25_POLL_ACK_CTRL_MAX,
                                        CS40L25_POLL_ACK_CTRL_MS);
    if (ret)
    {
        ret = CS40L25_STATUS_FAIL;
    }
    else
    {
        ret = CS40L25_STATUS_OK;
    }

    return ret;
}

/**
 * Power up from Standby
 *
 * This function performs all necessary steps to transition the CS40L25 to be ready to generate haptic events.
 * Completing this results in the driver transition to POWER_UP state.
 *
 * @param [in] driver           Pointer to the driver state
 *
 * @return
 * - CS40L25_STATUS_FAIL if:
 *      - OTP_BOOT_DONE is not set
 *      - DSP Scratch register is not cleared
 * - CS40L25_STATUS_OK          otherwise
 *
 */
static uint32_t cs40l25_power_up(cs40l25_t *driver)
{
    uint32_t count = 0;
    uint32_t temp_reg_val = 0;
    uint32_t ret;
    uint32_t halo_symbol;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    // Write errata
    regmap_write_array(cp,
                       (uint32_t *) cs40l25_revb0_errata_patch,
                       (sizeof(cs40l25_revb0_errata_patch)/sizeof(uint32_t)));

    // Set HALO Core DSP Sample Rate registers to G1R2
    for (count = 0; count < (sizeof(cs40l25_frame_sync_regs)/sizeof(uint32_t)); count++)
    {
        regmap_write(cp, cs40l25_frame_sync_regs[count], CS40L25_DSP1_SAMPLE_RATE_G1R2);
    }

    // Send words of Power Up Patch
    regmap_write_array(cp,
                       (uint32_t *) cs40l25_pup_patch,
                       (sizeof(cs40l25_pup_patch)/sizeof(uint32_t)));

    // Enable clocks to HALO Core DSP in DSP CCM control register
    regmap_update_reg(cp,
                      XM_UNPACKED24_DSP1_CCM_CORE_CONTROL_REG,
                      (XM_UNPACKED24_DSP1_CCM_CORE_CONTROL_DSP1_CCM_CORE_EN_BITMASK | \
                        XM_UNPACKED24_DSP1_CCM_CORE_CONTROL_DSP1_CCM_CORE_RESET_BITMASK),
                      (XM_UNPACKED24_DSP1_CCM_CORE_CONTROL_DSP1_CCM_CORE_EN_BITMASK | \
                       XM_UNPACKED24_DSP1_CCM_CORE_CONTROL_DSP1_CCM_CORE_RESET_BITMASK));

    halo_symbol = (driver->state == CS40L25_STATE_CAL_STANDBY) ?
                  CS40L25_CAL_SYM_FIRMWARE_HALO_STATE :
                  CS40L25_SYM_FIRMWARE_HALO_STATE;

    ret = regmap_poll_fw_control(cp,
                                 driver->fw_info,
                                 halo_symbol,
                                 0xCB,
                                 CS40L25_POLL_OTP_BOOT_DONE_MAX,
                                 CS40L25_T_BST_PUP_MS);
    if (ret)
    {
        /*
         * Some systems can miss the transition inside the poll window even
         * though the DSP reaches ACTIVE immediately afterwards. Re-read once
         * before reporting failure.
         */
        ret = regmap_read_fw_control(cp, driver->fw_info, halo_symbol, &temp_reg_val);
        if ((ret != REGMAP_STATUS_OK) || (temp_reg_val != 0xCB))
        {
            return CS40L25_STATUS_FAIL;
        }
    }

    regmap_read(cp, XM_UNPACKED24_DSP1_SCRATCH_REG, &temp_reg_val);

    if (temp_reg_val)
    {
        return CS40L25_STATUS_FAIL;
    }

    return CS40L25_STATUS_OK;
}

/**
 * Power down to Standby
 *
 * This function performs all necessary steps to transition the CS40L25 to be in Standby power mode. This includes
 * disabling clocks to the HALO Core DSP.  Completing this results in the driver transition to CAL_STANDBY or
 * DSP_STANDBY state.
 *
 * @param [in] driver           Pointer to the driver state
 *
 * @return
 * - CS40L25_STATUS_FAIL if:
 *      - Control port activity fails
 *      - Firmware control addresses cannot be resolved by Symbol ID
 * - CS40L25_STATUS_OK          otherwise
 *
 */
static uint32_t cs40l25_power_down(cs40l25_t *driver)
{
    uint32_t ret;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    // Force fw into standby
    if (driver->state == CS40L25_STATE_CAL_POWER_UP)
    {
        ret = cs40l25_write_acked_fw_control(driver, CS40L25_CAL_SYM_FIRMWARE_SHUTDOWNREQUEST, 1);
    }
    else
    {
        ret = regmap_write_acked_reg(cp,
                                     DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_REG,
                                     CS40L25_POWERCONTROL_FRC_STDBY,
                                     CS40L25_POWERCONTROL_NONE,
                                     CS40L25_POLL_ACK_CTRL_MAX,
                                     CS40L25_POLL_ACK_CTRL_MS);
    }

    if (ret)
    {
        return CS40L25_STATUS_FAIL;
    }

    // Disable HALO Core DSP
    regmap_update_reg(cp,
                      XM_UNPACKED24_DSP1_CCM_CORE_CONTROL_REG,
                      XM_UNPACKED24_DSP1_CCM_CORE_CONTROL_DSP1_CCM_CORE_EN_BITMASK,
                      0);

    return CS40L25_STATUS_OK;
}

/**
 * Exit Basic Haptics Mode (BHM)
 *
 * This function performs all necessary steps to transition the CS40L25 out of Basic Haptics Mode (BHM) and into
 * Standby Mode.  Completing this results in the driver transition to STANDBY state.
 *
 * @param [in] driver           Pointer to the driver state
 *
 * @return
 * - CS40L25_STATUS_FAIL if:
 *      - Control port activity fails
 *      - Any errors or failures when exiting BHM
 * - CS40L25_STATUS_OK          otherwise
 *
 */
static uint32_t cs40l25_exit_bhm(cs40l25_t *driver)
{
    uint32_t *patch;
    uint32_t patch_length;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

#ifndef CONFIG_OPEN_LOOP
    uint32_t temp_reg_val = 0;
    uint32_t ret;

    // Request BHM shuts down
    regmap_write(cp, DSP_BHM_AMP_SHUTDOWNREQUEST_REG, DSP_BHM_AMP_SHUTDOWNREQUEST_BITMASK);
    // Wait for at least 1ms
    bsp_driver_if_g->set_timer(BSP_TIMER_DURATION_2MS, NULL, NULL);

    // If OTP_BOOT_DONE is set
    // Read SHUTDOWNREQUEST to see if the reg has been cleared
    ret = regmap_poll_reg(cp,
                          DSP_BHM_AMP_SHUTDOWNREQUEST_REG,
                          0,
                          CS40L25_POLL_OTP_BOOT_DONE_MAX,
                          CS40L25_POLL_OTP_BOOT_DONE_MS);

    if (ret)
    {
        return CS40L25_STATUS_FAIL;
    }

    // Read BHM_STATEMACHINE
    regmap_read(cp, DSP_BHM_STATEMACHINE_REG, &temp_reg_val);

    // If STATEMACHINE != shutdown
    if (temp_reg_val != DSP_BHM_STATEMACHINE_SHUTDOWN)
    {
        return CS40L25_STATUS_FAIL;
    }

    // Read BHM_AMP_STATUS
    regmap_read(cp, DSP_BHM_AMP_STATUS_REG, &temp_reg_val);

    // If any errors:
    if (temp_reg_val & (DSP_BHM_AMP_STATUS_OTP_ERROR_BITMASK |
                        DSP_BHM_AMP_STATUS_AMP_ERROR_BITMASK |
                        DSP_BHM_AMP_STATUS_TEMP_RISE_WARN_BITMASK |
                        DSP_BHM_AMP_STATUS_TEMP_ERROR_BITMASK))
    {
        return CS40L25_STATUS_FAIL;
    }
#endif

    // start basic mode revert
    if (driver->config.ext_boost.use_ext_boost)
    {
        patch = (uint32_t *) cs40l25_ext_boost_bhm_revert_patch;
        patch_length = (sizeof(cs40l25_ext_boost_bhm_revert_patch)/sizeof(uint32_t));
    }
    else
    {
        patch = (uint32_t *) cs40l25_bhm_revert_patch;
        patch_length = (sizeof(cs40l25_bhm_revert_patch)/sizeof(uint32_t));
    }

    // Send BHM revert patch set
    regmap_write_array(cp, patch, patch_length);

    return CS40L25_STATUS_OK;
}

/**
 * Puts device into hibernate
 *
 * @param [in] driver           Pointer to the driver state
 *
 * @return
 * - CS40L25_STATUS_FAI         Control port activity fails
 * - CS40L25_STATUS_OK          otherwise
 *
 */
static uint32_t cs40l25_hibernate(cs40l25_t *driver)
{
    uint32_t count = 0;
    uint32_t reg_address;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    reg_address = fw_img_find_symbol(driver->fw_info, CS40L25_SYM_FIRMWARE_POWERONSEQUENCE);
    if (!reg_address)
    {
        return CS40L25_STATUS_FAIL;
    }

    while (count < driver->wseq_num_entries)
    {
        if (driver->wseq_table[count].changed == 1)
        {
            //Write 16bit address and 32bit value to poweronsequence
            regmap_write_block(cp,
                               reg_address + (8 * count),
                               (uint8_t *) driver->wseq_table[count].words,
                               8);

            driver->wseq_table[count].changed = 0;
        }

        count++;
    }

    regmap_write(cp, reg_address + (8 * count), 0x00FFFFFF);

    regmap_write(cp, DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_REG, CS40L25_POWERCONTROL_HIBERNATE);

    return CS40L25_STATUS_OK;
}

/**
 * Wakes device from hibernate
 *
 * @param [in] driver           Pointer to the driver state
 *
 * @return
 * - CS40L25_STATUS_FAI         Control port activity fails
 * - CS40L25_STATUS_OK          otherwise
 *
 */
static uint32_t cs40l25_wake(cs40l25_t *driver)
{
    uint32_t i, j;
    uint32_t temp_reg_val = 0;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    // Outer loop for wake-hibernate attempts
    for (i = 0; i < 10; i++)
    {
        // Inner loop for send WAKE command attempts
        for (j = 0; j < 10; j++)
        {
            uint32_t ret;
            // Request Wake
            ret = regmap_write(cp,
                               DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_REG,
                               CS40L25_POWERCONTROL_WAKEUP);

            // Check for control port write error, indicating possible wake from control port
            // If I2C command failed, then wait 1ms and try again
            if (ret == REGMAP_STATUS_FAIL)
            {
                bsp_driver_if_g->set_timer(BSP_TIMER_DURATION_1MS, NULL, NULL);
            }
            else
            {
                break;
            }
        }

        if (j >= 10)
        {
            return CS40L25_STATUS_FAIL;
        }

        // Increment the inner loop counter to read FW ID
        for (j = 0; j < 10; j++)
        {
            // Wait for at least 5ms
            bsp_driver_if_g->set_timer(BSP_TIMER_DURATION_5MS, NULL, NULL);

            // Read FW ID
            regmap_read(cp, CS40L25_FIRMWARE_ID_ADDR, &temp_reg_val);

            // Check if FW ID is correct
            if (temp_reg_val == driver->fw_info->header.fw_id)
            {
                break;
            }
        }

        // If FW ID was incorrect 10 times, force back into hibernate
        if (j >= 10)
        {
            // Request Hibernate manually (not via HALO MBOX)
            cs40l25_write_wseq_reg(driver, CS40L25_PWRMGT_CTL_REG, CS40L25_PWRMGT_CTL_MEM_RDY_TRIG_HIBER);
            // Wait for at least 1ms
            bsp_driver_if_g->set_timer(BSP_TIMER_DURATION_1MS, NULL, NULL);
        }
        else
        {
            for (j = 0; j < 10; j++)
            {
                // Read POWERSTATE
                regmap_read_fw_control(cp, driver->fw_info, CS40L25_SYM_FIRMWARE_POWERSTATE, &temp_reg_val);

                if ((temp_reg_val == CS40L25_POWERSTATE_ACTIVE) || (temp_reg_val == CS40L25_POWERSTATE_STANDBY))
                {
                    break;
                }
                else if (temp_reg_val == CS40L25_POWERSTATE_HIBERNATE)
                {
                    // Wait for at least 5ms
                    bsp_driver_if_g->set_timer(BSP_TIMER_DURATION_5MS, NULL, NULL);
                }
            }
            if (j >= 10)
            {
                return CS40L25_STATUS_FAIL;
            }
            else
            {
                break;
            }
        }
    }

    return CS40L25_STATUS_OK;
}

/**
 * Handle events indicated by the IRQ pin ALERTb
 *
 * This function performs all steps to handle IRQ and other asynchronous events the driver is aware of,
 * resulting in calling of the notification callback (cs40l25_notification_callback_t).
 *
 * If there are any IRQ events that include Actuator-Safe Mode Errors or Boost-related events, then the procedure
 * outlined in the Datasheet Sections 4.5.2, 4.9.3, 4.9.4 is implemented here.
 *
 * @param [in] driver           Pointer to the driver state
 *
 * @return
 * - CS40L25_STATUS_FAIL        Control port activity fails
 * - CS40L25_STATUS_OK          otherwise
 *
 * @see CS40L25_EVENT_FLAG_
 * @see cs40l25_notification_callback_t
 *
 */
static uint32_t cs40l25_event_handler(cs40l25_t *driver)
{
    uint32_t ret = CS40L25_STATUS_OK;
    uint32_t temp_reg_val;
    uint32_t msm_block_enables_val;
    uint8_t count;
    uint32_t temp_event_control = driver->config.event_control.reg.word;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    // Clear the event flags
    driver->event_flags = 0;

    // Read the first IRQ1 flag register
    ret = regmap_read(cp, XM_UNPACKED24_DSP1_SCRATCH_REG, &temp_reg_val);

    // If SCRATCH is nonzero OR CP transaction error
    if (temp_reg_val || (ret != CS40L25_STATUS_OK))
    {
        driver->event_flags = CS40L25_EVENT_FLAG_DSP_ERROR;

        return CS40L25_STATUS_OK;
    }

    // Read unmasked event registers
    for (count = 0; count < (sizeof(cs40l2x_event_controls)/sizeof(uint32_t)); count++)
    {
        regmap_read_fw_control(cp, driver->fw_info, cs40l2x_event_controls[count], &temp_reg_val);

        if ((temp_reg_val == CS40L2X_EVENT_CTRL_NONE) || ((temp_event_control & cs40l2x_event_masks[count]) == 0))
        {
            continue;
        }

        // If event is HW Event, then process separately
        if (temp_reg_val == CS40L2X_EVENT_CTRL_HARDWARE)
        {
            // Read the first IRQ2 flag register
            regmap_read(cp, IRQ2_IRQ2_EINT_1_REG, &temp_reg_val);

            // Set flags in event_flags
            for (uint8_t i = 0; i < (CS40L25_EVENT_HW_SOURCES * 2); i += 2)
            {
                if (temp_reg_val & cs40l25_irq2_mask_1_to_event_flag_map[i])
                {
                    driver->event_flags |= cs40l25_irq2_mask_1_to_event_flag_map[i + 1];
                }
            }

            // Clear any IRQ2 flags from first register
            temp_reg_val &= ~CS40L25_INT2_MASK_DEFAULT;
            regmap_write(cp, IRQ2_IRQ2_EINT_1_REG, temp_reg_val);

            // If there are Boost-related Errors, proceed to DISABLE_BOOST
            if (driver->event_flags & CS40L25_EVENT_FLAGS_BOOST_CYCLE)
            {
                // Read which MSM Blocks are enabled
                regmap_read(cp, MSM_BLOCK_ENABLES_REG, &msm_block_enables_val);
                // Disable Boost converter
                temp_reg_val = msm_block_enables_val & ~(MSM_BLOCK_ENABLES_BST_EN_BITMASK);
                cs40l25_write_wseq_reg(driver, MSM_BLOCK_ENABLES_REG, temp_reg_val);
            }
            // IF there are no Boost-related Errors, proceed to TOGGLE_ERR_RLS
            // Clear the Error Release register
            cs40l25_write_wseq_reg(driver, MSM_ERROR_RELEASE_REG, 0);
            // Set the Error Release register
            cs40l25_write_wseq_reg(driver, MSM_ERROR_RELEASE_REG, CS40L25_ERR_RLS_ACTUATOR_SAFE_MODE_MASK);
            // Clear the Error Release register
            cs40l25_write_wseq_reg(driver, MSM_ERROR_RELEASE_REG, 0);

            // If there are Boost-related Errors, re-enable Boost
            if (driver->event_flags & CS40L25_EVENT_FLAGS_BOOST_CYCLE)
            {
                cs40l25_write_wseq_reg(driver, MSM_BLOCK_ENABLES_REG, msm_block_enables_val);
            }

        }
        // Else set event flag and clear event source
        else if (temp_reg_val <= CS40L2X_EVENT_CTRL_MAX)
        {
            // Set correct bit in flags to send to the BSP Notification Callback
            driver->event_flags |= cs40l25_event_value_to_flag_map[temp_reg_val];
        }
        else
        {
            return CS40L25_STATUS_FAIL;
        }

        // Write EVENT_CTRL_NONE to the triggered event register
        regmap_write_fw_control(cp, driver->fw_info, cs40l2x_event_controls[count], CS40L2X_EVENT_CTRL_NONE);
    }

    // Write WAKE to POWERCONTROL register
    /*
     * polling for acknowledgment as with other mailbox registers
     * is unnecessary in this case and adds latency, so only send
     * the wake-up command to complete the notification sequence
     */
    regmap_write(cp, DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_REG, CS40L25_POWERCONTROL_WAKEUP);

    return CS40L25_STATUS_OK;
}

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/

/**
 * Initialize driver state/handle
 *
 */
uint32_t cs40l25_initialize(cs40l25_t *driver)
{
    uint32_t ret = CS40L25_STATUS_FAIL;

    if (NULL != driver)
    {
        /*
         * The memset() call sets all members to 0, including the following semantics:
         * - 'state' is set to UNCONFIGURED
         */
        memset(driver, 0, sizeof(cs40l25_t));

        ret = CS40L25_STATUS_OK;
    }

    return ret;
}

/**
 * Configures driver state/handle
 *
 */
uint32_t cs40l25_configure(cs40l25_t *driver, cs40l25_config_t *config)
{
    uint32_t ret = CS40L25_STATUS_FAIL;

    if ((NULL != driver) && \
        (NULL != config))
    {
        driver->config = *config;

        ret = bsp_driver_if_g->register_gpio_cb(driver->config.bsp_config.bsp_int_gpio_id,
                                                &cs40l25_irq_callback,
                                                driver);

        if (ret == BSP_STATUS_OK)
        {
            ret = CS40L25_STATUS_OK;
        }

        // Advance driver to CONFIGURED state
        driver->state = CS40L25_STATE_CONFIGURED;
    }

    return ret;
}

/**
 * Processes driver events and notifications
 *
 */
uint32_t cs40l25_process(cs40l25_t *driver)
{
    // check for driver state
    if ((driver->state != CS40L25_STATE_UNCONFIGURED) && (driver->state != CS40L25_STATE_ERROR))
    {
        // check for driver mode
        if (driver->mode == CS40L25_MODE_HANDLING_EVENTS)
        {
            // Check for valid state to process events
            if ((driver->state == CS40L25_STATE_DSP_STANDBY) ||
                (driver->state == CS40L25_STATE_DSP_POWER_UP) ||
                (driver->state == CS40L25_STATE_HIBERNATE))
            {
                // run through event handler
                if (CS40L25_STATUS_OK == cs40l25_event_handler(driver))
                {
                    driver->mode = CS40L25_MODE_HANDLING_CONTROLS;
                }
                else
                {
                    driver->state = CS40L25_STATE_ERROR;
                }
            }
            // If in invalid state for handling events (i.e. BHM, Calibration), simply switch back to Handling Controls
            else
            {
                driver->mode = CS40L25_MODE_HANDLING_CONTROLS;
            }
        }

        if (driver->state == CS40L25_STATE_ERROR)
        {
            driver->event_flags |= CS40L25_EVENT_FLAG_STATE_ERROR;
        }

        if (driver->event_flags)
        {
            if (driver->config.bsp_config.notification_cb != NULL)
            {
                driver->config.bsp_config.notification_cb(driver->event_flags,
                                                          driver->config.bsp_config.notification_cb_arg);
            }

            driver->event_flags = 0;
        }
    }

    return CS40L25_STATUS_OK;
}

/**
 * Reset the CS40L25
 *
 */
uint32_t cs40l25_reset(cs40l25_t *driver)
{
    uint32_t count;
    uint32_t temp_reg_val;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    if ((driver->state == CS40L25_STATE_UNCONFIGURED) || (driver->state == CS40L25_STATE_ERROR))
    {
        return CS40L25_STATUS_FAIL;
    }

    // Drive RESET low for at least T_RLPW (1ms)
    bsp_driver_if_g->set_gpio(driver->config.bsp_config.bsp_reset_gpio_id, BSP_GPIO_LOW);
    bsp_driver_if_g->set_timer(CS40L25_T_RLPW_MS, NULL, NULL);

    // Drive RESET high and wait for at least T_IRS (1ms)
    bsp_driver_if_g->set_gpio(driver->config.bsp_config.bsp_reset_gpio_id, BSP_GPIO_HIGH);
    bsp_driver_if_g->set_timer(CS40L25_T_IRS_MS, NULL, NULL);

    // Start polling OTP_BOOT_DONE bit every 10ms
    count = 0;
    do
    {
        regmap_read(cp, IRQ1_IRQ1_EINT_4_REG,  &temp_reg_val);
        if (temp_reg_val & IRQ1_IRQ1_EINT_4_BOOT_DONE_BITMASK)
            break;
        else if (count < CS40L25_POLL_OTP_BOOT_DONE_MAX)
        {
            bsp_driver_if_g->set_timer(CS40L25_POLL_OTP_BOOT_DONE_MS, NULL, NULL);
            count++;
        }
        else
        {
            return CS40L25_STATUS_FAIL;
        }
    }
    while (!(temp_reg_val & IRQ1_IRQ1_EINT_4_BOOT_DONE_BITMASK));

    // Read OTP_BOOT_ERR
    regmap_read(cp, IRQ1_IRQ1_EINT_3_REG, &temp_reg_val);
    if (temp_reg_val & IRQ1_IRQ1_EINT_3_OTP_BOOT_ERR_BITMASK)
    {
        return CS40L25_STATUS_FAIL;
    }

    // Read DEVID
    regmap_read(cp, CS40L25_SW_RESET_DEVID_REG, &temp_reg_val);
    driver->devid = temp_reg_val;
    // Read REVID
    regmap_read(cp, CS40L25_SW_RESET_REVID_REG, &temp_reg_val);
    driver->revid = temp_reg_val;

    // Start polling BHM_AMP_STATUS_BOOT_DONE bit every 10ms
    count = 0;
    do
    {
        regmap_read(cp, DSP_BHM_AMP_STATUS_REG, &temp_reg_val);
        if (temp_reg_val & DSP_BHM_AMP_STATUS_BOOT_DONE_BITMASK)
            break;
        else if (count < CS40L25_POLL_OTP_BOOT_DONE_MAX)
        {
            bsp_driver_if_g->set_timer(CS40L25_POLL_OTP_BOOT_DONE_MS, NULL, NULL);
            count++;
        }
        else
        {
            return CS40L25_STATUS_FAIL;
        }
    }
    while (!(temp_reg_val & DSP_BHM_AMP_STATUS_BOOT_DONE_BITMASK));

    driver->state = CS40L25_STATE_POWER_UP;

    return CS40L25_STATUS_OK;
}

/**
 * Finish booting the CS40L25
 *
 */
uint32_t cs40l25_boot(cs40l25_t *driver, fw_img_info_t *fw_info)
{
    bool is_cal_boot = false;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    driver->fw_info = fw_info;

    if (driver->fw_info == NULL)
    {
        return CS40L25_STATUS_OK;
    }

    if (driver->fw_info->header.fw_id == CS40L25_FWID_CAL)
    {
        is_cal_boot = true;
    }

    if (!is_cal_boot)
    {
        int errata_entries = sizeof(cs40l25_revb0_errata_patch) / (2 * sizeof(uint32_t));
        int wseq_entries = sizeof(cs40l25_wseq_regs) / (2 * sizeof(uint32_t));

        driver->wseq_num_entries = 0;
        cs40l25_wseq_table_add(driver, CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG, CS40L25_TEST_KEY_CTRL_UNLOCK_1);
        cs40l25_wseq_table_add(driver, CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG, CS40L25_TEST_KEY_CTRL_UNLOCK_2);
        cs40l25_wseq_add_block(driver, (uint32_t *) cs40l25_revb0_errata_patch, errata_entries);
        cs40l25_wseq_add_block(driver, (uint32_t *) cs40l25_wseq_regs, wseq_entries);
        if (driver->config.ext_boost.use_ext_boost)
        {
            cs40l25_wseq_table_add(driver, MSM_BLOCK_ENABLES_REG, 0x00003301);
        }
        else
        {
            cs40l25_wseq_table_add(driver, MSM_BLOCK_ENABLES_REG, 0x00003321);
        }
        cs40l25_wseq_table_add(driver, CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG, CS40L25_TEST_KEY_CTRL_LOCK_1);
        cs40l25_wseq_table_add(driver, CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG, CS40L25_TEST_KEY_CTRL_LOCK_2);
        driver->wseq_initialized = true;
    }

    // Apply Calibration data
    if (!is_cal_boot)
    {
        if (driver->config.cal_data.is_valid_f0)
        {
            regmap_write_fw_control(cp, driver->fw_info, CS40L25_SYM_FIRMWARE_F0_STORED, driver->config.cal_data.f0);
            regmap_write_fw_control(cp,
                                    driver->fw_info,
                                    CS40L25_SYM_FIRMWARE_REDC_STORED,
                                    driver->config.cal_data.redc);

        }

        if (driver->config.cal_data.is_valid_qest)
        {
            regmap_write_fw_control(cp, driver->fw_info, CS40L25_SYM_FIRMWARE_Q_STORED, driver->config.cal_data.qest);
        }

        driver->state = CS40L25_STATE_DSP_STANDBY;
    }
    else
    {
        driver->state = CS40L25_STATE_CAL_STANDBY;
    }

    // Write configuration data
    // Unlock the register file
    regmap_write(cp, CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG, CS40L25_TEST_KEY_CTRL_UNLOCK_1);
    regmap_write(cp, CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG, CS40L25_TEST_KEY_CTRL_UNLOCK_2);

    regmap_write_array(cp, driver->config.syscfg_regs, driver->config.syscfg_regs_total);

    // If NOT Calibration boot, write HALO Core DSP configuration data and IRQMASKSEQ patch
    if (driver->state == CS40L25_STATE_DSP_STANDBY)
    {
        // Apply Event Control settings
        regmap_write_fw_control(cp,
                                driver->fw_info,
                                CS40L25_SYM_FIRMWARE_EVENTCONTROL,
                                driver->config.event_control.reg.word);

        // Apply IRQMASKSEQ Patch set
        regmap_write_fw_vals(cp,
                             driver->fw_info,
                             CS40L25_SYM_FIRMWARE_IRQMASKSEQUENCE,
                             (uint32_t *) cs40l25_irqmaskseq_patch,
                             (sizeof(cs40l25_irqmaskseq_patch) / sizeof(uint32_t)));
        regmap_write_fw_control(cp, driver->fw_info, CS40L25_SYM_FIRMWARE_IRQMASKSEQUENCE_VALID, 0x1);

        // Apply External Boost configuration
        if (driver->config.ext_boost.use_ext_boost)
        {
            regmap_write_fw_control(cp, driver->fw_info, CS40L25_SYM_FIRMWARE_USE_EXT_BOOST, 0x1);
            regmap_write_fw_control(cp,
                                    driver->fw_info,
                                    CS40L25_SYM_FIRMWARE_GPI_PLAYBACK_DELAY,
                                    driver->config.ext_boost.gpi_playback_delay);
        }
    }

    // Lock the register file
    regmap_write(cp, CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG, CS40L25_TEST_KEY_CTRL_LOCK_1);
    regmap_write(cp, CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG, CS40L25_TEST_KEY_CTRL_LOCK_2);

    // Write any FW Controls only sampled at FW initialization
    regmap_write_fw_control(cp,
                            driver->fw_info,
                            CS40L25_SYM_FIRMWARE_GPIO_BUTTONDETECT,
                            driver->config.gpio_button_detect.word);

    return CS40L25_STATUS_OK;
}

/**
 * Change the power state
 *
 */
uint32_t cs40l25_power(cs40l25_t *driver, uint32_t power_state)
{
    uint32_t ret = CS40L25_STATUS_FAIL;
    uint32_t new_state = driver->state;

    // Submit the correct request based on power_state
    switch (power_state)
    {
        case CS40L25_POWER_UP:
            // POWER_UP Control Requests are only valid for STANDBY and DSP_STANDBY states
            if ((driver->state == CS40L25_STATE_STANDBY) ||
                (driver->state == CS40L25_STATE_DSP_STANDBY) ||
                (driver->state == CS40L25_STATE_CAL_STANDBY))
            {
                ret = cs40l25_power_up(driver);

                if (driver->state == CS40L25_STATE_STANDBY)
                {
                    new_state = CS40L25_STATE_POWER_UP;
                }
                else if (driver->state == CS40L25_STATE_DSP_STANDBY)
                {
                    new_state = CS40L25_STATE_DSP_POWER_UP;
                }
                else if (driver->state == CS40L25_STATE_CAL_STANDBY)
                {
                    new_state = CS40L25_STATE_CAL_POWER_UP;
                }
            }

            break;

        case CS40L25_POWER_DOWN:
            if (driver->state == CS40L25_STATE_POWER_UP)
            {
                ret = cs40l25_exit_bhm(driver);
                new_state = CS40L25_STATE_STANDBY;
            }
            else if (driver->state == CS40L25_STATE_DSP_POWER_UP)
            {
                ret = cs40l25_power_down(driver);
                new_state = CS40L25_STATE_DSP_STANDBY;
            }
            else if (driver->state == CS40L25_STATE_CAL_POWER_UP)
            {
                ret = cs40l25_power_down(driver);
                new_state = CS40L25_STATE_CAL_STANDBY;
            }
            break;

        case CS40L25_POWER_HIBERNATE:
            if (driver->state == CS40L25_STATE_DSP_POWER_UP)
            {
                ret = cs40l25_hibernate(driver);
                new_state = CS40L25_STATE_HIBERNATE;
            }
            break;

        case CS40L25_POWER_WAKE:
            if (driver->state == CS40L25_STATE_HIBERNATE)
            {
                ret = cs40l25_wake(driver);
                new_state = CS40L25_STATE_DSP_POWER_UP;
            }
            break;

        default:
            break;
    }

    if (ret == CS40L25_STATUS_OK)
    {
        driver->state = new_state;
    }

    return ret;
}

/**
 * Calibrate the HALO Core DSP Protection Algorithm
 *
 */
uint32_t cs40l25_calibrate(cs40l25_t *driver, uint32_t calib_type)
{
    uint32_t temp_reg_val = 0;
    uint32_t calib_pcm_vol = 0;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    if (!(calib_type & CS40L25_CALIB_ALL) || (driver->state != CS40L25_STATE_CAL_POWER_UP))
    {
        return CS40L25_STATUS_FAIL;
    }

    // Get all control addresses needed - just re-use the array for the addresses
    uint32_t ctrl_addresses[] =
    {
        CS40L25_CAL_SYM_F0_TRACKING_MAXBACKEMF,
        CS40L25_CAL_SYM_F0_TRACKING_CLOSED_LOOP,
        CS40L25_CAL_SYM_F0_TRACKING_F0_TRACKING_ENABLE,
        CS40L25_CAL_SYM_F0_TRACKING_F0,
        CS40L25_CAL_SYM_F0_TRACKING_REDC,
        CS40L25_CAL_SYM_Q_ESTIMATION_Q_EST
    };

    for (uint8_t i = 0; i < (sizeof(ctrl_addresses) / sizeof(uint32_t)); i++)
    {
        ctrl_addresses[i] = fw_img_find_symbol(driver->fw_info, ctrl_addresses[i]);
        if (!ctrl_addresses[i])
        {
            return CS40L25_STATUS_FAIL;
        }
    }

    driver->config.cal_data.is_valid_f0 = false;
    driver->config.cal_data.is_valid_qest = false;

    // Read current volume
    regmap_read(cp, CS40L25_INTP_AMP_CTRL_REG, &temp_reg_val);
    uint32_t temp_mask = (~(0xFFFFFFFF << CS40L25_INTP_AMP_CTRL_AMP_VOL_PCM_BITWIDTH) << CS40L25_INTP_AMP_CTRL_AMP_VOL_PCM_BITOFFSET);

    // Save volume level
    calib_pcm_vol = temp_reg_val;
    cs40l25_write_wseq_reg(driver, CS40L25_INTP_AMP_CTRL_REG, temp_reg_val & ~temp_mask);

    if (calib_type & CS40L25_CALIB_F0)
    {
        regmap_write(cp, ctrl_addresses[0], 0); // MAXBACKEMF
        regmap_write(cp, ctrl_addresses[1], 0); // CLOSED_LOOP
        regmap_write(cp, ctrl_addresses[2], 1); // F0_TRACKING_ENABLE

        // Wait 500ms
        bsp_driver_if_g->set_timer(500, NULL, NULL);

        regmap_write(cp, ctrl_addresses[1], 1); // CLOSED_LOOP

        // Wait 2s
        bsp_driver_if_g->set_timer(BSP_TIMER_DURATION_2S, NULL, NULL);

        regmap_write(cp, ctrl_addresses[2], 0); // F0_TRACKING_ENABLE
        regmap_read(cp, ctrl_addresses[3], &(driver->config.cal_data.f0)); // F0
        regmap_read(cp, ctrl_addresses[4], &(driver->config.cal_data.redc)); // REDC
        regmap_read(cp, ctrl_addresses[0], &(driver->config.cal_data.backemf)); // MAXBACKEMF
        driver->config.cal_data.is_valid_f0 = true;
    }

    if (calib_type & CS40L25_CALIB_QEST)
    {
        regmap_write(cp, ctrl_addresses[2], 2); // F0_TRACKING_ENABLE

        uint32_t count;
        for (count = 0; count < CS40L25_POLL_CAL_Q_MAX; count++)
        {
            bsp_driver_if_g->set_timer(100, NULL, NULL);

            regmap_read(cp, ctrl_addresses[2], &temp_reg_val); // F0_TRACKING_ENABLE

            if (temp_reg_val == 0)
            {
                break;
            }
        }

        if (count >= CS40L25_POLL_CAL_Q_MAX)
        {
            return CS40L25_STATUS_FAIL;
        }

        regmap_read(cp, ctrl_addresses[5], &(driver->config.cal_data.qest)); // Q_EST
        driver->config.cal_data.is_valid_qest = true;
    }

    cs40l25_write_wseq_reg(driver, CS40L25_INTP_AMP_CTRL_REG, calib_pcm_vol);

    return CS40L25_STATUS_OK;
}

/**
 * Start I2S Streaming Mode
 *
 */
uint32_t cs40l25_start_i2s(cs40l25_t *driver)
{
    uint32_t ret, val;
    bool i2s_passthrough = true;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    /* If the firmware doesn't support i2s pass-through:
     * - bypass the DSP
     * - force the DSP into standby
     * - set global_enable
     */
    regmap_read_fw_control(cp, driver->fw_info, CS40L25_SYM_FIRMWARE_FEATURE_BITMAP, &val);
    if (!(val & CS40L25_FEATURE_BITMAP_I2s))
    {
        i2s_passthrough = false;
        cs40l25_write_wseq_reg(driver, CS40L25_MIXER_DACPCM1_INPUT_REG, CS40L25_INPUT_SRC_ASPRX1);
    }

    // Enable ASPs
    cs40l25_dataif_asp_enables1_t asp_reg_val;
    regmap_read(cp, DATAIF_ASP_ENABLES1_REG, &(asp_reg_val.word));
    asp_reg_val.asp_rx1_en = 1;
    asp_reg_val.asp_rx2_en = 1;
    cs40l25_write_wseq_reg(driver, DATAIF_ASP_ENABLES1_REG, asp_reg_val.word);

    // Force DSP into standby
    ret = regmap_write_acked_reg(cp,
                                 DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_REG,
                                 DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_FORCE_STANDBY,
                                 DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_NONE,
                                 CS40L25_POLL_ACK_CTRL_MAX,
                                 CS40L25_POLL_ACK_CTRL_MS);

    if (ret != REGMAP_STATUS_OK)
    {
        return CS40L25_STATUS_FAIL;
    }

    // Change clock to bclk - encoding of PLL REFCLK is same as ASP BCLK
    cs40l25_dataif_asp_control1_t asp_control1;
    cs40l25_ccm_refclk_input_t clk_reg_val;

    regmap_read(cp, DATAIF_ASP_CONTROL1_REG, &(asp_control1.word));
    regmap_read(cp, CCM_REFCLK_INPUT_REG, &(clk_reg_val.word));

    clk_reg_val.pll_refclk_sel = CS40L25_PLL_REFLCLK_SEL_BCLK;
    clk_reg_val.pll_refclk_freq = asp_control1.asp_bclk_freq;

#ifdef CONFIG_OPEN_LOOP
    regmap_write(cp, CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG, CS40L25_TEST_KEY_CTRL_UNLOCK_1);
    regmap_write(cp, CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG, CS40L25_TEST_KEY_CTRL_UNLOCK_2);
    cs40l25_write_wseq_reg(driver, 0x2D20, 0x0);
    regmap_write(cp, CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG, CS40L25_TEST_KEY_CTRL_LOCK_1);
    regmap_write(cp, CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG, CS40L25_TEST_KEY_CTRL_LOCK_2);
    cs40l25_write_wseq_reg(driver, 0x3018, 0x0);
#endif

    cs40l25_write_wseq_reg(driver, CCM_REFCLK_INPUT_REG, clk_reg_val.word);

    if (i2s_passthrough)
    {
        //Wake the firmware
        ret = regmap_write_acked_reg(cp,
                                     DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_REG,
                                     DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_WAKEUP,
                                     DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_NONE,
                                     CS40L25_POLL_ACK_CTRL_MAX,
                                     CS40L25_POLL_ACK_CTRL_MS);

        if (ret != REGMAP_STATUS_OK)
        {
            return CS40L25_STATUS_FAIL;
        }

        //Enable I2S
        ret = regmap_write_acked_reg(cp,
                                     DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_5_REG,
                                     DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_5_START_I2S,
                                     DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_5_NONE,
                                     CS40L25_POLL_ACK_CTRL_MAX,
                                     CS40L25_POLL_ACK_CTRL_MS);
    }
    else
    {
        ret = cs40l25_write_wseq_reg(driver, MSM_GLOBAL_ENABLES_REG, MSM_GLOBAL_ENABLES_GLOBAL_EN_BITMASK);
    }

    if (ret)
    {
        return CS40L25_STATUS_FAIL;
    }

    return CS40L25_STATUS_OK;
}

/**
 * Stop I2S Streaming Mode
 *
 */
uint32_t cs40l25_stop_i2s(cs40l25_t *driver)
{
    uint32_t ret, val;
    bool i2s_passthrough = true;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    /* If the firmware doesn't support i2s pass-through:
     * - disable global_enable
     * - the DSP will aready be in standby
     * - re-route the ASP through the DSP
     */
    regmap_read_fw_control(cp, driver->fw_info, CS40L25_SYM_FIRMWARE_FEATURE_BITMAP, &val);
    if (!(val & CS40L25_FEATURE_BITMAP_I2s))
    {
        i2s_passthrough = false;
        cs40l25_write_wseq_reg(driver, MSM_GLOBAL_ENABLES_REG, 0);
    }

    if (i2s_passthrough)
    {
        ret = regmap_write_acked_reg(cp,
                                     DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_REG,
                                     DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_FORCE_STANDBY,
                                     DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_NONE,
                                     CS40L25_POLL_ACK_CTRL_MAX,
                                     CS40L25_POLL_ACK_CTRL_MS);

        if (ret != REGMAP_STATUS_OK)
        {
            return CS40L25_STATUS_FAIL;
        }
    }

    // Change clock back to MCLK
    cs40l25_ccm_refclk_input_t clk_reg_val;
    clk_reg_val.word = CCM_REFCLK_INPUT_REG_DEFAULT;

    for (uint32_t i = 0; i < driver->config.syscfg_regs_total; i++)
    {
        if (driver->config.syscfg_regs[i] == CCM_REFCLK_INPUT_REG)
        {
            clk_reg_val.word |= driver->config.syscfg_regs[i+1];
        }
    }

    ret = cs40l25_write_wseq_reg(driver, CCM_REFCLK_INPUT_REG, clk_reg_val.word);
    if (ret != CS40L25_STATUS_OK)
    {
        return CS40L25_STATUS_FAIL;
    }

#ifdef CONFIG_OPEN_LOOP
    cs40l25_write_wseq_reg(driver, 0x3018, 0x02000000);
    regmap_write(cp, CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG, CS40L25_TEST_KEY_CTRL_UNLOCK_1);
    regmap_write(cp, CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG, CS40L25_TEST_KEY_CTRL_UNLOCK_2);
    cs40l25_write_wseq_reg(driver, 0x2D20, 0x00000030);
    regmap_write(cp, CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG, CS40L25_TEST_KEY_CTRL_LOCK_1);
    regmap_write(cp, CS40L25_CTRL_KEYS_TEST_KEY_CTRL_REG, CS40L25_TEST_KEY_CTRL_LOCK_2);
#endif

    //Wake the firmware

    ret = regmap_write_acked_reg(cp,
                                 DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_REG,
                                 DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_WAKEUP,
                                 DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_NONE,
                                 CS40L25_POLL_ACK_CTRL_MAX,
                                 CS40L25_POLL_ACK_CTRL_MS);

    if (ret != REGMAP_STATUS_OK)
    {
        return CS40L25_STATUS_FAIL;
    }

    if (i2s_passthrough)
    {
        ret = regmap_write_acked_reg(cp,
                                     DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_5_REG,
                                     DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_5_STOP_I2S,
                                     DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_5_NONE,
                                     CS40L25_POLL_ACK_CTRL_MAX,
                                     CS40L25_POLL_ACK_CTRL_MS);
        if (ret != REGMAP_STATUS_OK)
        {
            return CS40L25_STATUS_FAIL;
        }
    }

    //Disable ASP
    cs40l25_dataif_asp_enables1_t asp_reg_val;
    regmap_read(cp, DATAIF_ASP_ENABLES1_REG, &(asp_reg_val.word));
    asp_reg_val.asp_rx1_en = 0;
    asp_reg_val.asp_rx2_en = 0;
    cs40l25_write_wseq_reg(driver, DATAIF_ASP_ENABLES1_REG, asp_reg_val.word);

    if (!i2s_passthrough)
    {
        cs40l25_write_wseq_reg(driver, CS40L25_MIXER_DACPCM1_INPUT_REG, CS40L25_INPUT_SRC_DSP1TX1);
    }

    return CS40L25_STATUS_OK;
}

/**
 * Enable the VAMP Discharge via the CS40L25
 *
 */
uint32_t cs40l25_enable_vamp_discharge(cs40l25_t *driver, bool is_enable)
{
    uint32_t ret;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    // If External Boost is used, then first send DISCHARGE command
    if (driver->config.ext_boost.use_ext_boost)
    {
        if (is_enable)
        {
            ret = regmap_write_fw_control(cp, driver->fw_info, CS40L25_SYM_FIRMWARE_USER_CONTROL_IPDATA, 0);
        }
        else
        {
            ret = regmap_write_fw_control(cp, driver->fw_info, CS40L25_SYM_FIRMWARE_USER_CONTROL_IPDATA, 1);
        }

        if (ret != CS40L25_STATUS_OK)
        {
            return CS40L25_STATUS_FAIL;
        }

        ret = regmap_write_acked_reg(cp,
                                     DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_5_REG,
                                     DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_5_DISCHARGE_VAMP,
                                     DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_5_NONE,
                                     CS40L25_POLL_ACK_CTRL_MAX,
                                     CS40L25_POLL_ACK_CTRL_MS);
    }
    else
    {
        ret = CS40L25_STATUS_FAIL;
    }

    return ret;
}

/*
 * Reads the contents of a single register/memory address
 *
 */
uint32_t cs40l25_read_reg(cs40l25_t *driver, uint32_t addr, uint32_t *val)
{
    uint32_t ret;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    ret = regmap_read(cp, addr, val);
    if (ret)
    {
        return CS40L25_STATUS_FAIL;
    }

    return CS40L25_STATUS_OK;
}

/*
 * Writes the contents of a single register/memory address
 *
 */
uint32_t cs40l25_write_reg(cs40l25_t *driver, uint32_t addr, uint32_t val)
{
    uint32_t ret;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    ret = regmap_write(cp, addr, val);
    if (ret)
    {
        return CS40L25_STATUS_FAIL;
    }

    return CS40L25_STATUS_OK;
}

/*!
 * \mainpage Introduction
 *
 * This document outlines the driver source code included in the MCU Driver Software Package for the CS40L25 Boosted
 * Haptics Driver.  This guide is primarily intended for those involved in end-system implementation, integration, and
 * testing, who will use the CS40L25 MCU Driver Software Package to integrate the CS40L25 driver source code into the
 * end-system's host MCU software.  After reviewing this guide, the reader will be able to begin software integration
 * of the CS40L25 MCU driver and then have the ability to initialize, reset, boot, configure, and service events from
 * the CS40L25.  This guide should be used along with the CS40L25 Datasheet.
 *
 *  In order to obtain any additional materials, and for any questions regarding this guide, the MCU Driver
 *  Software Package, or CS40L25 system integration, please contact your Cirrus Logic Representative.
 */
