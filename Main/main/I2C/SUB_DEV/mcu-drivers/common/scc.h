/**
 * @file scc.h
 *
 * @brief Functions and prototypes exported by the SCC API module
 *
 * @copyright
 * Copyright (c) Cirrus Logic 2023 All Rights Reserved, http://www.cirrus.com/
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

#ifndef SCC_H
#define SCC_H

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/
#include <stdint.h>
#include "I2C/SUB_DEV/mcu-drivers/common/compression/decompr.h"
#include "I2C/SUB_DEV/mcu-drivers/common/regmap.h"
#include "I2C/SUB_DEV/mcu-drivers/common/fw_img.h"

/***********************************************************************************************************************
 * LITERALS & CONSTANTS
 **********************************************************************************************************************/
 /**
 * @defgroup SCC_
 * @brief Values for SCC API module control
 *
 * @{
 */
#define SCC_COMPR_ENC_FORMAT_PACKED16       (0)
#define SCC_COMPR_ENC_FORMAT_MSBC           (2)
#define SCC_COMPR_ENC_FORMAT_DEFAULT        (0xFF) // Do not change the buffer format (must be chosen for SCC lib v8.7.0 and older)

#define SCC_STATUS_CMD_ERROR                (1<<2)
#define SCC_STATUS_VTE1_OVERFLOW            (1<<4)
#define SCC_STATUS_VTE1_ACTIVE              (1<<8)
#define SCC_STATUS_VTE1_TRIGGERED           (1<<12)
#define SCC_STATUS_VTE1_MOST_RECENT_TRIGGER (1<<16)
#define SCC_STATUS_VTE1_HOST_BUF_ACTIVE     (1<<20)

#define SCC_NUM_HOST_CMD_RETRIES            (20)


/***********************************************************************************************************************
 * MACROS
 **********************************************************************************************************************/

#define SCC_STATUS_OK                    (0)
#define SCC_STATUS_FAIL                  (1)

/***********************************************************************************************************************
 * ENUMS, STRUCTS, UNIONS, TYPEDEFS
 **********************************************************************************************************************/

typedef enum
{
    SCC_HOST_CMD_START_VTE1         = (1<<0),
    SCC_HOST_CMD_STOP_VTE1          = (1<<1),
    SCC_HOST_CMD_START_VTE_STREAM1  = (1<<4),
    SCC_HOST_CMD_STOP_VTE_STREAM1   = (1<<5),
    SCC_HOST_CMD_ACK_VTE1_TRIG      = (1<<6),
    SCC_HOST_CMD_ACK_VTE2_TRIG      = (1<<7),
    SCC_HOST_CMD_MASK_PHRASE        = (1<<8),
    SCC_HOST_CMD_UNMASK_PHRASE      = (1<<9),
    SCC_HOST_CMD_CLEAR_ERROR        = (1<<15)
} scc_host_cmd_t;

typedef enum
{
    SCC_STATE_RESET     = 0,
    SCC_STATE_INIT      = 1,
    SCC_STATE_IDLE      = 2,
    SCC_STATE_LISTEN    = 3,
    SCC_STATE_LISTEN_AP = 4,
    SCC_STATE_DETECTED  = 5,
    SCC_STATE_STREAM    = 6,
    SCC_STATE_FATAL     = 7
} scc_state_t;

typedef struct
{
    uint32_t dsp_core;
    compr_enc_format_t enc_format;
    regmap_cp_config_t *cp_config;
    fw_img_info_t *fw_info;
    uint32_t state_symbol;
    uint32_t status_symbol;
    uint32_t error_symbol;
    uint32_t control_symbol;
    uint32_t manageackctrl_symbol;
    uint32_t enc_format_symbol;
    uint32_t host_buffer_raw_symbol;
} scc_config_t;

typedef struct
{
    scc_config_t config;
    uint32_t host_buffer_raw_address;
    uint32_t state;
    uint32_t status;
    uint32_t error;
} scc_t;

/***********************************************************************************************************************
 * GLOBAL VARIABLES
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/

uint32_t scc_init(scc_t *scc, scc_config_t *scc_config, uint32_t (*scc_init_fp)(scc_t*));
uint32_t scc_get_host_buffer(scc_t *scc);
uint32_t scc_get_state(scc_t *scc);
uint32_t scc_get_status(scc_t *scc);
uint32_t scc_get_error(scc_t *scc);
uint32_t scc_update_status(scc_t *scc);
uint32_t scc_host_command(scc_t *scc, scc_host_cmd_t command);
uint32_t scc_update_status(scc_t *scc);

/**********************************************************************************************************************/
#ifdef __cplusplus
}
#endif

#endif // SCC_H
