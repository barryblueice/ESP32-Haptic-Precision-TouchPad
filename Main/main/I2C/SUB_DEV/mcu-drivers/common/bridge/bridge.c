/**
 * @file bridge.c
 *
 * @brief The bridge implementation for WISCE or SCS interaction
 *
 * @copyright
 * Copyright (c) Cirrus Logic 2021-2022 All Rights Reserved, http://www.cirrus.com/
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
// Standard include files
#include <string.h>  // strncmp, strlen, strcat, strcpy
#include <stdlib.h>  // atoi, strtoul
#include <stdio.h>   // sscanf

// Local include files
#include "bridge.h"
#include "I2C/SUB_DEV/mcu-drivers/common/platform_bsp/platform_bsp.h"
#ifdef CONFIG_USE_VREGMAP
#include "I2C/SUB_DEV/mcu-drivers/cs40l25/bsp/vregmap.h"
#endif

/***********************************************************************************************************************
 * LOCAL LITERAL SUBSTITUTIONS
 **********************************************************************************************************************/
#define CHAR_RX_LEN                 (2)
#define MAX_BLOCK_DATA_BYTES        (BRIDGE_MAX_BLOCK_READ_BYTES * CHAR_RX_LEN)
#define MSG_RX_LEN                  (MAX_BLOCK_DATA_BYTES + 50)
#define MSG_TX_LEN                  (MAX_BLOCK_DATA_BYTES + 1)
#if (MSG_RX_LEN > MSG_TX_LEN)
    #define CMD_RESP_LENGTH_CHAR    (MSG_RX_LEN)
#else
    #define CMD_RESP_LENGTH_CHAR    (MSG_TX_LEN)
#endif

#define BLOCKWRITE_CONT   ("BWc")        // "BlockWrite chunk continue"


/* Error Codes
 23 (WMT_INVALID_PARAMETER) - Encountered an unexpected null pointer in the server.
 27 (WMT_WRITE_FAILED) - Failed to write debug control value
 28 (WMT_READ_FAILED) - Unable to parse the register line from the codec file.
    Either the device is in low power mode or the line is in an unrecognized format.
 32 (WMT_RESOURCE_FAIL) - Failed to allocate memory.
 33 (WMT_UNSUPPORTED) - Operation is not supported by the current StudioBridge implementation.
 36 (WMT_NO_DEVICE) - No device present or failed to open codec file.
 37 (WMT_REG_NOT_PRESENT) - Register is not present on device.
 46 (WMT_TRUNCATED) - Successfully read from the codec file, but the given buffer
    was not large enough for the requested count of bytes to be read - data has been truncated.
 63 General failure - String manipulation error in the server, failed to read from the
    codec file or there was a failure when communicating with the device.
 1E (WMT_INVALID_COMMAND) - Missing <reg> value or <reg> is too long

*/
// These represent Hex values as string literals
#define WMT_INVALID_COMMAND     ("1E")
#define WMT_INVALID_PARAMETER   ("23")
#define WMT_WRITE_FAILED        ("27")
#define WMT_READ_FAILED         ("28")
#define WMT_RESOURCE_FAIL       ("32")
#define WMT_UNSUPPORTED         ("33")
#define WMT_NO_DEVICE           ("36")
#define WMT_REG_NOT_PRESENT     ("37")
#define WMT_TRUNCATED           ("46")
#define GENERAL_FAILURE         ("63")
#define EVERYTHING_IS_OK        ("0")

#define ERROR                   ("ER")

// Some defines used in responses, for Shelley
/* So on Alt-OS need a Info table coded for THAT device in file bsp_<device>.c
 where we read all this info from, incl if multi-chip device and device identifiers
 */
#define APP_NAME        ("\"StudioBridge\"")
#define APP_VER         ("\"1.5.13.0\"")   // copied from protocol doc. Adjust
#define PROTO_VER       ("\"106\"")
#define STR(A) #A
#ifdef SYS_ID
#define STR_X(A) STR(A)
#define SYSTEM_ID STR_X(SYS_ID)
#else
#define SYSTEM_ID       STR(DEADBEEF)
#endif
#define OP_SYS          ("\"Alt-OS\"")
#define OP_SYS_VER      ("\"0.0.0\"")
#define DRIVER_CTRL     ("false")

