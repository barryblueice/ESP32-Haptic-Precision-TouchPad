/**
 * @file scc.c
 *
 * @brief Sound Clear Control API module
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
/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/
#include "I2C/SUB_DEV/mcu-drivers/common/scc.h"

/***********************************************************************************************************************
 * LOCAL LITERAL SUBSTITUTIONS
 **********************************************************************************************************************/
#define SCC_POLL_ACK_CTRL_MAX   (10)
#define SCC_POLL_ACK_CTRL_MS    (10)

/***********************************************************************************************************************
 * LOCAL VARIABLES
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * LOCAL FUNCTIONS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/

uint32_t scc_init(scc_t *scc, scc_config_t *scc_config, uint32_t (*scc_init_fp)(scc_t*))
{
    uint32_t ret;
    uint32_t scc_enc_format;

    scc->config = *scc_config;

    scc->host_buffer_raw_address = fw_img_find_symbol(scc->config.fw_info, scc->config.host_buffer_raw_symbol);
    if (scc->host_buffer_raw_address == 0)
    {
        return SCC_STATUS_FAIL;
    }

    // Select the requested compressed stream encoding
    switch (scc_config->enc_format)
    {
        case COMPR_ENC_FORMAT_PACKED16:
            scc_enc_format = SCC_COMPR_ENC_FORMAT_PACKED16;
            break;

        case COMPR_ENC_FORMAT_MSBC:
            scc_enc_format = SCC_COMPR_ENC_FORMAT_MSBC;
            break;

        case COMPR_ENC_FORMAT_DEFAULT: // Do not change the buffer format (must be chosen for SCC lib v8.7.0 and older)
            scc_enc_format = SCC_COMPR_ENC_FORMAT_DEFAULT;
            break;

        default:
            return SCC_STATUS_FAIL;
    }

    if (scc_enc_format != SCC_COMPR_ENC_FORMAT_DEFAULT)
    {
        ret = regmap_write_fw_control(scc->config.cp_config,
                                      scc->config.fw_info,
                                      scc->config.enc_format_symbol,
                                      scc_enc_format);
        if (ret != REGMAP_STATUS_OK)
        {
            return SCC_STATUS_FAIL;
        }
    }

    if (scc_init_fp != 0)
    {
        ret = scc_init_fp(scc);
        if (ret != SCC_STATUS_OK)
        {
            return SCC_STATUS_FAIL;
        }
    }

    ret = scc_update_status(scc);
    if (ret != SCC_STATUS_OK)
    {
        return SCC_STATUS_FAIL;
    }

    return SCC_STATUS_OK;
}

uint32_t scc_get_host_buffer(scc_t *scc)
{
    return scc->host_buffer_raw_address;
}

uint32_t scc_get_state(scc_t *scc)
{
    return scc->state;
}

uint32_t scc_get_status(scc_t *scc)
{
    return scc->status;
}

uint32_t scc_get_error(scc_t *scc)
{
    return scc->error;
}

uint32_t scc_host_command(scc_t *scc, scc_host_cmd_t command)
{
    if (regmap_write_acked_fw_control(scc->config.cp_config,
                                      scc->config.fw_info,
                                      scc->config.manageackctrl_symbol,
                                      command,
                                      0,
                                      SCC_POLL_ACK_CTRL_MAX,
                                      SCC_POLL_ACK_CTRL_MS) != REGMAP_STATUS_OK)
    {
        return SCC_STATUS_FAIL;
    }
    else
    {
        return SCC_STATUS_OK;
    }
}

uint32_t scc_update_status(scc_t *scc)
{
    uint32_t ret;

    ret = regmap_read_fw_control(scc->config.cp_config,
                                 scc->config.fw_info,
                                 scc->config.state_symbol,
                                 &scc->state);
    if (ret != REGMAP_STATUS_OK)
    {
        return SCC_STATUS_FAIL;
    }
    ret = regmap_read_fw_control(scc->config.cp_config,
                                 scc->config.fw_info,
                                 scc->config.status_symbol,
                                 &scc->status);
    if (ret != REGMAP_STATUS_OK)
    {
        return SCC_STATUS_FAIL;
    }
    ret = regmap_read_fw_control(scc->config.cp_config,
                                 scc->config.fw_info,
                                 scc->config.error_symbol,
                                 &scc->error);
    if (ret != REGMAP_STATUS_OK)
    {
        return SCC_STATUS_FAIL;
    }

    return SCC_STATUS_OK;
}

