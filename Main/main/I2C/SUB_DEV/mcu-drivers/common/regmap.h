/**
 * @file regmap.h
 *
 * @brief
 * Generic regmap interface.
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
#ifndef REGMAP_H
#define REGMAP_H

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include "I2C/SUB_DEV/mcu-drivers/common/bsp_driver_if.h"
#include "I2C/SUB_DEV/mcu-drivers/common/fw_img.h"

/***********************************************************************************************************************
 * LITERALS & CONSTANTS
 **********************************************************************************************************************/

/**
 * @defgroup REGMAP_STATUS_
 * @brief Return values for all public API calls
 *
 * @{
 */
#define REGMAP_STATUS_OK                   (0)
#define REGMAP_STATUS_FAIL                 (1)
/** @} */

/**
 * @defgroup REGMAP_BUS_TYPE_
 * @brief Types of control port bus supported
 *
 * @{
 */
#define REGMAP_BUS_TYPE_I2C                (0)
#define REGMAP_BUS_TYPE_SPI                (1)
#define REGMAP_BUS_TYPE_SPI_3000           (2)
#define REGMAP_BUS_TYPE_VIRTUAL            (3)
/** @} */

/**
 * @defgroup REGMAP_WRITE_ARRAY_TYPE_
 * @brief Types of arrays supported for regmap_write_array
 *
 * @{
 */
#define REGMAP_WRITE_ARRAY_TYPE_ADDR_VAL   (0)
/** @} */

/**
 * @defgroup REGMAP_ARRAY_
 * @brief Types of arrays supported for regmap_write_array
 *
 * @{
 */
#define REGMAP_ARRAY_RMODW                 (0x80000001)
#define REGMAP_ARRAY_BLOCK_WRITE           (0x80000002)
#define REGMAP_ARRAY_DELAY                 (0x80000003)
/** @} */

/***********************************************************************************************************************
 * MACROS
 **********************************************************************************************************************/

/**
 * Macro for getting pointer to Control Port config from driver handle
 */
#define REGMAP_GET_CP(A)            (&((A)->config.bsp_config.cp_config))

/***********************************************************************************************************************
 * ENUMS, STRUCTS, UNIONS, TYPEDEFS
 **********************************************************************************************************************/

/**
 * Control port configuration for regmap API calls
 */
typedef struct
{
    uint32_t dev_id;                                    ///< Used to ID device in bsp_driver_if calls
    uint8_t bus_type;                                   ///< Control Port type - I2C or SPI
    uint16_t receive_max;                               ///< Number of bytes available in receive buffer
    uint32_t spi_pad_len;                               ///< Number of bytes to pad for SPI transactions
} regmap_cp_config_t;

typedef uint32_t (*regmap_vread_t)(void *self, uint32_t *val);
typedef uint32_t (*regmap_vwrite_t)(void *self, uint32_t val);

typedef struct
{
    const uint32_t address;
    const uint32_t default_value;
    uint32_t value;
    regmap_vread_t on_read;
    regmap_vwrite_t on_write;
} regmap_virtual_register_t;

/***********************************************************************************************************************
 * GLOBAL VARIABLES
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/

/**
 * Reads the contents of a single register/memory address
 *
 * The main purpose is to handle buffering and BSP calls required for reading a single memory address.
 *
 * @param [in] cp               Pointer to the BSP control port configuration
 * @param [in] addr             32-bit address to be read
 * @param [out] val             Pointer to register value read
 *
 * @return
 * - REGMAP_STATUS_FAIL         if the call to BSP failed
 * - REGMAP_STATUS_OK           otherwise
 *
 */
uint32_t regmap_read(regmap_cp_config_t *cp, uint32_t addr, uint32_t *val);

/**
 * Writes the contents of a single register/memory address
 *
 * The main purpose is to handle buffering and BSP calls required for writing a single memory address.
 *
 * @param [in] cp               Pointer to the BSP control port configuration
 * @param [in] addr             32-bit address to be written
 * @param [in] val              32-bit value to be written
 *
 * @return
 * - REGMAP_STATUS_FAIL         if the call to BSP failed
 * - REGMAP_STATUS_OK           otherwise
 *
 */
uint32_t regmap_write(regmap_cp_config_t *cp, uint32_t addr, uint32_t val);