/* The MCU-Bridge msg format Version
   Update this inline with bridge msg format updates to ensure versions remain compatible
*/
#define BRIDGE_MCU_MSG_FORMAT   "0.1"

#define WRITE_OK        ("Ok")

// Binary payload field offsets
#define LENGTH_OFFSET   (0)
#define OPCODE_OFFSET   (2)
// For PV only
#define VERSION_OFFSET  (3)
// For R, W, BR, BWs only
#define CHIPID_OFFSET   (3)
#define REG_ADDR_OFFSET (4)
// For W, BWs only
#define REG_VAL_OFFSET  (8)
// For BR only
#define READ_LEN_OFFSET  (8)
// For BWc only
#define REG_VAL_OFFSET_BWC  (3)


/***********************************************************************************************************************
 * ENUMS, STRUCTS, UNIONS, TYPEDEFS
 **********************************************************************************************************************/
typedef struct
{
    bridge_device_t *device_list;
    uint8_t num_devices;
    bridge_device_t *current_device;
} bridge_t;

typedef uint32_t (*bridge_command_handler_t)(unsigned char *cmd);

typedef struct
{
    const uint8_t opcode;
    bridge_command_handler_t handler;
} bridge_command_handler_map_t;

/***********************************************************************************************************************
 * LOCAL VARIABLES
 **********************************************************************************************************************/
static unsigned char cmd_resp[CMD_RESP_LENGTH_CHAR] = {0};
static uint8_t block_buffer[BRIDGE_BLOCK_BUFFER_LENGTH_BYTES] = {0};
static const char bus_name_i2c[] = "I2C";
static const char bus_name_spi[] = "SPI";
#ifdef CONFIG_USE_VREGMAP
static const char *bus_name_vregmap = bus_name_i2c;
#endif
static bridge_t bridge;

//static uint32_t bw_chip_num = 0;
static uint32_t bw_addr = 0;
static uint32_t bw_data_collect_indx = 0;
static size_t reg_sz = sizeof(uint32_t);

static uint32_t handle_protocol_version(unsigned char *cmd);
static uint32_t handle_info(unsigned char *cmd);
static uint32_t handle_read(unsigned char *cmd);
static uint32_t handle_write(unsigned char *cmd);
static uint32_t handle_blockread(unsigned char *cmd);
static uint32_t handle_blockwrite_start(unsigned char *cmd);
static uint32_t handle_blockwrite_cont(unsigned char *cmd);
static uint32_t handle_blockwrite_end(unsigned char *cmd);
static uint32_t handle_detect(unsigned char *cmd);
static uint32_t handle_unsupported(unsigned char *cmd);
static uint32_t handle_invalid(unsigned char *cmd);
static uint32_t handle_current_device(unsigned char *cmd);
static uint32_t handle_mcu_msg_format_version(unsigned char *cmd);


// An array of coded command Ids mapped to their handler functions
static const bridge_command_handler_map_t command_handler_map[] =
{
    {.opcode = 0x1,     .handler = handle_current_device},          // CurrentDevice
    {.opcode = 0x2,     .handler = handle_protocol_version},        // ProtocolVersion
    {.opcode = 0x3,     .handler = handle_info},                    // Info
    {.opcode = 0x4,     .handler = handle_detect},                  // Detect
    {.opcode = 0x5,     .handler = handle_read},                    // Read
    {.opcode = 0x6,     .handler = handle_write},                   // Write
    {.opcode = 0x7,     .handler = handle_blockread},               // BlockRead
    {.opcode = 0x8,     .handler = handle_blockwrite_start},        // BlockWrite
    {.opcode = 0x9,     .handler = handle_blockwrite_cont},
    {.opcode = 0xa,     .handler = handle_blockwrite_end},
    {.opcode = 0xb,     .handler = handle_unsupported},             // Device
    {.opcode = 0xc,     .handler = handle_unsupported},             // DriverControl
    {.opcode = 0xd,     .handler = handle_unsupported},             // ServiceMessage
    {.opcode = 0xe,     .handler = handle_invalid},                 // ServiceAvailable
    {.opcode = 0xf,     .handler = handle_unsupported},             // Shutdown
    {.opcode = 0x10,    .handler = handle_mcu_msg_format_version}   // MCU msg format version
};

/***********************************************************************************************************************
 * GLOBAL VARIABLES
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * LOCAL FUNCTIONS
 **********************************************************************************************************************/

