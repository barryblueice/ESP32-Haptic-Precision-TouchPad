/**
 * @file data_ringbuf.h
 *
 * @brief Functions and prototypes for data ring buffer handling
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

#ifndef DATA_RINGBUF_H
#define DATA_RINGBUF_H

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/
#include <stdint.h>

/***********************************************************************************************************************
 * LITERALS & CONSTANTS
 **********************************************************************************************************************/
 /**
 * @defgroup DATA_RINGBUF_
 * @brief Return values for data ring buffer API
 *
 * @{
 */
#define DATA_RINGBUF_STATUS_OK                    (0)
#define DATA_RINGBUF_STATUS_FAIL                  (1)

/***********************************************************************************************************************
 * MACROS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * ENUMS, STRUCTS, UNIONS, TYPEDEFS
 **********************************************************************************************************************/


/**
 * Data structure for storing a ring buffer of data
 */
 typedef struct
 {
     uint8_t *buf_ptr;
     uint32_t buf_size;
     uint32_t data_length;
     uint32_t next_byte_write_index;
     uint32_t next_byte_read_index;
 } data_ringbuf_t;

/***********************************************************************************************************************
 * GLOBAL VARIABLES
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/

/**
 * Initialize data ring buffer struct
 *
 * @param [in]
 * - data_buf_ptr     Pointer to the data ring buffer structure
 * - buf_ptr          Pointer to the memory to use for the ring buffer data
 * - buf_size         Size of the ring buffer data memory (in bytes)
 *
 */
void data_ringbuf_init(data_ringbuf_t *data_buf_ptr, uint8_t *buf_ptr, uint32_t buf_size);

/**
 * Return the number of unused bytes in the data ring buffer
 *
 * @param [in]
 * - data_buf_ptr     Pointer to the data ring buffer structure
 *
 * @return
 * - uint32_t         The amount of free space in the buffer (in bytes)
 *
 */
uint32_t data_ringbuf_free_space(data_ringbuf_t *data_buf_ptr);

/**
 * Return the number of bytes in the data ring buffer
 *
 * @param [in]
 * - data_buf_ptr     Pointer to the data ring buffer structure
 *
 * @return
 * - uint32_t         The number of bytes in the buffer
 *
 */
uint32_t data_ringbuf_data_length(data_ringbuf_t *data_buf_ptr);

/**
 * Retrieve a raw pointer and length representing the next contiguous area that can be written to the buffer
 *
 * @param [in]
 * - data_buf_ptr     Pointer to the data ring buffer structure
 *
 * @param [out]
 * - write_ptr_ptr    Pointer to a byte pointer that will be set to point to the next contiguous write area
 * - write_len_ptr    Pointer to a length that will be set to the length of the next contiguous write area
 *
 */
void data_ringbuf_next_write_block(data_ringbuf_t *data_buf_ptr, uint8_t **write_ptr_ptr, uint32_t *write_len_ptr);

/**
 * Retrieve a raw pointer and length representing the next contiguous area that can be read from the buffer
 *
 * @param [in]
 * - data_buf_ptr     Pointer to the data ring buffer structure
 *
 * @param [out]
 * - read_ptr_ptr    Pointer to a byte pointer that will be set to point to the next contiguous read area
 * - read_len_ptr    Pointer to a length that will be set to the length of the next contiguous read area
 *
 */
void data_ringbuf_next_read_block(data_ringbuf_t *data_buf_ptr, uint8_t **read_ptr_ptr, uint32_t *read_len_ptr);

/**
 * Read bytes from the data ring buffer.  Any bytes read will be removed from the buffer.
 *
 * @param [in]
 * - data_buf_ptr     Pointer to the data ring buffer structure
 * - dest_ptr         Pointer to the location to read the data to
 * - read_len         The number of bytes to read
 *
 * @return
 * - DATA_RINGBUF_STATUS_FAIL       The read failed due to there not being enough data in the buffer
 * - DATA_RINGBUF_STATUS_OK         The requested data was read and removed from the buffer
 *
 */
uint32_t data_ringbuf_read(data_ringbuf_t *data_buf_ptr, uint8_t* dest_ptr, uint32_t read_len);

/**
 * Increment the next write location in the buffer as a result of data being copied in using a raw write pointer
 *
 * @param [in]
 * - data_buf_ptr     Pointer to the data ring buffer structure
 * - write_len        The number of bytes written into the buffer
 *
 * @return
 * - DATA_RINGBUF_STATUS_FAIL       The write failed due to there not being enough space in the buffer
 * - DATA_RINGBUF_STATUS_OK         The next write index was incremented
 *
 */
uint32_t data_ringbuf_bytes_written(data_ringbuf_t *data_buf_ptr, uint32_t write_len);

/**
 * Increment the next read location in the buffer, discarding the read data
 *
 * @param [in]
 * - data_buf_ptr     Pointer to the data ring buffer structure
 * - read_len         The number of bytes read from the buffer
 *
 * @return
 * - DATA_RINGBUF_STATUS_FAIL       The read failed due to there not being enough data in the buffer
 * - DATA_RINGBUF_STATUS_OK         The next read index was incremented
 *
 */
uint32_t data_ringbuf_bytes_read(data_ringbuf_t *data_buf_ptr, uint32_t read_len);

/**
 * Write a number of bytes into the buffer
 *
 * @param [in]
 * - data_buf_ptr     Pointer to the data ring buffer structure
 * - buf_ptr          A pointer to the data to write
 * - buf_size         The number of bytes to write into the ring buffer
 *
 * @return
 * - DATA_RINGBUF_STATUS_FAIL       The write failed due to there not being enough space in the buffer
 * - DATA_RINGBUF_STATUS_OK         The data was written and the next write index was incremented
 *
 */
uint32_t data_ringbuf_write(data_ringbuf_t *data_buf_ptr, uint8_t *buf_ptr, uint32_t buf_size);

/**********************************************************************************************************************/
#ifdef __cplusplus
}
#endif

#endif // DATA_RINGBUF_H