/**
 * Read-Modify-Write of register using 32-bit mask
 *
 * The main purpose is to handle buffering and BSP calls required for RMW a single memory address.
 *
 * @param [in] cp               Pointer to the BSP control port configuration
 * @param [in] addr             32-bit address to be read
 * @param [in] mask             32-bit mask for bits to be modified
 * @param [in] val              32-bit value to be written
 *
 * @return
 * - REGMAP_STATUS_FAIL         if the call to BSP failed
 * - REGMAP_STATUS_OK           otherwise
 *
 */
uint32_t regmap_update_reg(regmap_cp_config_t *cp, uint32_t addr, uint32_t mask, uint32_t val);

/**
 * Reads a register for a specific value for a specified amount of tries while waiting between reads.
 *
 * @param [in] cp               Pointer to the BSP control port configuration
 * @param [in] addr             Address to read from.
 * @param [in] val              Value to compare the read value to.
 * @param [in] tries            How many times to read the address.
 * @param [in] delay            How long to delay between each read.
 *
 * @return
 * - REGMAP_STATUS_FAIL         if the call to BSP failed or if value not polled
 *                              within the amount of tries
 * - REGMAP_STATUS_OK           otherwise
 */
uint32_t regmap_poll_reg(regmap_cp_config_t *cp, uint32_t addr, uint32_t val, uint8_t tries, uint32_t delay);

/**
 * Write a value to a register and poll for an updated value
 *
 * @param [in] cp               Pointer to the BSP control port configuration
 * @param [in] addr             Address to read from.
 * @param [in] val              32-bit value to be written
 * @param [in] acked_val        Value to poll for after writing 'val'
 * @param [in] tries            How many times to read the address.
 * @param [in] delay            How long to delay between each read.
 *
 * @return
 * - REGMAP_STATUS_FAIL         if the call to BSP failed or if value not polled
 *                              within the amount of tries
 * - REGMAP_STATUS_OK           otherwise
 */
uint32_t regmap_write_acked_reg(regmap_cp_config_t *cp,
                                uint32_t addr,
                                uint32_t val,
                                uint32_t acked_val,
                                uint8_t tries,
                                uint32_t delay);

/**
 * Reads contents from a consecutive number of memory addresses
 *
 * Starting at 'addr', this will read 'length' number of 32-bit values into the BSP-allocated buffer from the
 * control port.  This bulk read will place contents into the BSP buffer starting at the 4th byte address.
 * Bytes 0-3 in the buffer are reserved for non-bulk reads (i.e. calls to cs35l41_read_reg).
 *
 * @param [in] cp               Pointer to the BSP control port configuration
 * @param [in] addr             32-bit address to be read
 * @param [in] bytes            pointer to 8-bit buffer to be used for reading
 * @param [in] length           number of memory addresses (i.e. 32-bit words) to read
 *
 * @return
 * - REGMAP_STATUS_FAIL         if the call to BSP failed, or if 'length' exceeds the size of BSP buffer
 * - REGMAP_STATUS_OK           otherwise
 *
 */
uint32_t regmap_read_block(regmap_cp_config_t *cp, uint32_t addr, uint8_t *bytes, uint32_t length);

/**
 * Writes from byte array to consecutive number of Control Port memory addresses
 *
 * @param [in] cp               Pointer to the BSP control port configuration
 * @param [in] addr             32-bit address to be read
 * @param [in] bytes            pointer to array of bytes to write via Control Port bus
 * @param [in] length           number of bytes to write
 *
 * @return
 * - REGMAP_STATUS_FAIL         if the call to BSP failed
 * - REGMAP_STATUS_OK           otherwise
 *
 */
uint32_t regmap_write_block(regmap_cp_config_t *cp, uint32_t addr, uint8_t *bytes, uint32_t length);

/**
 * Writes a value in a list to corresponding address. Data can be encoded to perform specific operations.
 *
 * @param [in] cp               Pointer to the BSP control port configuration
 * @param [in] array            Pointer to value list.
 * @param [in] array_len        Size of array list.
 *
 * @return
 * - REGMAP_STATUS_FAIL         if the call to BSP failed
 * - REGMAP_STATUS_OK           otherwise
 *
 */
uint32_t regmap_write_array(regmap_cp_config_t *cp, uint32_t *array, uint32_t array_len);

