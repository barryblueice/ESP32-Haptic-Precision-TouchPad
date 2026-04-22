/**
 * @file bridge.h
 *
 * @brief Functions and prototypes exported by the WISCE/Studio Bridge handler
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

#ifndef BRIDGE_H
#define BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/
#include "I2C/SUB_DEV/mcu-drivers/common/regmap.h"

/***********************************************************************************************************************
 * LITERALS & CONSTANTS
 **********************************************************************************************************************/

#define BRIDGE_STATUS_OK    (0)
#define BRIDGE_STATUS_FAIL  (1)

// This is based on StudioLink's current policy of chunking commands that span multiple register addresses.
// Cmds are limited to a 200 register span, after which Cmds get chunked.
// For 32-bit wide regsiters this means BlockRead & BlockWrite cmds will not contain more the 800 bytes of data
// However the data is represented in hex string format meaning byte needs 2 chars to represent it

#define BRIDGE_MAX_WISCE_REG_SPAN               (200)
#define BRIDGE_REG_BYTES                        (4)
#define BRIDGE_MAX_BLOCK_WRITE_BYTES            (BRIDGE_MAX_WISCE_REG_SPAN * BRIDGE_REG_BYTES)
#define BRIDGE_MAX_BLOCK_READ_BYTES             (800)
#if (BRIDGE_MAX_BLOCK_WRITE_BYTES > BRIDGE_MAX_BLOCK_READ_BYTES)
    #define BRIDGE_BLOCK_BUFFER_LENGTH_BYTES    (BRIDGE_MAX_BLOCK_WRITE_BYTES)
#else
    #define BRIDGE_BLOCK_BUFFER_LENGTH_BYTES    (BRIDGE_MAX_BLOCK_READ_BYTES)
#endif

/***********************************************************************************************************************
 * MACROS
 **********************************************************************************************************************/
#define MAX_BRIDGE_DEVICE_NAME_LEN  32

/***********************************************************************************************************************
 * ENUMS, STRUCTS, UNIONS, TYPEDEFS
 **********************************************************************************************************************/
typedef struct
{
    // device_id_str should contain the value of register 0 in ASCII form eg "47A63" or "CS47A63"
    const char *device_id_str;
    // dev_name_str should be a unique string of max length MAX_BRIDGE_DEVICE_NAME_LEN with no spaces. Eg "Shelley_Left,"
    // WISCE/SCS will use this in their commands to target the correct device
    const char *dev_name_str;
    uint8_t bus_i2c_cs_address;
    regmap_cp_config_t b;
} bridge_device_t;

/***********************************************************************************************************************
 * GLOBAL VARIABLES
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/

/**
 * Initialize the bridge processing module.
 *
 * @param [in] device_list      Pointer to the array of bridge_device_t configured in BSP code
 * @param [in] num_devices      Number of devices in device_list
 *
 * @return
 * - BRIDGE_STATUS_FAIL         If device_list is NULL
 * - BRIDGE_STATUS_OK           otherwise
 *
 */
uint32_t bridge_initialize(bridge_device_t *device_list, uint8_t num_devices);

/**
 * Process any incoming Bridge commands
 *
 * @return
 * - void
 *
 */
void bridge_process(void);

/**********************************************************************************************************************/
#ifdef __cplusplus
}
#endif

#endif // BRIDGE_H
