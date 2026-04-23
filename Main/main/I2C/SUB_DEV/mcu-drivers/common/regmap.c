/**
 * @file regmap.c
 *
 * @brief Generic regmap operations implementation.
 *
 * @copyright
 * Copyright (c) Cirrus Logic 2021, 2023 All Rights Reserved, http://www.cirrus.com/
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
#include "I2C/SUB_DEV/mcu-drivers/common/regmap.h"

/***********************************************************************************************************************
 * LOCAL LITERAL SUBSTITUTIONS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * MACROS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * LOCAL VARIABLES
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * GLOBAL VARIABLES
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * LOCAL FUNCTIONS
 **********************************************************************************************************************/
static uint32_t regmap_virtual_read(regmap_cp_config_t *cp, uint32_t addr, uint32_t *val)
{
    uint32_t ret = BSP_STATUS_FAIL;
    regmap_virtual_register_t *regfile = (regmap_virtual_register_t *) cp->dev_id;
    uint16_t regfile_length = cp->receive_max;

    for (uint16_t i = 0; i < regfile_length; i++)
    {
        if (regfile->address == addr)
        {
            if (regfile->on_read == NULL)
            {
                *val = regfile->default_value;
                ret = BSP_STATUS_OK;
            }
            else
            {
                ret = regfile->on_read((void *) regfile, val);
            }

            break;
        }

        regfile++;
    }

    return ret;
}

static uint32_t regmap_virtual_write(regmap_cp_config_t *cp, uint32_t addr, uint32_t val)
{
    uint32_t ret = BSP_STATUS_FAIL;
    regmap_virtual_register_t *regfile = (regmap_virtual_register_t *) cp->dev_id;
    uint16_t regfile_length = cp->receive_max;

    for (uint16_t i = 0; i < regfile_length; i++)
    {
        if (regfile->address == addr)
        {
            if (regfile->on_write != NULL)
            {
                ret = regfile->on_write((void *) regfile, val);
            }

            break;
        }

        regfile++;
    }

    return ret;
}

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/

/**
 * Reads the contents of a single register/memory address
 *
 */
