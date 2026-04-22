/**
 * @file data_ringbuf.c
 *
 * @brief The data ring buffer API module
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
#include <string.h>
#include "I2C/SUB_DEV/mcu-drivers/common/buffers/data_ringbuf.h"

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

void data_ringbuf_init(data_ringbuf_t *data_buf_ptr, uint8_t *buf_ptr, uint32_t buf_size)
{
    data_buf_ptr->buf_ptr = buf_ptr;
    data_buf_ptr->buf_size = buf_size;
    data_buf_ptr->next_byte_write_index = 0;
    data_buf_ptr->next_byte_read_index = 0;
    data_buf_ptr->data_length = 0;
}

uint32_t data_ringbuf_free_space(data_ringbuf_t *data_buf_ptr)
{
    return data_buf_ptr->buf_size - data_buf_ptr->data_length;
}

uint32_t data_ringbuf_data_length(data_ringbuf_t *data_buf_ptr)
{
    return data_buf_ptr->data_length;
}

void data_ringbuf_next_write_block(data_ringbuf_t *data_buf_ptr, uint8_t **write_ptr_ptr, uint32_t *write_len_ptr)
{
    uint32_t free_space = data_ringbuf_free_space(data_buf_ptr);
    if (free_space == 0)
    {
        *write_len_ptr = 0;
        *write_ptr_ptr = NULL;
    }
    else
    {
        if (data_buf_ptr->next_byte_read_index <= data_buf_ptr->next_byte_write_index)
        {
            *write_len_ptr = data_buf_ptr->buf_size - data_buf_ptr->next_byte_write_index;
        }
        else
        {
            *write_len_ptr = data_buf_ptr->next_byte_read_index - data_buf_ptr->next_byte_write_index;
        }
        *write_ptr_ptr = data_buf_ptr->buf_ptr + data_buf_ptr->next_byte_write_index;
    }
}

void data_ringbuf_next_read_block(data_ringbuf_t *data_buf_ptr, uint8_t **read_ptr_ptr, uint32_t *read_len_ptr)
{
    if (data_ringbuf_data_length(data_buf_ptr) == 0)
    {
        *read_len_ptr = 0;
        *read_ptr_ptr = NULL;
    }
    else
    {
        if (data_buf_ptr->next_byte_write_index <= data_buf_ptr->next_byte_read_index)
        {
            *read_len_ptr = data_buf_ptr->buf_size - data_buf_ptr->next_byte_read_index;
        }
        else
        {
            *read_len_ptr = data_buf_ptr->next_byte_write_index - data_buf_ptr->next_byte_read_index;
        }
        *read_ptr_ptr = data_buf_ptr->buf_ptr + data_buf_ptr->next_byte_read_index;
    }
}

uint32_t data_ringbuf_read(data_ringbuf_t *data_buf_ptr, uint8_t* dest_ptr, uint32_t read_len)
{
    uint32_t bytes_read = 0;
    if (data_buf_ptr->data_length < read_len)
    {
        return DATA_RINGBUF_STATUS_FAIL;
    }
    while (bytes_read < read_len)
    {
        uint8_t *next_read_ptr;
        uint32_t next_read_len;
        data_ringbuf_next_read_block(data_buf_ptr, &next_read_ptr, &next_read_len);
        if (next_read_len > (read_len - bytes_read))
        {
            next_read_len = read_len - bytes_read;
        }
        memcpy(dest_ptr + bytes_read, next_read_ptr, next_read_len);
        if (data_ringbuf_bytes_read(data_buf_ptr, next_read_len) != DATA_RINGBUF_STATUS_OK)
        {
            return DATA_RINGBUF_STATUS_FAIL;
        }
        bytes_read += next_read_len;
    }
    return DATA_RINGBUF_STATUS_OK;
}

uint32_t data_ringbuf_bytes_written(data_ringbuf_t *data_buf_ptr, uint32_t write_len)
{
    if (((data_buf_ptr->data_length + write_len) > data_buf_ptr->buf_size)
     || ((data_buf_ptr->next_byte_write_index + write_len) > data_buf_ptr->buf_size))
    {
        return DATA_RINGBUF_STATUS_FAIL;
    }
    data_buf_ptr->next_byte_write_index = (data_buf_ptr->next_byte_write_index + write_len) % data_buf_ptr->buf_size;
    data_buf_ptr->data_length += write_len;
    return DATA_RINGBUF_STATUS_OK;
}

uint32_t data_ringbuf_bytes_read(data_ringbuf_t *data_buf_ptr, uint32_t read_len)
{
    if ((data_buf_ptr->data_length < read_len)
    || ((data_buf_ptr->next_byte_read_index + read_len) > data_buf_ptr->buf_size))
    {
        return DATA_RINGBUF_STATUS_FAIL;
    }
    data_buf_ptr->next_byte_read_index = (data_buf_ptr->next_byte_read_index + read_len) % data_buf_ptr->buf_size;
    data_buf_ptr->data_length -= read_len;
    return DATA_RINGBUF_STATUS_OK;
}

uint32_t data_ringbuf_write(data_ringbuf_t *data_buf_ptr, uint8_t *buf_ptr, uint32_t buf_size)
{
    uint32_t free_space = data_ringbuf_free_space(data_buf_ptr);
    uint32_t buf_written = 0;

    // Check the pointer is not null
    if (buf_ptr == NULL)
    {
        return DATA_RINGBUF_STATUS_FAIL;
    }

    // Check there is enough space
    if (free_space < buf_size)
    {
        return DATA_RINGBUF_STATUS_FAIL;
    }

    while (buf_written < buf_size)
    {
        uint8_t *next_write_ptr;
        uint32_t next_write_len;
        data_ringbuf_next_write_block(data_buf_ptr, &next_write_ptr, &next_write_len);
        if (next_write_len > (buf_size - buf_written))
        {
            next_write_len = buf_size - buf_written;
        }
        memcpy(next_write_ptr, buf_ptr + buf_written, next_write_len);
        buf_written += next_write_len;
        data_ringbuf_bytes_written(data_buf_ptr, next_write_len);
    }
    return DATA_RINGBUF_STATUS_OK;
}

