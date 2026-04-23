/**
 * @file cs40l25_ext.c
 *
 * @brief The CS40L25 Driver Extended API module
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
#include "I2C/SUB_DEV/mcu-drivers/cs40l25/cs40l25_ext.h"
#include "I2C/SUB_DEV/mcu-drivers/common/bsp_driver_if.h"

/***********************************************************************************************************************
 * LOCAL LITERAL SUBSTITUTIONS
 **********************************************************************************************************************/

#define CS40L25_DYNAMIC_F0_TABLE_SIZE           (20)
#define CS40L25_POLL_DYNAMIC_REDC_TOTAL         (30)

#define CS40L25_COMPENSATION_ENABLE_F0_MASK     (1 << 0)
#define CS40L25_COMPENSATION_ENABLE_REDC_MASK   (1 << 1)

/***********************************************************************************************************************
 * LOCAL VARIABLES
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * LOCAL FUNCTIONS
 **********************************************************************************************************************/

/**
 * Get the HALO HEARTBEAT
 *
 */
uint32_t cs40l25_get_halo_heartbeat(cs40l25_t *driver, uint32_t *hb)
{
    uint32_t ret = CS40L25_STATUS_FAIL;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    if (hb == NULL)
    {
        return ret;
    }

    if (driver->state == CS40L25_STATE_POWER_UP)
    {
        ret = regmap_read(cp, DSP_BHM_HALO_HEARTBEAT_REG, hb);
    }
    else if (driver->state == CS40L25_STATE_CAL_POWER_UP)
    {
        ret = regmap_read_fw_control(cp, driver->fw_info, CS40L25_SYM_FIRMWARE_HALO_HEARTBEAT, hb);
    }
    else if (driver->state == CS40L25_STATE_DSP_POWER_UP)
    {
        ret = regmap_read_fw_control(cp, driver->fw_info, CS40L25_CAL_SYM_FIRMWARE_HALO_HEARTBEAT, hb);
    }

    return ret;
}

/**
 * Update the HALO FW Haptic Configuration
 *
 */
uint32_t cs40l25_update_haptic_config(cs40l25_t *driver, cs40l25_haptic_config_t *config)
{
    uint32_t ret;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    if (config == NULL)
    {
        return CS40L25_STATUS_FAIL;
    }

    ret = regmap_write_fw_control(cp, driver->fw_info, CS40L25_SYM_FIRMWARE_GPIO_ENABLE, 0);
    if (ret)
    {
        return ret;
    }

    ret = regmap_write_fw_control(cp, driver->fw_info, CS40L25_SYM_FIRMWARE_GAIN_CONTROL, config->gain_control.word);
    if (ret)
    {
        return ret;
    }

    ret = regmap_write_fw_vals(cp,
                               driver->fw_info,
                               CS40L25_SYM_FIRMWARE_INDEXBUTTONPRESS,
                               config->index_button_press,
                               4);
    if (ret)
    {
        return ret;
    }

    ret = regmap_write_fw_vals(cp,
                               driver->fw_info,
                               CS40L25_SYM_FIRMWARE_INDEXBUTTONRELEASE,
                               config->index_button_release,
                               4);
    if (ret)
    {
        return ret;
    }

    ret = regmap_write_fw_control(cp, driver->fw_info, CS40L25_SYM_FIRMWARE_GPIO_ENABLE, config->gpio_enable.word);

    return ret;
}

/**
 * Trigger the ROM Mode (BHM) Haptic Effect
 *
 */
uint32_t cs40l25_trigger_bhm(cs40l25_t *driver)
{
    uint32_t ret;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    ret = regmap_write_acked_reg(cp,
                                 DSP_BHM_BUZZ_TRIGGER_REG,
                                 1,
                                 0,
                                 CS40L25_POLL_ACK_CTRL_MAX,
                                 CS40L25_POLL_ACK_CTRL_MS);

    return ret;
}

/**
 * Trigger RAM Mode Haptic Effects
 *
 */
uint32_t cs40l25_trigger(cs40l25_t *driver, uint32_t index, uint32_t duration_ms)
{
    uint32_t ret;
    uint32_t sym_id;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    if (duration_ms == 0)
    {
        sym_id = DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_1_REG;
    }
    else
    {
        ret = regmap_write_fw_control(cp, driver->fw_info, CS40L25_SYM_VIBEGEN_TIMEOUT_MS, duration_ms);
        if (ret)
        {
            return ret;
        }

        sym_id = DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_2_REG;
    }

    ret = regmap_write_acked_reg(cp,
                                 sym_id,
                                 index,
                                 0xFFFFFFFF,
                                 CS40L25_POLL_ACK_CTRL_MAX,
                                 CS40L25_POLL_ACK_CTRL_MS);
    return ret;
}

/**
 * Enable the HALO FW Click Compensation
 *
 */
uint32_t cs40l25_set_click_compensation_enable(cs40l25_t *driver, bool f0_enable, bool redc_enable)
{
    uint32_t ret;
    uint32_t enable = 0;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    if (f0_enable)
    {
        enable = CS40L25_COMPENSATION_ENABLE_F0_MASK;
    }

    if (redc_enable)
    {
        enable |= CS40L25_COMPENSATION_ENABLE_REDC_MASK;
    }

    ret = regmap_write_fw_control(cp, driver->fw_info, CS40L25_SYM_VIBEGEN_COMPENSATION_ENABLE, (uint32_t) enable);

    return ret;
}