/*
 * Functions that handle each of the bridge commands that we support
 */

static uint32_t handle_protocol_version(unsigned char *u_cmd)
{
    char *chptr = (char*)u_cmd;
    strcpy(chptr, "ProtocolVersion 106");

    return BRIDGE_STATUS_OK;
}

static uint32_t handle_info(unsigned char *u_cmd)
{
    // Abrreviated format: "app,versions,protocolversions,systemID,OS,OSversion"
    char *chptr = (char*)u_cmd;
    sprintf(chptr, "%s,%s,%s,%s,%s,%s", APP_NAME, APP_VER, PROTO_VER, "\"" SYSTEM_ID "\"", OP_SYS, OP_SYS_VER);

    return BRIDGE_STATUS_OK;
}

// User has executed a single register read command on WISCE/SCS.
static uint32_t handle_read(unsigned char *u_cmd)
{
    char *cmd = (char*)u_cmd;
    uint32_t ret;
    uint32_t reg_val = 0;

    // Get chip Id and address to read
    uint8_t cmd_chip_num = *(uint8_t*)&u_cmd[CHIPID_OFFSET];
    uint8_t device_index = cmd_chip_num - 1;
    if (device_index < bridge.num_devices)
    {
        bridge.current_device = &(bridge.device_list[device_index]);
    }
    else
    {
        sprintf(cmd, "%s", WMT_NO_DEVICE);
        return BRIDGE_STATUS_FAIL;
    }

    // Get read address
    uint32_t read_addr;
    memcpy((uint32_t*)&read_addr, (uint32_t*)&u_cmd[REG_ADDR_OFFSET], sizeof(uint32_t));

    /* All references to functions beginning with regmap_ are calls to the underlying bus between
     * the MCU and the device over either I2C or SPI, depending on the implementation.
     * Replace these calls using the bus access API of your implementaion.
     */
    ret = regmap_read(&(bridge.current_device->b), read_addr, &reg_val);
    if (ret != REGMAP_STATUS_OK)
    {
        sprintf(cmd, "%s", WMT_READ_FAILED);
        return BRIDGE_STATUS_FAIL;
    }
    sprintf(cmd, "%lu", reg_val);

    return BRIDGE_STATUS_OK;
}

// User has executed a single register write command on WISCE/SCS.
static uint32_t handle_write(unsigned char *u_cmd)
{
    char *cmd = (char*)u_cmd;
    uint32_t ret;

    // Get chip Id and address to write
    uint8_t cmd_chip_num = *(uint8_t*)&u_cmd[CHIPID_OFFSET];
    uint8_t device_index = cmd_chip_num - 1;
    if (device_index < bridge.num_devices)
    {
        bridge.current_device = &(bridge.device_list[device_index]);
    }
    else
    {
        sprintf(cmd, "%s", WMT_NO_DEVICE);
        return BRIDGE_STATUS_FAIL;
    }

    // Get write address
    uint32_t write_addr;
    memcpy((void*)&write_addr, (void*)&u_cmd[REG_ADDR_OFFSET], sizeof(uint32_t));

    // Get write value
    uint32_t write_val;
    memcpy((void*)&write_val, (void*)&u_cmd[REG_VAL_OFFSET], sizeof(uint32_t));

    ret = regmap_write(&(bridge.current_device->b), write_addr, write_val);
    if (ret != REGMAP_STATUS_OK)
    {
        sprintf(cmd, "%s", GENERAL_FAILURE);
        return BRIDGE_STATUS_FAIL;
    }

    sprintf(cmd, "%s", WRITE_OK);

    return BRIDGE_STATUS_OK;
}