/**
 * Reads a firmware control corresponding to the respective symbol_id.
 *
 * @param [in] cp               Pointer to the BSP control port configuration
 * @param [in] f                Pointer to fw_img_info struct
 * @param [in] symbol_id        id to a specific register address
 * @param [out] val             Pointer to val which will contain the read value of the register
 *
 * @return
 * - REGMAP_STATUS_FAIL         if the call to BSP failed
 * - REGMAP_STATUS_OK           otherwise
 *
 */
uint32_t regmap_read_fw_control(regmap_cp_config_t *cp, fw_img_info_t *f, uint32_t symbol_id, uint32_t *val);

/**
 * Writes a firmware control corresponding to the respective symbol_id.
 *
 * @param [in] cp               Pointer to the BSP control port configuration
 * @param [in] f                Pointer to fw_img_info struct
 * @param [in] symbol_id        id to a specific register address
 * @param [in] val              Value to be written to register
 *
 * @return
 * - REGMAP_STATUS_FAIL         if the call to BSP failed
 * - REGMAP_STATUS_OK           otherwise
 *
 */
uint32_t regmap_write_fw_control(regmap_cp_config_t *cp, fw_img_info_t *f, uint32_t symbol_id, uint32_t val);

/**
 * Updates bitfields in a firmware control corresponding to the respective symbol_id.
 *
 * @param [in] cp               Pointer to the BSP control port configuration
 * @param [in] f                Pointer to fw_img_info struct
 * @param [in] symbol_id        id to a specific register address
 * @param [in] mask             32-bit mask for bits to be modified
 * @param [in] val              Value to be written to register
 *
 * @return
 * - REGMAP_STATUS_FAIL         if the call to BSP failed
 * - REGMAP_STATUS_OK           otherwise
 *
 */
uint32_t regmap_update_fw_control(regmap_cp_config_t *cp,
                                  fw_img_info_t *f,
                                  uint32_t symbol_id,
                                  uint32_t mask,
                                  uint32_t val);

/**
 * Reads a firmware control for a specific bit for a specified amount of tries while waiting between reads.
 *
 * @param [in] cp               Pointer to the BSP control port configuration
 * @param [in] f                Pointer to fw_img_info struct
 * @param [in] symbol_id        id to a specific register address
 * @param [in] val              What value to compare the read value to.
 * @param [in] tries            How many times to read the address.
 * @param [in] delay            How long to delay between each read.
 *
 * @return
 * - REGMAP_STATUS_FAIL         if the call to BSP failed or if value not polled
 *                              within the amount of tries
 * - REGMAP_STATUS_OK           otherwise
 *
 */
uint32_t regmap_poll_fw_control(regmap_cp_config_t *cp,
                                fw_img_info_t *f,
                                uint32_t symbol_id,
                                uint32_t val,
                                uint8_t tries,
                                uint32_t delay);

/**
 * Write a value to a firmware control and poll for an updated value
 *
 * @param [in] cp               Pointer to the BSP control port configuration
 * @param [in] f                Pointer to fw_img_info struct
 * @param [in] symbol_id        id to a specific register address
 * @param [in] val              32-bit value to be written
 * @param [in] acked_val        Value to poll for after writing 'val'
 * @param [in] tries            How many times to read the address.
 * @param [in] delay            How long to delay between each read.
 *
 * @return
 * - REGMAP_STATUS_FAIL         if the call to BSP failed or if value not polled
 *                              within the amount of tries
 * - REGMAP_STATUS_OK           otherwise
 */
uint32_t regmap_write_acked_fw_control(regmap_cp_config_t *cp,
                                       fw_img_info_t *f,
                                       uint32_t symbol_id,
                                       uint32_t val,
                                       uint32_t acked_val,
                                       uint8_t tries,
                                       uint32_t delay);

/**
 * Writes from word array to a firmware control array corresponding to a symbol_id.
 *
 * @param [in] cp               Pointer to the BSP control port configuration
 * @param [in] f                Pointer to fw_img_info struct
 * @param [in] symbol_id        id to a specific register address
 * @param [out] val             Pointer to 32-bit word value list.
 * @param [in] size             Size of val list.
 *
 * @return
 * - REGMAP_STATUS_FAIL         if the call to BSP failed
 * - REGMAP_STATUS_OK           otherwise
 *
 */
uint32_t regmap_write_fw_vals(regmap_cp_config_t *cp,
                              fw_img_info_t *f,
                              uint32_t symbol_id,
                              uint32_t *val,
                              uint32_t size);

/**********************************************************************************************************************/
#ifdef __cplusplus
}
#endif

#endif // REGMAP_H
