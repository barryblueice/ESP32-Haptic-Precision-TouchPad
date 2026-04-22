/**
 * @file msbc.c
 *
 * @brief mSBC decompression module
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
#include "I2C/SUB_DEV/mcu-drivers/common/compression/msbc.h"
#include "sbc.h"
#include "I2C/SUB_DEV/mcu-drivers/common/platform_bsp/platform_bsp.h"

#include "esp_log.h"

#define TAG "MSBC_DBG"

/***********************************************************************************************************************
 * LOCAL LITERAL SUBSTITUTIONS
 **********************************************************************************************************************/
#define MSBC_BUF_SIZE   (2048LU)

/***********************************************************************************************************************
 * LOCAL VARIABLES
 **********************************************************************************************************************/
typedef struct
{
    sbc_t sbc;
    data_ringbuf_t packed16_data_buf;
    uint8_t *packed16_buf;
    uint8_t *frame;
    uint8_t *decoded_frame;
    int32_t framelen;
    size_t decoded_framelen;
    decompr_t decompr;
} msbc_t;

/***********************************************************************************************************************
 * LOCAL FUNCTIONS
 **********************************************************************************************************************/

static uint32_t msbc_configure(msbc_t *msbc, data_ringbuf_t *compr_data_buf_ptr)
{
    uint8_t temp_buf[1024];
    uint32_t p16_bytes;
    uint8_t *next_read_ptr;
    uint32_t next_read_len;

    // Get some data in the packed16_data_buf ready to decode
    if (decompr_data(&msbc->decompr, &msbc->packed16_data_buf, compr_data_buf_ptr, &p16_bytes) != DECOMPR_STATUS_OK)
    {
        debug_printf(TAG, "Failed to decompress compr_data_buf_ptr->packed16_data_buf\n\r");
        return DECOMPR_STATUS_FAIL;
    }

    // Find the next read block in the packed16_data_buf as sbc_decode works with raw pointers
    data_ringbuf_next_read_block(&msbc->packed16_data_buf, &next_read_ptr, &next_read_len);

    // Temporarily point decoded_frame at the temp_buf until it is known how big decoded_frame needs to be
    if (msbc->decoded_frame != NULL)
    {
        free(msbc->decoded_frame);
    }
    msbc->decoded_frame = temp_buf;

    // Decode the 1st frame to get the framelen
    msbc->framelen = sbc_decode(&msbc->sbc,
                                next_read_ptr,
                                next_read_len,
                                msbc->decoded_frame,
                                sizeof(temp_buf),
                                &msbc->decoded_framelen);
    // Don't need the data now so set decoded_frame to NULL in case a later step fails
    msbc->decoded_frame = NULL;

    // Allocate frame and decoded_frame buffers of the correct size
    if (msbc->framelen > 0)
    {
        // Allocate a frame buffer of the exact required size
        msbc->frame = malloc(msbc->framelen);
        if (msbc->frame == NULL)
        {
            debug_printf(TAG, "Couldn't allocate frame\n\r");
            return DECOMPR_STATUS_FAIL;
        }
        // Allocate a decoded_frame buffer of the exact required size
        msbc->decoded_frame = malloc(msbc->decoded_framelen);
        if (msbc->decoded_frame == NULL)
        {
            debug_printf(TAG, "Couldn't allocate decoded_frame\n\r");
            return DECOMPR_STATUS_FAIL;
        }
    }
    else
    {
        debug_printf(TAG, "Initial framelen=%ld\n\r", msbc->framelen);
        return DECOMPR_STATUS_FAIL;
    }
    return DECOMPR_STATUS_OK;
}

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/

void *msbc_init(endian_t output_endian)
{
    msbc_t *msbc;

    // Create a context structure and set it all to 0s
    msbc = malloc(sizeof(msbc_t));
    if (msbc == NULL)
    {
        return NULL;
    }

    msbc->frame = NULL;
    msbc->decoded_frame = NULL;
    msbc->framelen = 0;
    msbc->decoded_framelen = 0;

    // Initialize the packed16 decode
    decompr_init(&msbc->decompr, COMPR_ENC_FORMAT_PACKED16, ENDIAN_LITTLE);

    // Init the sbc lib
    sbc_init_msbc(&msbc->sbc, 0L);
    msbc->sbc.endian = (output_endian == ENDIAN_LITTLE) ? SBC_LE : SBC_BE;

    // Init the packed16_data_buf which will hold the data after it has been unpacked
    msbc->packed16_buf = malloc(MSBC_BUF_SIZE);

    if (msbc->packed16_buf != NULL)
    {
        data_ringbuf_init(&msbc->packed16_data_buf, msbc->packed16_buf, MSBC_BUF_SIZE);
    }
    else
    {
        sbc_finish(&msbc->sbc);
        decompr_deinit(&msbc->decompr);
        free(msbc);
        msbc = NULL;
    }

    return msbc;
}