// User has executed a block-read command on WISCE/SCS.
static uint32_t handle_blockread(unsigned char *u_cmd)
{
    char *cmd = (char*)u_cmd;
    uint32_t ret;

    // Get chip Id, address to read, length to read
    uint8_t cmd_chip_num = *(uint8_t*)&u_cmd[CHIPID_OFFSET];
    uint8_t device_index = cmd_chip_num - 1;
    if (device_index < bridge.num_devices)
    {
        bridge.current_device = &(bridge.device_list[device_index]);
    }
    else
    {
        sprintf(cmd, "%s", WMT_NO_DEVICE);
        return BRIDGE_STATUS_FAIL;
    }

    // Get read adrress
    uint32_t read_addr;
    memcpy((void*)&read_addr, (void*)&u_cmd[REG_ADDR_OFFSET], sizeof(uint32_t));

    // Get number of bytes to read
    uint16_t block_read_length;
    memcpy((void*)&block_read_length, (void*)&u_cmd[READ_LEN_OFFSET], sizeof(uint16_t));

    if (block_read_length > MAX_BLOCK_DATA_BYTES)
    {
        sprintf(cmd, "%s", WMT_UNSUPPORTED);
        return BRIDGE_STATUS_FAIL;
    }

    ret = regmap_read_block(&(bridge.current_device->b), read_addr, block_buffer, block_read_length);
    if(ret != REGMAP_STATUS_OK)
    {
        sprintf(cmd, "%s", WMT_READ_FAILED);
        return BRIDGE_STATUS_FAIL;
    }

    for(uint32_t i = 0; i < block_read_length; ++i)
    {
        // Convert each byte value into ASCII hex
        sprintf(&cmd[i*2], "%02X", block_buffer[i]);
    }
    cmd[block_read_length*2] = '\0'; // Manually add null terminator

    return BRIDGE_STATUS_OK;
}

// User has executed a block-write command on WISCE/SCS.
// The bridge agent breaks block-write commands into a series of messages, each carrying a chunk of register
// values to be written to the hardware. Now we accumulate the messages into a single block write to be
// written to hardware.
static uint32_t handle_blockwrite_start(unsigned char *u_cmd)
{
    char *cmd = (char*)u_cmd;

    /*
     The normal client cmd structure:
     Client to Agent: "[<seq>]       BlockWrite <start> <data>\n"  Assuming we're not implementing for ADSP cores!!
     Eg:              "[Shelley-1:9e] BW         2      00010203\n"

     Agent will send register values in chunks to here:

            MCU                                    Agent
                <-- "BWs [N] <addr> <XXXXXXXX>\n"
                <-- | Payload Length | BWs OpCode | Chip-Id | Start Addr | Reg value | Reg value | ...
                        2-bytes         1-byte      1-byte     4-bytes      4-bytes     4-bytes

                    "BWc\n"  -->

                <-- | Payload Length | BWc OpCode | Reg value | Reg value | ...

                    "BWc\n"  -->
                    :
                    :
                <-- | Payload Length | BWe OpCode |

                    "Ok\n"  -->
                    or
                    "ER <N>\n"

    Once we have all the data from agent we use block_write api
    */

    // Get chip Id and address to write
    uint8_t cmd_chip_num = *(uint8_t*)&u_cmd[CHIPID_OFFSET];
    uint8_t device_index = cmd_chip_num - 1;
    if (device_index < bridge.num_devices)
    {
        bridge.current_device = &(bridge.device_list[device_index]);
    }
    else
    {
        sprintf(cmd, "%s", WMT_NO_DEVICE);
        return BRIDGE_STATUS_FAIL;
    }

    // Get reg start address to use in regmap_write_block()
    memcpy((void*)&bw_addr , (void*)&u_cmd[REG_ADDR_OFFSET], sizeof(uint32_t));

    uint16_t payload_len, num_regs;
    // We stored payload length word in big-endian so do some byte swapping
    payload_len = u_cmd[LENGTH_OFFSET] << 8;
    payload_len |= u_cmd[LENGTH_OFFSET+1];
    num_regs = (payload_len-8)/reg_sz;

    if (num_regs*reg_sz > BRIDGE_BLOCK_BUFFER_LENGTH_BYTES)
    {
        // Abort
        sprintf(cmd, "%s", WMT_INVALID_COMMAND);
        return BRIDGE_STATUS_FAIL;
    }

    // Store each register value to new blockwrite context for collecting chunked data
    bw_data_collect_indx = 0;

    memcpy(&block_buffer[bw_data_collect_indx], &u_cmd[REG_VAL_OFFSET], reg_sz*num_regs);
    bw_data_collect_indx += reg_sz*num_regs;

    sprintf(cmd, "%s", BLOCKWRITE_CONT);

    return BRIDGE_STATUS_OK;
}