uint32_t regmap_read(regmap_cp_config_t *cp, uint32_t addr, uint32_t *val)
{
    uint32_t ret = REGMAP_STATUS_FAIL;
    uint8_t write_buffer[4];
    uint8_t read_buffer[4] = {0};

    *val = 0;

    // This ESP32 port is I2C-only for physical devices.
    switch (cp->bus_type)
    {
        case REGMAP_BUS_TYPE_I2C:
            write_buffer[0] = GET_BYTE_FROM_WORD(addr, 3);
            write_buffer[1] = GET_BYTE_FROM_WORD(addr, 2);
            write_buffer[2] = GET_BYTE_FROM_WORD(addr, 1);
            write_buffer[3] = GET_BYTE_FROM_WORD(addr, 0);

            ret = bsp_driver_if_g->i2c_read_repeated_start(cp->dev_id, write_buffer, 4, read_buffer, 4, NULL, NULL);

            if (BSP_STATUS_OK == ret)
            {
                ADD_BYTE_TO_WORD(*val, read_buffer[0], 3);
                ADD_BYTE_TO_WORD(*val, read_buffer[1], 2);
                ADD_BYTE_TO_WORD(*val, read_buffer[2], 1);
                ADD_BYTE_TO_WORD(*val, read_buffer[3], 0);

                ret = REGMAP_STATUS_OK;
            }
            break;

        // case REGMAP_BUS_TYPE_SPI:
        //     write_buffer[0] = GET_BYTE_FROM_WORD(addr, 3);
        //     write_buffer[1] = GET_BYTE_FROM_WORD(addr, 2);
        //     write_buffer[2] = GET_BYTE_FROM_WORD(addr, 1);
        //     write_buffer[3] = GET_BYTE_FROM_WORD(addr, 0);
        //     // Set the R/W bit
        //     write_buffer[0] = 0x80 | write_buffer[0];
        //     ret = bsp_driver_if_g->spi_read(cp->dev_id, write_buffer, 4, read_buffer, 4, cp->spi_pad_len);

        //     if (BSP_STATUS_OK == ret)
        //     {
        //         ADD_BYTE_TO_WORD(*val, read_buffer[0], 3);
        //         ADD_BYTE_TO_WORD(*val, read_buffer[1], 2);
        //         ADD_BYTE_TO_WORD(*val, read_buffer[2], 1);
        //         ADD_BYTE_TO_WORD(*val, read_buffer[3], 0);

        //         ret = REGMAP_STATUS_OK;
        //     }
        //     break;

        // case REGMAP_BUS_TYPE_SPI_3000:
        //     write_buffer[0] = GET_BYTE_FROM_WORD(addr, 3);
        //     write_buffer[1] = GET_BYTE_FROM_WORD(addr, 2);
        //     write_buffer[2] = GET_BYTE_FROM_WORD(addr, 1);
        //     write_buffer[3] = GET_BYTE_FROM_WORD(addr, 0);

        //     // Set the R/W bit
        //     write_buffer[0] = 0x80 | write_buffer[0];

        //     if (addr < 0x3000)
        //     {
        //         ret = bsp_driver_if_g->spi_read(cp->dev_id,
        //                                         write_buffer,
        //                                         4,
        //                                         read_buffer,
        //                                         2,
        //                                         cp->spi_pad_len);

        //         ADD_BYTE_TO_WORD(*val, read_buffer[0], 1);
        //         ADD_BYTE_TO_WORD(*val, read_buffer[1], 0);
        //     }
        //     else
        //     {

        //         ret = bsp_driver_if_g->spi_read(cp->dev_id,
        //                                         write_buffer,
        //                                         4,
        //                                         read_buffer,
        //                                         4,
        //                                         cp->spi_pad_len);

        //         ADD_BYTE_TO_WORD(*val, read_buffer[0], 3);
        //         ADD_BYTE_TO_WORD(*val, read_buffer[1], 2);
        //         ADD_BYTE_TO_WORD(*val, read_buffer[2], 1);
        //         ADD_BYTE_TO_WORD(*val, read_buffer[3], 0);
        //     }

        //     break;

        case REGMAP_BUS_TYPE_VIRTUAL:
            ret = regmap_virtual_read(cp, addr, val);
            break;

        default:
            break;
    }

    if (ret)
    {
        ret = REGMAP_STATUS_FAIL;
    }
    else
    {
        ret = REGMAP_STATUS_OK;
    }

    return ret;
}

/**
 * Writes the contents of a single register/memory address
 *
 */
uint32_t regmap_write(regmap_cp_config_t *cp, uint32_t addr, uint32_t val)
{
    uint32_t ret = REGMAP_STATUS_FAIL;
    uint8_t write_buffer[8];

    switch (cp->bus_type)
    {
        case REGMAP_BUS_TYPE_I2C:
            write_buffer[0] = GET_BYTE_FROM_WORD(addr, 3);
            write_buffer[1] = GET_BYTE_FROM_WORD(addr, 2);
            write_buffer[2] = GET_BYTE_FROM_WORD(addr, 1);
            write_buffer[3] = GET_BYTE_FROM_WORD(addr, 0);
            write_buffer[4] = GET_BYTE_FROM_WORD(val, 3);
            write_buffer[5] = GET_BYTE_FROM_WORD(val, 2);
            write_buffer[6] = GET_BYTE_FROM_WORD(val, 1);
            write_buffer[7] = GET_BYTE_FROM_WORD(val, 0);

            ret = bsp_driver_if_g->i2c_write(cp->dev_id, write_buffer, 8, NULL, NULL);
            break;

        case REGMAP_BUS_TYPE_SPI:
        case REGMAP_BUS_TYPE_SPI_3000:
            // SPI is intentionally not implemented in this project port.
            break;

        case REGMAP_BUS_TYPE_VIRTUAL:
            ret = regmap_virtual_write(cp, addr, val);
            break;

        default:
            break;
    }

    if (ret)
    {
        ret = REGMAP_STATUS_FAIL;
    }
    else
    {
        ret = REGMAP_STATUS_OK;
    }

    return ret;
}

