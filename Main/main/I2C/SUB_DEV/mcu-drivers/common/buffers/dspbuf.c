/**
 * @file dspbuf.c
 *
 * @brief DSP compressed read buffer module
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
#include "I2C/SUB_DEV/mcu-drivers/common/buffers/dspbuf.h"

#include "esp_log.h"

#define TAG "DSPBUF"
/***********************************************************************************************************************
 * LOCAL LITERAL SUBSTITUTIONS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * LOCAL VARIABLES
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * LOCAL FUNCTIONS
 **********************************************************************************************************************/
static uint32_t dspbuf_get_value(dspbuf_t *dspbuf, dspbuf_struct_offsets_t offset, uint32_t *value);
static uint32_t dspbuf_set_value(dspbuf_t *dspbuf, dspbuf_struct_offsets_t offset, uint32_t value);
static uint32_t dspbuf_struct_init(dspbuf_t *dspbuf, dspbuf_ringbuf_t *ring_buf);

/**
 * Read a value of an element of dspbuf struct from DSP
 *
 */
static uint32_t dspbuf_get_value(dspbuf_t *dspbuf,
                                 dspbuf_struct_offsets_t offset,
                                 uint32_t *value)
{
    uint32_t addr = (dspbuf->rb_struct_base_addr + (offset * dspbuf->config.bytes_per_reg));
    uint32_t ret;

    ret = regmap_read(dspbuf->config.cp, addr, value);
    *value = *value & 0xFFFFFF; // 24bit values on ADSP2
    if (ret != REGMAP_STATUS_OK)
    {
        return DSPBUF_STATUS_FAIL;
    }
    else
    {
        return DSPBUF_STATUS_OK;
    }
}

/**
 * Set a value of an element of dspbuf struct to DSP
 *
 */
static uint32_t dspbuf_set_value(dspbuf_t *dspbuf,
                                 dspbuf_struct_offsets_t offset,
                                 uint32_t value)
{
    uint32_t addr = (dspbuf->rb_struct_base_addr + (offset * dspbuf->config.bytes_per_reg));
    uint32_t ret;

    value = value & 0x00FFFFFF; // 24bit values on ADSP2
    ret = regmap_write(dspbuf->config.cp, addr, value);
    if (ret != REGMAP_STATUS_OK)
    {
        return DSPBUF_STATUS_FAIL;
    }
    else
    {
        return DSPBUF_STATUS_OK;
    }
}

/**
 * Initialize each element of dsp ring dspbuf struct, and communicate values with DSP when needed
 *
 */
static uint32_t dspbuf_struct_init(dspbuf_t *dspbuf, dspbuf_ringbuf_t *ring_buf)
{
    uint32_t index = 0;
    uint32_t buf_start_offset = 0;
    while (index < DSPBUF_MAX_N_BUFFERS)
    {
        dspbuf_loc_t *buf_loc_ptr = &ring_buf->dspbuf_locs[index];
        buf_loc_ptr->start_offset = buf_start_offset;

        // Get the dspbuf end offset
        dspbuf_get_value(dspbuf,
                         dspbuf->config.bufs_config[index].size_id,
                         &(buf_loc_ptr->end_offset));

        // If the end of this dspbuf is the same as the start of the last, then this dspbuf is NULL
        if (buf_loc_ptr->end_offset != buf_start_offset)
        {
            dspbuf_get_value(dspbuf,
                             dspbuf->config.bufs_config[index].base_id,
                             &(buf_loc_ptr->base));
            buf_loc_ptr->base = (buf_loc_ptr->base * dspbuf->config.bytes_per_reg)
                               + dspbuf->config.bufs_config[index].mem_base;
        }
        else
        {
            buf_loc_ptr->base = 0;
        }

        buf_start_offset = buf_loc_ptr->end_offset;
        ++index;
        ++buf_loc_ptr;
    }
    ring_buf->total_bufs_size = ring_buf->dspbuf_locs[DSPBUF_MAX_N_BUFFERS - 1].end_offset
                              * dspbuf->config.bytes_per_reg;
    ring_buf->space_avail = ring_buf->total_bufs_size / dspbuf->config.bytes_per_reg;
    ring_buf->next_word_write_index = 0;
    ring_buf->next_word_read_index = 0;
    ring_buf->data_avail = 0;
    dspbuf_get_value(dspbuf, irq_ack, &(ring_buf->irq_ack));
    dspbuf_get_value(dspbuf, next_word_write_index, &(ring_buf->next_word_write_index));
    dspbuf_get_value(dspbuf, error, &(ring_buf->error));
    dspbuf_get_value(dspbuf, irq_count, &(ring_buf->irq_count));
    ring_buf->high_water_mark = 4096;
    dspbuf_set_value(dspbuf, high_water_mark, ring_buf->high_water_mark);

    return DSPBUF_STATUS_OK;
}

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/