uint32_t msbc_decompress(void *context,
                         data_ringbuf_t *decompr_data_buf_ptr,
                         data_ringbuf_t *compr_data_buf_ptr,
                         uint32_t *bytes_decompressed)
{
    msbc_t *msbc = (msbc_t *)context;
    bool data_to_decode;
    *bytes_decompressed = 0;

    // If this is the 1st time decompress has been called, then init the msbc stream
    if (msbc->frame == NULL)
    {
        if (msbc_configure(msbc, compr_data_buf_ptr) != DECOMPR_STATUS_OK)
        {
            debug_printf(TAG, "Failed to init msbc stream\n\r");
            return DECOMPR_STATUS_OK;
        }
    }

    // There needs to be:
    // * At least 8 bytes of data to decode in the compressed buffer OR at least framelen bytes in the packed16 buffer
    // * At least decoded_framlen bytes free space in the decompressed buffer
    data_to_decode = (   (data_ringbuf_data_length(compr_data_buf_ptr) >= 8)
                      || (data_ringbuf_data_length(&msbc->packed16_data_buf) >= msbc->framelen))
                  && (data_ringbuf_free_space(decompr_data_buf_ptr) > msbc->decoded_framelen);
    while (data_to_decode)
    {
        // Check if the packed16_data_buf can be topped up
        if (data_ringbuf_free_space(&msbc->packed16_data_buf) > 6)
        {
            uint32_t p16_bytes;
            if (decompr_data(&msbc->decompr, &msbc->packed16_data_buf, compr_data_buf_ptr, &p16_bytes) != DECOMPR_STATUS_OK)
            {
                debug_printf(TAG, "msbc_decompress: decmopress failed\n\r");
                return DECOMPR_STATUS_FAIL;
            }
        }

        // Decode whilst there is still packed16 data to decode and space to put it in the decompressed buffer
        while ((data_ringbuf_data_length(&msbc->packed16_data_buf) >= msbc->framelen)
            && (data_ringbuf_free_space(decompr_data_buf_ptr) >= msbc->decoded_framelen))
        {
            size_t len;
            int32_t curr_framelen;

            // Read the next frame from packed16_data_buf into frame
            data_ringbuf_read(&msbc->packed16_data_buf, msbc->frame, msbc->framelen);

            // Decode the frame
            curr_framelen = sbc_decode(&msbc->sbc,
                                       msbc->frame,
                                       msbc->framelen,
                                       msbc->decoded_frame,
                                       msbc->decoded_framelen,
                                       &len);
            if (curr_framelen < 0)
            {
                debug_printf(TAG, "msbc_decompress: Failed to decode frame - discard frame and continue\n\r");
            }
            else
            {
                // Write it to the decompressed buffer
                if (data_ringbuf_write(decompr_data_buf_ptr, msbc->decoded_frame, msbc->decoded_framelen) != DATA_RINGBUF_STATUS_OK)
                {
                    debug_printf(TAG, "msbc_decompress: Failed to write decoded frame to decompressed buffer\n\r");
                    return DECOMPR_STATUS_FAIL;
                }
                *bytes_decompressed += msbc->decoded_framelen;
            }
        }

        // There needs to be:
        // * At least 8 bytes of data to decode in the compressed buffer OR at least framelen bytes in the packed16 buffer
        // * At least decoded_framlen bytes free space in the decompressed buffer
        data_to_decode = (   (data_ringbuf_data_length(compr_data_buf_ptr) >= 8)
                          || (data_ringbuf_data_length(&msbc->packed16_data_buf) >= msbc->framelen))
                      && (data_ringbuf_free_space(decompr_data_buf_ptr) > msbc->decoded_framelen);
    }

    return DECOMPR_STATUS_OK;
}

void msbc_deinit(void *context)
{
    msbc_t *msbc = (msbc_t *)context;
    sbc_finish(&msbc->sbc);
    decompr_deinit(&msbc->decompr);
    if (msbc->decoded_frame)
    {
        free(msbc->decoded_frame);
        msbc->decoded_frame = NULL;
    }
    if (msbc->frame)
    {
        free(msbc->frame);
        msbc->frame = NULL;
    }
    if (msbc->packed16_buf)
    {
        free(msbc->packed16_buf);
        msbc->packed16_buf = NULL;
    }
    free(msbc);
    msbc = NULL;
}