/**
 * Read-Modify-Write of register using 32-bit mask
 *
 */
uint32_t regmap_update_reg(regmap_cp_config_t *cp, uint32_t addr, uint32_t mask, uint32_t val)
{
    uint32_t ret = REGMAP_STATUS_FAIL;
    uint32_t temp_val, data;

    ret = regmap_read(cp, addr, &data);

    if (ret)
    {
        return ret;
    }

    temp_val = data & ~(mask);
    temp_val |= val;

    if (data == temp_val)
    {
        return REGMAP_STATUS_OK;
    }

    ret = regmap_write(cp, addr, temp_val);

    return ret;
}

/**
 * Reads a register for a specific value for a specified amount of tries while waiting between reads.
 *
 */
uint32_t regmap_poll_reg(regmap_cp_config_t *cp, uint32_t addr, uint32_t val, uint8_t tries, uint32_t delay)
{
    uint32_t tmp, ret;

    for (int i = 0; i < tries; i++)
    {
        ret = regmap_read(cp, addr, &tmp);

        if (ret)
        {
          return ret;
        }

        if (tmp == val)
        {
          return REGMAP_STATUS_OK;;
        }

        bsp_driver_if_g->set_timer(delay, NULL, NULL);
    }

    return REGMAP_STATUS_FAIL;
}

/**
 * Write a value to a register and poll for an updated value
 *
 */
uint32_t regmap_write_acked_reg(regmap_cp_config_t *cp,
                                uint32_t addr,
                                uint32_t val,
                                uint32_t acked_val,
                                uint8_t tries,
                                uint32_t delay)
{
    uint32_t ret;

    ret = regmap_write(cp, addr, val);

    if (ret)
    {
        return REGMAP_STATUS_FAIL;
    }

    for (uint8_t i = 0 ; i < tries; i++)
    {
        uint32_t temp_reg_val;

        bsp_driver_if_g->set_timer(delay, NULL, NULL);

        regmap_read(cp, addr, &temp_reg_val);

        if (temp_reg_val == acked_val)
        {
            return REGMAP_STATUS_OK;
        }
    }

    return REGMAP_STATUS_FAIL;
}

/**
 * Reads contents from a consecutive number of memory addresses
 *
 */
uint32_t regmap_read_block(regmap_cp_config_t *cp, uint32_t addr, uint8_t *bytes, uint32_t length)
{
    uint32_t ret = REGMAP_STATUS_FAIL;
    uint8_t write_buffer[4];

    switch (cp->bus_type)
    {
        case REGMAP_BUS_TYPE_I2C:
            write_buffer[0] = GET_BYTE_FROM_WORD(addr, 3);
            write_buffer[1] = GET_BYTE_FROM_WORD(addr, 2);
            write_buffer[2] = GET_BYTE_FROM_WORD(addr, 1);
            write_buffer[3] = GET_BYTE_FROM_WORD(addr, 0);

            ret = bsp_driver_if_g->i2c_read_repeated_start(cp->dev_id, write_buffer, 4, bytes, length, NULL, NULL);
            break;

        // case REGMAP_BUS_TYPE_SPI:
        // case REGMAP_BUS_TYPE_SPI_3000:
        //     write_buffer[0] = GET_BYTE_FROM_WORD(addr, 3);
        //     write_buffer[1] = GET_BYTE_FROM_WORD(addr, 2);
        //     write_buffer[2] = GET_BYTE_FROM_WORD(addr, 1);
        //     write_buffer[3] = GET_BYTE_FROM_WORD(addr, 0);

        //     // Set the R/W bit
        //     write_buffer[0] = 0x80 | write_buffer[0];
        //     ret = bsp_driver_if_g->spi_read(cp->dev_id, write_buffer, 4, bytes, length, cp->spi_pad_len);

        //     break;

        case REGMAP_BUS_TYPE_VIRTUAL:
            // 'length' is in bytes, so /4 to get in terms of 32-bit words
            for (uint32_t i = 0; i < (length >> 2); i++)
            {
                ret = regmap_virtual_read(cp, addr, (uint32_t *) bytes);
                if (ret)
                {
                    break;
                }

                addr += 4;
                bytes += 4;
            }
            break;

        default:
            break;
    }

    if (ret)
    {
        ret = REGMAP_STATUS_FAIL;
    }
    else
    {
        ret = REGMAP_STATUS_OK;
    }

    return ret;
}