static uint32_t handle_blockwrite_cont(unsigned char *u_cmd)
{
    char *cmd = (char*)u_cmd;

    if (bw_data_collect_indx >= BRIDGE_BLOCK_BUFFER_LENGTH_BYTES)
    {
        // Abort
        sprintf(cmd, "%s", WMT_INVALID_COMMAND);
        return BRIDGE_STATUS_FAIL;
    }

    uint16_t payload_len, num_regs;
    // We stored payload length word in big-endian so do some byte swapping
    payload_len = u_cmd[LENGTH_OFFSET] << 8;
    payload_len |= u_cmd[LENGTH_OFFSET+1];
    num_regs = (payload_len-3)/reg_sz;

    // Add to blockwrite context for collecting chunked data
    memcpy(&block_buffer[bw_data_collect_indx], &u_cmd[REG_VAL_OFFSET_BWC], reg_sz*num_regs);
    bw_data_collect_indx += reg_sz*num_regs;

    sprintf(cmd, "%s", BLOCKWRITE_CONT);

    return BRIDGE_STATUS_OK;
}

static uint32_t handle_blockwrite_end(unsigned char *u_cmd)
{
    char *cmd = (char*)u_cmd;
    uint32_t ret;

    // Have all data, do block write
    ret = regmap_write_block(&(bridge.current_device->b), bw_addr , &block_buffer[0], bw_data_collect_indx);
    if(ret != REGMAP_STATUS_OK)
    {
        sprintf(cmd, "%s", GENERAL_FAILURE);
        return BRIDGE_STATUS_FAIL;
    }
    sprintf(cmd, "%s", WRITE_OK);

    return BRIDGE_STATUS_OK;
}

static uint32_t handle_detect(unsigned char *u_cmd)
{
    char *cmd = (char*)u_cmd;
    const char *bus_name_str;
    char temp_str[MAX_BRIDGE_DEVICE_NAME_LEN+1]; // Holds the dev_name_str eg "Shelley_Left,", plus one for safety

    cmd[0] = '\0';  // 0-out the cmd string for reuse
    for (uint8_t i = 0; i < bridge.num_devices; i++)
    {
        if (bridge.device_list[i].b.bus_type == REGMAP_BUS_TYPE_SPI)
        {
            bus_name_str = bus_name_spi;
        }
#ifdef CONFIG_USE_VREGMAP
        else if (bridge.device_list[i].b.bus_type == REGMAP_BUS_TYPE_I2C)
        {
            bus_name_str = bus_name_i2c;
        }
        else // only other option is REGMAP_BUS_TYPE_VIRTUAL
        {
            bus_name_str = bus_name_vregmap;
        }
#else
        else
        {
            bus_name_str = bus_name_i2c;
        }
#endif

        sprintf(temp_str, "%s,", bridge.device_list[i].dev_name_str);
        strcat(cmd, temp_str);
        sprintf(temp_str, "%s,", bus_name_str);
        strcat(cmd, temp_str);
        sprintf(temp_str, "%x,", bridge.device_list[i].bus_i2c_cs_address);
        strcat(cmd, temp_str);
        sprintf(temp_str, "%s,", DRIVER_CTRL);
        strcat(cmd, temp_str);
        sprintf(temp_str, "%s:", bridge.device_list[i].device_id_str);
        strcat(cmd, temp_str);
    }

    uint32_t index = strlen(cmd) - 1;
    cmd[index] = '\0'; // remove the final ':'

    return BRIDGE_STATUS_OK;
}

static uint32_t handle_unsupported(unsigned char *u_cmd)
{
    char *cmd = (char*)u_cmd;
    sprintf(cmd, "%s", WMT_UNSUPPORTED);

    return BRIDGE_STATUS_FAIL;
}

static uint32_t handle_invalid(unsigned char *u_cmd)
{
    char *cmd = (char*)u_cmd;
    sprintf(cmd, "%s", WMT_INVALID_COMMAND);

    return BRIDGE_STATUS_FAIL;
}

static uint32_t handle_current_device(unsigned char *u_cmd)
{
    char *cmd = (char*)u_cmd;
    sprintf(cmd, "%s", bridge.current_device->dev_name_str);

    return BRIDGE_STATUS_OK;
}

static uint32_t handle_mcu_msg_format_version(unsigned char *u_cmd)
{
    char *cmd = (char*)u_cmd;
    sprintf(cmd, "%s", BRIDGE_MCU_MSG_FORMAT);

    return BRIDGE_STATUS_OK;
}

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/

/**
 * Initialize the bridge processing module.
 *
 */
