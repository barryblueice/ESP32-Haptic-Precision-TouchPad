/**
 * @file packed16.c
 *
 * @brief packed16 decompression module
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
#include <stdlib.h>
#include "packed16.h"
#include "I2C/SUB_DEV/mcu-drivers/common/platform_bsp/platform_bsp.h"

#include "esp_log.h"

#define TAG "PACKED16_DBG"

/***********************************************************************************************************************
 * LOCAL LITERAL SUBSTITUTIONS
 **********************************************************************************************************************/
#define COMPRESSED_DATA_BYTES   (8) // two 32bits containing two 24bit values
#define DECOMPRESSED_DATA_BYTES (6) // two 24bit values

// MSB and LSB positions of packed16 data within compressed data
#define PACKED16_0_MSB          (3)
#define PACKED16_0_LSB          (2)
#define PACKED16_1_MSB          (1)
#define PACKED16_1_LSB          (7)
#define PACKED16_2_MSB          (6)
#define PACKED16_2_LSB          (5)

/***********************************************************************************************************************
 * LOCAL VARIABLES
 **********************************************************************************************************************/
typedef struct
{
    endian_t endian;
    uint32_t write_index[DECOMPRESSED_DATA_BYTES]; // Indices of packed16 bytes within compressed data
} packed16_t;

/***********************************************************************************************************************
 * LOCAL FUNCTIONS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/

void *packed16_init(endian_t output_endian)
{
    packed16_t *packed16;

    // Create a context structure and set it all to 0s
    packed16 = malloc(sizeof(packed16_t));
    if (packed16 == NULL)
    {
        return NULL;
    }

    packed16->endian = output_endian;

    if (packed16->endian == ENDIAN_LITTLE)
    {
        packed16->write_index[0] = PACKED16_0_MSB;
        packed16->write_index[1] = PACKED16_0_LSB;
        packed16->write_index[2] = PACKED16_1_MSB;
        packed16->write_index[3] = PACKED16_1_LSB;
        packed16->write_index[4] = PACKED16_2_MSB;
        packed16->write_index[5] = PACKED16_2_LSB;
    }
    else
    {
        packed16->write_index[0] = PACKED16_0_LSB;
        packed16->write_index[1] = PACKED16_0_MSB;
        packed16->write_index[2] = PACKED16_1_LSB;
        packed16->write_index[3] = PACKED16_1_MSB;
        packed16->write_index[4] = PACKED16_2_LSB;
        packed16->write_index[5] = PACKED16_2_MSB;
    }

    return packed16;
}

uint32_t packed16_decompress(void *context,
                             data_ringbuf_t *decompr_data_buf_ptr,
                             data_ringbuf_t *compr_data_buf_ptr,
                             uint32_t *bytes_decompressed)
{
    packed16_t *packed16 = (packed16_t *)context;
    *bytes_decompressed = 0;
    while ((data_ringbuf_data_length(compr_data_buf_ptr) >= COMPRESSED_DATA_BYTES)
    &&     (data_ringbuf_free_space(decompr_data_buf_ptr) >= DECOMPRESSED_DATA_BYTES))
    {
        uint8_t samplebuf_compr[COMPRESSED_DATA_BYTES];
        uint8_t samplebuf_decompr[DECOMPRESSED_DATA_BYTES];
        if (data_ringbuf_read(compr_data_buf_ptr, &samplebuf_compr[0], COMPRESSED_DATA_BYTES) != DATA_RINGBUF_STATUS_OK)
        {
            ESP_LOGI(TAG, "Failed to read from compr buf\n");
            return DECOMPR_STATUS_FAIL;
        }
        for (uint32_t index = 0; index < DECOMPRESSED_DATA_BYTES; index++)
        {
            // Put decompressed data in correct order and ignore empty bytes
            samplebuf_decompr[index] = samplebuf_compr[packed16->write_index[index]];
        }
        if (data_ringbuf_write(decompr_data_buf_ptr, &samplebuf_decompr[0], DECOMPRESSED_DATA_BYTES) != DATA_RINGBUF_STATUS_OK)
        {
            ESP_LOGI(TAG, "Failed to write to packed16 buf\n");
            return DECOMPR_STATUS_FAIL;
        }
        *bytes_decompressed += DECOMPRESSED_DATA_BYTES;
    }

    return DECOMPR_STATUS_OK;
}

void packed16_deinit(void *context)
{
    packed16_t *packed16 = (packed16_t *)context;
    free(packed16);
}