/**
 * Writes from byte array to consecutive number of Control Port memory addresses
 *
 */
uint32_t regmap_write_block(regmap_cp_config_t *cp, uint32_t addr, uint8_t *bytes, uint32_t length)
{
    uint32_t ret = REGMAP_STATUS_FAIL;
    uint8_t write_buffer[4];

    switch (cp->bus_type)
    {
        case REGMAP_BUS_TYPE_I2C:
            write_buffer[0] = GET_BYTE_FROM_WORD(addr, 3);
            write_buffer[1] = GET_BYTE_FROM_WORD(addr, 2);
            write_buffer[2] = GET_BYTE_FROM_WORD(addr, 1);
            write_buffer[3] = GET_BYTE_FROM_WORD(addr, 0);

            ret = bsp_driver_if_g->i2c_db_write(cp->dev_id, write_buffer, 4, bytes, length, NULL, NULL);
            break;

        case REGMAP_BUS_TYPE_SPI:
        case REGMAP_BUS_TYPE_SPI_3000:
            // SPI is intentionally not implemented in this project port.
            break;

        case REGMAP_BUS_TYPE_VIRTUAL:
            // 'length' is in bytes, so /4 to get in terms of 32-bit words
            for (uint32_t i = 0; i < (length >> 2); i++)
            {
                ret = regmap_virtual_write(cp, addr, (uint32_t) *bytes);
                if (ret)
                {
                    break;
                }

                addr += 4;
                bytes += 4;
            }
            break;

        default:
            break;
    }

    if (ret)
    {
        ret = REGMAP_STATUS_FAIL;
    }
    else
    {
        ret = REGMAP_STATUS_OK;
    }

    return ret;
}

/**
 * Writes a value in a list to corresponding address. Data can be encoded to perform specific operations.
 *
 */
uint32_t regmap_write_array(regmap_cp_config_t *cp, uint32_t * array, uint32_t array_len)
{
    uint32_t ret;
    for (uint32_t i = 0; i < array_len;)
    {
        switch (array[i])
        {
            case REGMAP_ARRAY_RMODW:
                ret = regmap_update_reg(cp, array[i + 1], array[i + 3], array[i + 2]);
                if (ret)
                {
                    return REGMAP_STATUS_FAIL;
                }
                i += 4;
                break;

            case REGMAP_ARRAY_BLOCK_WRITE:
                ret = regmap_write_block(cp, array[i + 1], (uint8_t *) &array[i + 3], array[i + 2] * 4);
                if (ret)
                {
                    return REGMAP_STATUS_FAIL;
                }
                i += array[i + 2] + 3;
                break;

            case REGMAP_ARRAY_DELAY:
                bsp_driver_if_g->set_timer(array[i + 1], NULL, NULL);
                i += 2;
                break;

            default:
                ret = regmap_write(cp, array[i], array[i + 1]);
                if (ret)
                {
                    return REGMAP_STATUS_FAIL;
                }
                i += 2;
                break;
        }
    }
    return REGMAP_STATUS_OK;
}

/**
 * Reads a firmware control corresponding to the respective symbol_id.
 *
 */
uint32_t regmap_read_fw_control(regmap_cp_config_t *cp, fw_img_info_t *f, uint32_t symbol_id, uint32_t *val)
{
    uint32_t temp_reg_addr, ret;

    temp_reg_addr = fw_img_find_symbol(f, symbol_id);

    if (temp_reg_addr == 0)
    {
        return REGMAP_STATUS_FAIL;
    }

    ret = regmap_read(cp, temp_reg_addr, val);

    if (ret)
    {
        return REGMAP_STATUS_FAIL;
    }

    return REGMAP_STATUS_OK;
}

