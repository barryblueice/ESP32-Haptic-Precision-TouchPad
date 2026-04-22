/**
 * @file decompr.c
 *
 * @brief Decompression API module
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
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "I2C/SUB_DEV/mcu-drivers/common/compression/decompr.h"
#include "I2C/SUB_DEV/mcu-drivers/common/compression/msbc.h"
#include "packed16.h"

/***********************************************************************************************************************
 * LOCAL LITERAL SUBSTITUTIONS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * LOCAL VARIABLES
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * LOCAL FUNCTIONS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/

uint32_t decompr_init(decompr_t *decompr, compr_enc_format_t enc_format, endian_t output_endian)
{
    decompr->output_endian = output_endian;
    decompr->enc_format = enc_format;

    // Select the requested compressed stream encoding
    switch (decompr->enc_format)
    {
        case COMPR_ENC_FORMAT_PACKED16:
            decompr->init = &packed16_init;
            decompr->decompress = &packed16_decompress;
            decompr->deinit = &packed16_deinit;
            break;
        case COMPR_ENC_FORMAT_MSBC:
            decompr->init = &msbc_init;
            decompr->decompress = &msbc_decompress;
            decompr->deinit = &msbc_deinit;
            break;
        case COMPR_ENC_FORMAT_DEFAULT: // Do not change the buffer format (must be chosen for SCC lib v8.7.0 and older)
           decompr->init = &msbc_init;
           decompr->decompress = &msbc_decompress;
           decompr->deinit = &msbc_deinit;
            break;
        default:
            decompr->decompress = NULL;
            return DECOMPR_STATUS_FAIL;
    }

    // Call the decompression algorithms init to create a context
    decompr->context = decompr->init(output_endian);
    if (decompr->context == NULL)
    {
        return DECOMPR_STATUS_FAIL;
    }

    return DECOMPR_STATUS_OK;
}

uint32_t decompr_data(decompr_t *decompr,
                      data_ringbuf_t *decompr_data_buf_ptr,
                      data_ringbuf_t *compr_data_buf_ptr,
                      uint32_t *bytes_decompressed)
{
    return decompr->decompress(decompr->context, decompr_data_buf_ptr, compr_data_buf_ptr, bytes_decompressed);
}

void decompr_deinit(decompr_t *decompr)
{
    decompr->deinit(decompr->context);
    decompr->context = NULL;
    decompr->init = NULL;
    decompr->decompress = NULL;
    decompr->deinit = NULL;
}