uint32_t dspbuf_init(dspbuf_t *dspbuf, dspbuf_config_t *dspbuf_config)
{
    uint32_t ret;
    uint32_t addr = 0;
    uint32_t count = 0;

    dspbuf->config = *dspbuf_config;

    // Find ring dspbuf address
    if (dspbuf->config.buf_symbol == 0)
    {
        return DSPBUF_STATUS_FAIL;
    }

    ret = regmap_read(dspbuf->config.cp, dspbuf->config.buf_symbol, &addr);
    if (ret != REGMAP_STATUS_OK)
    {
        return DSPBUF_STATUS_FAIL;
    }

    // If the address is 0, wait and check again once the fw has had time to init
    while ((addr == 0) & (count < 10))
    {
        bsp_driver_if_g->set_timer(5, NULL, NULL);
        ret = regmap_read(dspbuf->config.cp, dspbuf->config.buf_symbol, &addr);
        if (ret != REGMAP_STATUS_OK)
        {
            return DSPBUF_STATUS_OK;
        }
        count++;
    }

    if (addr == 0)
    {
         return DSPBUF_STATUS_FAIL;
    }

    dspbuf->rb_struct_base_addr = (addr * dspbuf->config.bytes_per_reg) + dspbuf->config.rb_struct_mem_start_address;

    ret = dspbuf_struct_init(dspbuf,
                             &dspbuf->ring_buf);
    if (ret != DSPBUF_STATUS_OK)
    {
      return DSPBUF_STATUS_FAIL;
    }

    // ret = decompr_init(&dspbuf->decompr, dspbuf->config.enc_format, ENDIAN_LITTLE);
    // if (ret != DECOMPR_STATUS_OK)
    // {
    //     ESP_LOGI(TAG, "Failed to init decompression %lu\n\r", ret);
    //     return DSPBUF_STATUS_FAIL;
    // }

    data_ringbuf_init(&dspbuf->compr_data_buf, dspbuf->config.compr_buf_ptr, dspbuf->config.compr_buf_size);

    return dspbuf_update_status(dspbuf);
}

uint32_t dspbuf_data_avail(dspbuf_t *dspbuf)
{
    int32_t data;
    uint32_t ret;

    ret = dspbuf_get_value(dspbuf, next_word_read_index, &dspbuf->ring_buf.next_word_read_index);
    if (ret)
    {
        return DSPBUF_STATUS_FAIL;
    }

    ret = dspbuf_get_value(dspbuf, next_word_write_index, &dspbuf->ring_buf.next_word_write_index);
    if (ret)
    {
        return DSPBUF_STATUS_FAIL;
    }

    data = (dspbuf->ring_buf.next_word_write_index - dspbuf->ring_buf.next_word_read_index)
         * dspbuf->config.bytes_per_reg;
    if (data < 0)
    {
        // Write index has wrapped
        data += dspbuf->ring_buf.total_bufs_size;
    }

    dspbuf->ring_buf.data_avail = data;

    return DSPBUF_STATUS_OK;
}