uint32_t bridge_initialize(bridge_device_t *device_list, uint8_t num_devices)
{
    if (device_list == NULL)
    {
        return BRIDGE_STATUS_FAIL;
    }

    bridge.device_list = device_list;
    bridge.num_devices = num_devices;
    bridge.current_device = device_list;

#ifdef CONFIG_USE_VREGMAP
    device_list[0].device_id_str = VREGMAP_BRIDGE_DEVICE_ID;
    device_list[0].dev_name_str = VREGMAP_BRIDGE_DEV_NAME;
    device_list[0].b = vregmap_cp;

    /*
     * A weird quirk of WISCE - it does not like devices of different types (I2C,SPI) in the same system.  So, if
     * there's another device in the list, vregmap bus type string must match.  default is I2C, so only check for SPI
     */
    if ((num_devices > 1) && (device_list[1].b.bus_type == REGMAP_BUS_TYPE_SPI))
    {
        bus_name_vregmap = bus_name_spi;
    }

    for (uint8_t i = 0; i < VREGMAP_LENGTH_REGS; i++)
    {
        vregmap[i].value = vregmap[i].default_value;
    }
#endif

    return BRIDGE_STATUS_OK;
}

/**
 * Wait for and process any incoming bridge commands from the transport between
 * the bridge agent (running on a host) and the MCU where this code is running.
 * This should be called in a continuous loop eg from the main function of the program.
 * It gathers characters until a '\n' character is received which signals the end of
 * a bridge command.
 */
void bridge_process(void)
{
    uint32_t val, i = 0;
    uint8_t pl_len_msb, pl_len_lsb;
    bool all_data_rxed = false;
    uint16_t payload_len = 0;

    /* Try to read the command sent from the transport between bridge agent and MCU.
     * In this implementation we are using a multi-packet UART, but other implementions
     * should replace this code with appropriate calls to their transport API.
     */

    // Get byte and see if not EOF
    do {
        /* In this implementation, fgetc overrides the std C system call and utilizes
         * a multi-packet UART. The semantics are the same as the std system call.
         * The function does not block
         * Received chars: EOF, ...EOF, PL, PL, PL, PL, EOF, .. EOF
         */
        val = fgetc(bridge_read_file);
        if (val == EOF)
        {
            // No data, so just return
            break;
        }
        else
        {
            // First 2 bytes will be the payload length
            pl_len_lsb = (uint8_t)val;   // If Agent sends in little-endian, this will be LSB
            pl_len_msb = (uint8_t)fgetc(bridge_read_file);
            // Work out payload len
            payload_len = (uint8_t)pl_len_msb << 8;
            payload_len |= (uint8_t)pl_len_lsb;

            cmd_resp[i++] = pl_len_msb;
            cmd_resp[i++] = pl_len_lsb;

            // Get remaining payload bytes
            for( ; i < payload_len ; ++i)
            {
                cmd_resp[i] = (uint8_t)fgetc(bridge_read_file);
            }
            all_data_rxed = true;  // cmd_resp now contains new binary format payload
        }
    } while (all_data_rxed != true);

    // If we received a complete Cmd from Agent, process it
    if (all_data_rxed)
    {
        uint32_t ret;
        uint8_t opcode = cmd_resp[OPCODE_OFFSET];
        bridge_command_handler_t handler = NULL;

        // Find the correct handler for the bridge command
        for (uint8_t k = 0; k < (sizeof(command_handler_map)/sizeof(bridge_command_handler_map_t)); k++)
        {
            if (opcode == command_handler_map[k].opcode)
            {
                handler = command_handler_map[k].handler;
                break;
            }
        }

        if (handler == NULL)
        {
            handler = handle_unsupported;
        }

        ret = handler(cmd_resp);

        if (ret != BRIDGE_STATUS_OK)
        {
            // Handler returned an error so send an error msg back to bridge
            fprintf(bridge_write_file, "%s %s\n", ERROR, cmd_resp);
        }
        else
        {
            /* Handler returned OK so send the response back to bridge.
             * Again, the fprintf overrides the C std system call and uses multi-packet UART
             * to transmit the data to the bridge agent.
             * For other transport, replace this call with the appropriate calls to
             * the transport API.
             */
            fprintf(bridge_write_file, "%s\n", cmd_resp);
        }
    }
}
