/**
 * @file decompr.h
 *
 * @brief Functions and prototypes exported by the decompression API module
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

#ifndef DECOMPR_H
#define DECOMPR_H

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/
#include "I2C/SUB_DEV/mcu-drivers/common/buffers/data_ringbuf.h"

/***********************************************************************************************************************
 * LITERALS & CONSTANTS
 **********************************************************************************************************************/

/**
 * @defgroup DECOMPR_
 * @brief Return values for decompression API
 *
 * @{
 */
#define DECOMPR_STATUS_OK                    (0)
#define DECOMPR_STATUS_FAIL                  (1)

/***********************************************************************************************************************
 * MACROS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * ENUMS, STRUCTS, UNIONS, TYPEDEFS
 **********************************************************************************************************************/

typedef enum
{
    COMPR_ENC_FORMAT_PACKED16,
    COMPR_ENC_FORMAT_MSBC,
    COMPR_ENC_FORMAT_UNSHORTEN,
    COMPR_ENC_FORMAT_DEFAULT // Do not change the buffer format (must be chosen for SCC lib v8.7.0 and older)
} compr_enc_format_t;

typedef enum
{
    ENDIAN_BIG,
    ENDIAN_LITTLE
} endian_t;

typedef struct
{
    compr_enc_format_t enc_format;
    endian_t output_endian;
    void *(*init)(endian_t output_endian);
    uint32_t (*decompress)(void *context, data_ringbuf_t *decompr_data_buf_ptr, data_ringbuf_t *compr_data_buf_ptr, uint32_t *bytes_decompressed);
    void (*deinit)(void *context);
    void *context;
} decompr_t;

/***********************************************************************************************************************
 * GLOBAL VARIABLES
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/

/**
 * Initialize the decompression structure with a given encoding format
 *
 * @param [in]
 * - decompr             Pointer to the decompression state structure
 * - enc_format          The encoding format to be used when decompressing
 * - output_endian       The endianness to use for the output data
 *
 * @return
 * - DECOMPR_STATUS_FAIL         Failed to initialize the decompression module
 * - DECOMPR_STATUS_OK           otherwise
 *
 */
uint32_t decompr_init(decompr_t *decompr, compr_enc_format_t enc_format, endian_t output_endian);

/**
 * Decompress data in the initialized format
 *
 * @param [in]
 * - decompr              Pointer to the decompression state structure
 * - decompr_data_buf_ptr Pointer to the data buffer to decompress data to
 * - compr_data_buf_ptr   Pointer to the data buffer containing the compressed data
 * - bytes_decompressed   Pointer to store number of decompressed bytes
 *
 * @return
 * - DECOMPR_STATUS_FAIL         Failed to decompress the given data
 * - DECOMPR_STATUS_OK           otherwise
 *
 */
uint32_t decompr_data(decompr_t *decompr,
                      data_ringbuf_t *decompr_data_buf_ptr,
                      data_ringbuf_t *compr_data_buf_ptr,
                      uint32_t *bytes_decompressed);

/**
 * Deinitialize the decompression structure, freeing all resources used
 *
 * @param [in]
 * - decompr             Pointer to the decompression state structure
 *
 */
void decompr_deinit(decompr_t *decompr);

/**********************************************************************************************************************/
#ifdef __cplusplus
}
#endif

#endif // DECOMPR_H
