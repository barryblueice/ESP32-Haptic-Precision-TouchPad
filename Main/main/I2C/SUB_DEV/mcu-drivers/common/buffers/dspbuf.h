/**
 * @file dspbuf.h
 *
 * @brief Functions and prototypes exported by the DSP compressed read buffer module
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

#ifndef DSPBUF_H
#define DSPBUF_H

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/
#include "I2C/SUB_DEV/mcu-drivers/common/buffers/data_ringbuf.h"
#include "I2C/SUB_DEV/mcu-drivers/common/regmap.h"

/***********************************************************************************************************************
 * LITERALS & CONSTANTS
 **********************************************************************************************************************/
 /**
 * @defgroup DSPBUF_
 * @brief Values for communicating with DSP buffer
 *
 * @{
 */
#define DSPBUF_MAX_N_BUFFERS                      3

#define DSPBUF_BUF_STATUS_OK                      (0)
#define DSPBUF_BUF_STATUS_ERROR_OVERFLOW          (1<<0)
#define DSPBUF_BUF_STATUS_ERROR_REWIND_ARG        (1<<1)
#define DSPBUF_BUF_STATUS_ERROR_REWIND_TWICE      (1<<2)
#define DSPBUF_BUF_STATUS_ERROR_RESERVE_TWICE     (1<<3)
#define DSPBUF_BUF_STATUS_ERROR_INVALID_RESERVE   (1<<4)
#define DSPBUF_BUF_STATUS_ERROR_INCONSISTENT      (1<<5)
#define DSPBUF_BUF_STATUS_ERROR_CORRUPT           (1<<6)
#define DSPBUF_BUF_STATUS_ERROR_PREV_BLOCK_REWIND (1<<7)
#define DSPBUF_BUF_STATUS_ERROR_TRUNCATED         (1<<8)
#define DSPBUF_BUF_STATUS_ERROR_OVERRUN_AT_START  (1<<9)

/***********************************************************************************************************************
 * MACROS
 **********************************************************************************************************************/

#define DSPBUF_STATUS_OK                          (0)
#define DSPBUF_STATUS_FAIL                        (1)

/***********************************************************************************************************************
 * ENUMS, STRUCTS, UNIONS, TYPEDEFS
 **********************************************************************************************************************/

/**
 * Configuration structure for defining the location and size of a dsp buffer
 */
typedef struct
{
    uint32_t base_id;
    uint32_t size_id;
    uint32_t mem_base;
} dspbuf_loc_config_t;

/**
 * Data structure for storing a DSP individual buffer location and size
 */
 typedef struct
 {
     uint32_t base;
     uint32_t start_offset;
     uint32_t end_offset;
 } dspbuf_loc_t;

/**
 * Data structure for tracking DSP's ring buffer
 *
 * @see cs47l63_init_dsp_buffers
 */
typedef struct
{
    dspbuf_loc_t dspbuf_locs[DSPBUF_MAX_N_BUFFERS];
    uint32_t total_bufs_size;
    uint32_t high_water_mark;
    uint32_t irq_count;
    uint32_t irq_ack;
    uint32_t next_word_write_index;
    uint32_t next_word_read_index;
    uint32_t error;
    uint32_t space_avail;
    uint32_t data_avail;
    uint32_t buf_size;
} dspbuf_ringbuf_t;

typedef struct
{
    regmap_cp_config_t *cp;
    dspbuf_loc_config_t bufs_config[DSPBUF_MAX_N_BUFFERS];
    uint32_t rb_struct_mem_start_address;
    uint8_t *compr_buf_ptr;
    uint32_t compr_buf_size;
    uint32_t buf_symbol;
    uint32_t bytes_per_reg;
} dspbuf_config_t;

/**
 * Data structure to hold anything buffer-related
 *
 * @see cs47l63_init_dsp_buffers
 */
typedef struct
{
    dspbuf_config_t config;
    uint32_t rb_struct_base_addr;
    dspbuf_ringbuf_t ring_buf;
    data_ringbuf_t compr_data_buf;
    data_ringbuf_t decompr_data_buf;
} dspbuf_t;