uint32_t dspbuf_read(dspbuf_t *dspbuf,
                     data_ringbuf_t *data_buf,
                     uint32_t data_len,
                     uint32_t *data_read)
{
    uint32_t data_buf_space;
    uint32_t ret;
    uint32_t data_to_read;
    uint32_t index = 0;
    *data_read = 0;

    if (data_len > dspbuf->ring_buf.data_avail || ((data_len % dspbuf->config.bytes_per_reg) != 0))
    {
        ESP_LOGI(TAG, "Reading: data_len error, requested %lu bytes but only %lu available\n\r", data_len, dspbuf->ring_buf.data_avail);
        return DSPBUF_STATUS_FAIL;
    }

    // Find out how much data to read
    data_buf_space = data_ringbuf_free_space(data_buf);
    data_to_read = (data_len > data_buf_space) ? data_buf_space : data_len;

    // Loop until all the required data has been read
    while (*data_read < data_to_read)
    {
        dspbuf_loc_t *buf_loc_ptr = &dspbuf->ring_buf.dspbuf_locs[index];

        // Check if the next read index starts in this dspbuf
        if (dspbuf->ring_buf.next_word_read_index >= buf_loc_ptr->start_offset
        &&  dspbuf->ring_buf.next_word_read_index < buf_loc_ptr->end_offset)
        {
            uint32_t buf_start_word_read_index = dspbuf->ring_buf.next_word_read_index - buf_loc_ptr->start_offset;
            uint32_t read_addr = buf_loc_ptr->base + (buf_start_word_read_index * dspbuf->config.bytes_per_reg);
            uint32_t bytes_to_read;
            uint8_t *write_ptr;
            uint32_t write_len;
            uint32_t buf_end_word_read_index;

            buf_end_word_read_index = (dspbuf->ring_buf.next_word_write_index <= buf_loc_ptr->end_offset) ?
                                       dspbuf->ring_buf.next_word_write_index - buf_loc_ptr->start_offset
                                     : buf_loc_ptr->end_offset;

            // Next part of the dspbuf that can be written into with the read data
            data_ringbuf_next_write_block(data_buf, &write_ptr, &write_len);

            // There is space to write more than is requested so just take what is requested
            if (write_len > (data_to_read - *data_read))
            {
                write_len = data_to_read - *data_read;
            }

            // Check there is enough space to read the data that is left in the DSP dspbuf
            if  (((buf_end_word_read_index - buf_start_word_read_index) * dspbuf->config.bytes_per_reg) > write_len)
            {
                // Adjust indexes to only read what there is space for, but not more than the space in the buffer
                buf_end_word_read_index = ((buf_start_word_read_index + (write_len / dspbuf->config.bytes_per_reg)) <= buf_loc_ptr->end_offset - buf_loc_ptr->start_offset) ?
                                          (buf_start_word_read_index + (write_len / dspbuf->config.bytes_per_reg))
                                          : buf_loc_ptr->end_offset - buf_loc_ptr->start_offset;
            }

            bytes_to_read = (buf_end_word_read_index - buf_start_word_read_index) * dspbuf->config.bytes_per_reg;

            // Make sure bytes to read is a multiple of 4 to prevent the dspbuf getting out of sync
            bytes_to_read /= 4;
            bytes_to_read *= 4;

            ret = regmap_read_block(dspbuf->config.cp, read_addr, write_ptr, bytes_to_read);
            if (ret == REGMAP_STATUS_FAIL)
            {
                return DSPBUF_STATUS_FAIL;
            }

            data_ringbuf_bytes_written(data_buf, bytes_to_read);
            dspbuf->ring_buf.next_word_read_index =
                    (dspbuf->ring_buf.next_word_read_index + (buf_end_word_read_index - buf_start_word_read_index))
                    % (((dspbuf->ring_buf.total_bufs_size) / dspbuf->config.bytes_per_reg));
            *data_read += bytes_to_read;
        }
        else
        {
            // Not starting in this dspbuf so check the next dspbuf
            index = (index + 1) % DSPBUF_MAX_N_BUFFERS;
        }
    }
    ret = dspbuf_set_value(dspbuf, next_word_read_index, dspbuf->ring_buf.next_word_read_index);
    if (ret != DSPBUF_STATUS_OK)
    {
        return DSPBUF_STATUS_FAIL;
    }

    dspbuf->ring_buf.data_avail -= *data_read;

    return DSPBUF_STATUS_OK;
}

uint32_t dspbuf_reenable_irq(dspbuf_t *dspbuf)
{
    uint32_t ret;

    ret = dspbuf_get_value(dspbuf, irq_count, &dspbuf->ring_buf.irq_count);
    if (ret != DSPBUF_STATUS_OK)
    {
        return DSPBUF_STATUS_FAIL;
    }

    if (dspbuf->ring_buf.irq_count & 0x01)
    {
        ESP_LOGI(TAG, "No need to ack irq_count=%lu\n\r", dspbuf->ring_buf.irq_count);
        return DSPBUF_STATUS_OK;
    }

    ret = dspbuf_set_value(dspbuf, irq_ack, dspbuf->ring_buf.irq_count | 0x1);
    if (ret)
    {
        return DSPBUF_STATUS_FAIL;
    }

    return DSPBUF_STATUS_OK;
}


uint32_t dspbuf_update_status(dspbuf_t *dspbuf)
{
    dspbuf_get_value(dspbuf, irq_ack, &(dspbuf->ring_buf.irq_ack));
    dspbuf_get_value(dspbuf, error, &(dspbuf->ring_buf.error));
    dspbuf_get_value(dspbuf, irq_count, &(dspbuf->ring_buf.irq_count));
    dspbuf_get_value(dspbuf, next_word_read_index, &(dspbuf->ring_buf.next_word_read_index));
    return DSPBUF_STATUS_OK;
}


uint32_t dspbuf_get_error(dspbuf_t *dspbuf)
{
    return dspbuf->ring_buf.error;
}


uint32_t dspbuf_get_data_avail(dspbuf_t *dspbuf)
{
    return dspbuf->ring_buf.data_avail;
}