/**
 * Writes a firmware control corresponding to the respective symbol_id.
 *
 */
uint32_t regmap_write_fw_control(regmap_cp_config_t *cp, fw_img_info_t *f, uint32_t symbol_id, uint32_t val)
{
    uint32_t temp_reg_addr, ret;

    temp_reg_addr = fw_img_find_symbol(f, symbol_id);

    if (temp_reg_addr == 0)
    {
        return REGMAP_STATUS_FAIL;
    }

    ret = regmap_write(cp, temp_reg_addr, val);

    if (ret)
    {
        return REGMAP_STATUS_FAIL;
    }

    return REGMAP_STATUS_OK;
}

/**
 * Updates bitfields in a firmware control corresponding to the respective symbol_id.
 *
 */
uint32_t regmap_update_fw_control(regmap_cp_config_t *cp,
                                  fw_img_info_t *f,
                                  uint32_t symbol_id,
                                  uint32_t mask,
                                  uint32_t val)
{
    uint32_t temp_reg_addr;

    temp_reg_addr = fw_img_find_symbol(f, symbol_id);

    if (temp_reg_addr == 0)
    {
        return REGMAP_STATUS_FAIL;
    }

    return regmap_update_reg(cp, temp_reg_addr, mask, val);
}

/**
 * Reads a firmware control for a specific bit for a specified amount of tries while waiting between reads.
 *
 */
uint32_t regmap_poll_fw_control(regmap_cp_config_t *cp,
                                fw_img_info_t *f,
                                uint32_t symbol_id,
                                uint32_t val,
                                uint8_t tries,
                                uint32_t delay)
{
    uint32_t temp_reg_val, temp_reg_addr;
    uint32_t ret = REGMAP_STATUS_FAIL;

    temp_reg_addr = fw_img_find_symbol(f, symbol_id);
    temp_reg_val = ~val;

    if (temp_reg_addr != 0)
    {
        for (uint8_t i = 0; i < tries; i++)
        {
            regmap_read(cp, temp_reg_addr, &temp_reg_val);

            if (temp_reg_val == val)
            {
                ret = REGMAP_STATUS_OK;
                break;
            }

            bsp_driver_if_g->set_timer(delay, NULL, NULL);
        }
    }

    return ret;
}

/**
 * Write a value to a firmware control and poll for an updated value
 *
 */
uint32_t regmap_write_acked_fw_control(regmap_cp_config_t *cp,
                                       fw_img_info_t *f,
                                       uint32_t symbol_id,
                                       uint32_t val,
                                       uint32_t acked_val,
                                       uint8_t tries,
                                       uint32_t delay)
{
    uint32_t temp_reg_addr;

    temp_reg_addr = fw_img_find_symbol(f, symbol_id);

    if (temp_reg_addr == 0)
    {
        return REGMAP_STATUS_FAIL;
    }

    return regmap_write_acked_reg(cp, temp_reg_addr, val, acked_val, tries, delay);
}

/**
 * Writes from word array to a firmware control array corresponding to a symbol_id.
 *
 */
uint32_t regmap_write_fw_vals(regmap_cp_config_t *cp,
                               fw_img_info_t *f,
                               uint32_t symbol_id,
                               uint32_t *val,
                               uint32_t length)
{
    uint32_t temp_reg_addr, ret;

    temp_reg_addr = fw_img_find_symbol(f, symbol_id);

    if (temp_reg_addr == 0)
    {
        return REGMAP_STATUS_FAIL;
    }

    for (uint32_t i = 0; i < length; i++)
    {

        ret = regmap_write(cp, (temp_reg_addr + (i * 4)), val[i]);

        if (ret)
        {
            return REGMAP_STATUS_FAIL;
        }
    }

    return REGMAP_STATUS_OK;
}