/**
 * Data structure to identify DSP buffer elements
 *
 * @see dspbuf_get_value
 * @see dspbuf_set_value
 * @see cs47l63_update_writeIndex_irqAck_eof
 * @see dspbuf_struct_init
 */
typedef enum
{
    buf1_base=0,
    buf1_size,
    buf2_base,
    buf1_buf2_size,
    buf3_base,
    total_buf_size,
    high_water_mark,
    irq_count,
    irq_ack,
    next_word_write_index,
    next_word_read_index,
    error
} dspbuf_struct_offsets_t;

/***********************************************************************************************************************
 * GLOBAL VARIABLES
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/

/**
 * Initialize struct with buffers needed to receive data from the dsp
 *
 * @param [in]
 * - dspbuf           Pointer to the DSP buffer structure
 * - dspbuf_config    Pointer to config for the DSP structure
 *
 * @return
 * - DSPBUF_STATUS_FAIL        Control port activity fails
 * - DSPBUF_STATUS_OK          otherwise
 *
 * @see dspbuf_struct_init
 *
 */
uint32_t dspbuf_init(dspbuf_t *dspbuf, dspbuf_config_t *dspbuf_config);

/**
 * Read data from dsp ring buffer
 *
 * If data has already started streaming, it should only be called after IRQ signal from DSP, and after determining
 * that there is data available in buffer
 *
 * @param [in]
 * - dspbuf              Pointer to DSP buffer structure
 * - data_buf            Pointer to data buffer structure to store incoming data
 * - data_len            Number of bytes to read from data array. Should not be longer than avail data in dsp buffer
 *                       or longer than the allocated buffer.
 *
 * @return
 * - DSPBUF_STATUS_FAIL         Control port activity fails
 * - DSPBUF_STATUS_OK           otherwise
 *
 * @see dspbuf_data_avail
 *
 */
uint32_t dspbuf_read(dspbuf_t *dspbuf, data_ringbuf_t *data_buf, uint32_t data_len, uint32_t *data_read);

/**
 * Update the amount of available data on DSP encoder
 *
 * @param [in]
 * - dspbuf           Pointer to DSP buffer structure
 *
 * @return
 * - DSPBUF_STATUS_FAIL         Control port activity fails
 * - DSPBUF_STATUS_OK          otherwise
 *
 */
uint32_t dspbuf_data_avail(dspbuf_t *dspbuf);

/**
 * Acknowledge the DSP IRQ which re-enables it
 *
 * @param [in]
 * - dspbuf           Pointer to DSP buffer structure
 *
 * @return
 * - DSPBUF_STATUS_FAIL        Control port activity fails
 * - DSPBUF_STATUS_OK          otherwise
 *
 */
uint32_t dspbuf_reenable_irq(dspbuf_t *dspbuffer);

/**
 * Read the current status of the DSP ring buffer
 *
 * @param [in]
 * - dspbuf           Pointer to DSP buffer structure
 *
 * @return
 * - DSPBUF_STATUS_FAIL        Control port activity fails
 * - DSPBUF_STATUS_OK          otherwise
 *
 */
uint32_t dspbuf_update_status(dspbuf_t *dspbuffer);

/**
 * Get the current error status of the DSP ring buffer
 *
 * @param [in]
 * - dspbuf           Pointer to DSP buffer structure
 *
 * @return
 * - DSPBUF_STATUS_FAIL        Control port activity fails
 * - DSPBUF_STATUS_OK          otherwise
 *
 */
uint32_t dspbuf_get_error(dspbuf_t *dsp_buffer);

/**
 * Get the length of compressed data in bytes available to read
 *
 * @param [in]
 * - dspbuf           Pointer to DSP buffer structure
 *
 * @return            The number of compressed bytes available to read
 *
 */
uint32_t dspbuf_get_data_avail(dspbuf_t *dspbuf);

/**********************************************************************************************************************/
#ifdef __cplusplus
}
#endif

#endif // DSPBUF_H