/**
 * Enable the HALO FW CLAB Algorithm
 *
 */
uint32_t cs40l25_set_clab_enable(cs40l25_t *driver, bool enable)
{
#ifdef CS40L25_ALGORITHM_CLAB
    uint32_t ret;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    ret = regmap_write_fw_control(cp, driver->fw_info, CS40L25_SYM_CLAB_CLAB_ENABLED, (uint32_t) enable);

    return ret;
#else
    (void)driver;
    (void)enable;

    return CS40L25_STATUS_FAIL;
#endif
}

/**
 * Set the CLAB Peak Amplitude Control
 *
 */
uint32_t cs40l25_set_clab_peak_amplitude(cs40l25_t *driver, uint32_t amplitude)
{
#ifdef CS40L25_ALGORITHM_CLAB
    uint32_t ret;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    ret = regmap_write_fw_control(cp, driver->fw_info, CS40L25_SYM_CLAB_PEAK_AMPLITUDE_CONTROL, amplitude);

    return ret;
#else
    (void)driver;
    (void)amplitude;

    return CS40L25_STATUS_FAIL;
#endif
}

/**
 * Enable the HALO FW Dynamic F0 Algorithm
 *
 */
uint32_t cs40l25_set_dynamic_f0_enable(cs40l25_t *driver, bool enable)
{
    uint32_t ret;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    ret = regmap_write_fw_control(cp, driver->fw_info, CS40L25_SYM_DYNAMIC_F0_DYNAMIC_F0_ENABLED, (uint32_t) enable);

    return ret;
}

/**
 * Get the Dynamic F0
 *
 */
uint32_t cs40l25_get_dynamic_f0(cs40l25_t *driver, cs40l25_dynamic_f0_table_entry_t *f0_entry)
{
    uint32_t ret;
    cs40l25_dynamic_f0_table_entry_t f0_read;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);
    uint32_t reg_addr;

    if (f0_entry->index >= CS40L25_DYNAMIC_F0_TABLE_SIZE)
    {
        return CS40L25_STATUS_FAIL;
    }

    reg_addr = fw_img_find_symbol(driver->fw_info, CS40L25_SYM_DYNAMIC_F0_DYN_F0_TABLE);
    uint8_t i;
    for (i = 0; i < CS40L25_DYNAMIC_F0_TABLE_SIZE; i++)
    {
        ret = regmap_read(cp, reg_addr, &(f0_read.word));
        if (ret)
        {
            return ret;
        }

        if (f0_entry->index == f0_read.index)
        {
            f0_entry->f0 = f0_read.f0;
            break;
        }

        reg_addr += 4;
    }

    // Set to default of table entry to indicate index not found
    if (i >= CS40L25_DYNAMIC_F0_TABLE_SIZE)
    {
        f0_entry->word = CS40L25_DYNAMIC_F0_TABLE_ENTRY_DEFAULT;
    }

    return ret;
}

/**
 * Get the Dynamic ReDC
 *
 */
uint32_t cs40l25_get_dynamic_redc(cs40l25_t *driver, uint32_t *redc)
{
    uint32_t ret;
    regmap_cp_config_t *cp = REGMAP_GET_CP(driver);

    // The driver will set the dynamic_redc control to -1 (0xFFFFFF)
    ret = regmap_write_fw_control(cp, driver->fw_info, CS40L25_SYM_DYNAMIC_F0_DYNAMIC_REDC, 0xFFFFFF);
    if (ret)
    {
        return ret;
    }

    // The PowerControl (MBOX_4) register must be set to WAKEUP (2)
    ret = regmap_write_acked_reg(cp,
                                 DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_REG,
                                 CS40L25_POWERCONTROL_WAKEUP,
                                 CS40L25_POWERCONTROL_NONE,
                                 CS40L25_POLL_ACK_CTRL_MAX,
                                 CS40L25_POLL_ACK_CTRL_MS);
    if (ret)
    {
        return ret;
    }

    // Set up for polling DYNAMIC_REDC in the loop
    uint8_t i;
    for (i = 0; i < CS40L25_POLL_DYNAMIC_REDC_TOTAL; i++)
    {
        // Wait 10ms before reading again
        ret = bsp_driver_if_g->set_timer(CS40L25_POLL_ACK_CTRL_MS,
                                         NULL,
                                         NULL);
        if (ret)
        {
            return CS40L25_STATUS_FAIL;
        }

        // The dynamic_redc register contents will remain at -1 until the calculation is complete
        ret = regmap_read_fw_control(cp, driver->fw_info, CS40L25_SYM_DYNAMIC_F0_DYNAMIC_REDC, redc);
        if (ret)
        {
            return ret;
        }

        if (*redc != 0xFFFFFF)
        {
            break;
        }
    }

    if (i >= CS40L25_POLL_DYNAMIC_REDC_TOTAL)
    {
        return CS40L25_STATUS_FAIL;
    }

    return CS40L25_STATUS_OK;
}
