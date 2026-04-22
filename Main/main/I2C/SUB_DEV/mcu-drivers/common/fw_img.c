/**
 * @file fw_img.c
 *
 * @brief The fw_img_v1 decode module
 *
 * @copyright
 * Copyright (c) Cirrus Logic 2021 All Rights Reserved, http://www.cirrus.com/
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
#include "I2C/SUB_DEV/mcu-drivers/common/fw_img.h"

/***********************************************************************************************************************
 * LOCAL FUNCTIONS
 **********************************************************************************************************************/

static uint32_t fw_img_copy_data_cs(fw_img_boot_state_t *state, uint32_t *data, uint32_t data_size, bool update_checksum)
{
    while ((state->count * sizeof(uint32_t)) < data_size &&
            state->fw_img_blocks < state->fw_img_blocks_end)
    {
        data[state->count++] = *(uint32_t *)state->fw_img_blocks;
        if (update_checksum && state->fw_info.preheader.img_format_rev != 1)
        {
            /* calculate checksum */
            state->c0 = (state->c0 + *(uint16_t *)state->fw_img_blocks) % FW_IMG_MODVAL;
            state->c1 = (state->c1 + state->c0) % FW_IMG_MODVAL;
            state->fw_img_blocks += sizeof(uint16_t);
            state->c0 = (state->c0 + *(uint16_t *)state->fw_img_blocks) % FW_IMG_MODVAL;
            state->c1 = (state->c1 + state->c0) % FW_IMG_MODVAL;
            state->fw_img_blocks += sizeof(uint16_t);
        }
        else
        {
            state->fw_img_blocks += sizeof(uint32_t);
        }
    }

    if ((state->count * sizeof(uint32_t)) == data_size)
        return FW_IMG_STATUS_AGAIN;
    else
        return FW_IMG_STATUS_NODATA;
}

/**
 * Copy data from input block to fw_img state member or output block buffer
 *
 * @param [in] state            Pointer to the fw_img boot state
 * @param [in,out] data         Pointer to output buffer
 * @param [in] data_size        Total number of uint32_t words to copy
 *
 * @return
 * - FW_IMG_STATUS_AGAIN        if copied all data from input buffer
 * - FW_IMG_STATUS_NODATA       otherwise
 *
 */
static uint32_t fw_img_copy_data(fw_img_boot_state_t *state, uint32_t *data, uint32_t data_size)
{
    return fw_img_copy_data_cs(state, data, data_size, true);
}

/**
 * Run through fw_img processing state machine
 *
 * The state machine will transition through reading the various sections of a fw_img file.
 *
 * @param [in] state            Pointer to the fw_img boot state
 *
 * @return
 * - FW_IMG_STATUS_AGAIN        if all of the current section of the fw_img file was processed
 * - FW_IMG_STATUS_FAIL if:
 *      - output block data size is smaller than the size of processed input data block
 *      - fw_img magic numbers were incorrect
 *      - fw_img checksum was incorrect
 *      - unknown state machine state
 * - FW_IMG_STATUS_DATA_READY   if output data block is ready
 * - FW_IMG_STATUS_OK           if fw_img checksum was correctly processed - processing is complete
 *
 */
static uint32_t fw_img_process_data(fw_img_boot_state_t *state)
{
    uint32_t ret = FW_IMG_STATUS_AGAIN;
    fw_img_info_t *fw_info = &state->fw_info;

    switch (state->state)
    {
        case FW_IMG_BOOT_STATE_INIT:
            state->count = 0;
            break;

        case FW_IMG_BOOT_STATE_READ_SYMBOLS:
            ret = fw_img_copy_data(state, (uint32_t *) fw_info->sym_table, fw_info->header.sym_table_size * sizeof(fw_img_v1_sym_table_t));
            break;

        case FW_IMG_BOOT_STATE_READ_ALGIDS:
            ret = fw_img_copy_data(state, fw_info->alg_id_list, fw_info->header.alg_id_list_size * sizeof(fw_info->header.alg_id_list_size));
            break;

        case FW_IMG_BOOT_STATE_READ_DATA_HEADER:
            if (fw_info->header.data_blocks > 0)
            {
                ret = fw_img_copy_data(state, (uint32_t *)&state->block, sizeof(state->block));
            }
            else
            {
                state->state = FW_IMG_BOOT_STATE_READ_MAGICNUM2 - 1; // Will be incremented
            }
            break;

        case FW_IMG_BOOT_STATE_WRITE_DATA:
            if (state->block.block_size > state->block_data_size)
            {
                ret = FW_IMG_STATUS_FAIL;
                break;
            }
            ret = fw_img_copy_data(state, (uint32_t *)state->block_data, state->block.block_size);
            if (ret == FW_IMG_STATUS_AGAIN)
            {
                ret = FW_IMG_STATUS_DATA_READY;
                state->count = 0;
                fw_info->header.data_blocks--;
                state->state = FW_IMG_BOOT_STATE_READ_DATA_HEADER;
            }
            break;

        case FW_IMG_BOOT_STATE_READ_MAGICNUM2:
            ret = fw_img_copy_data(state, (uint32_t *)&state->img_magic_number_2, sizeof(state->img_magic_number_2));
            if (ret == FW_IMG_STATUS_AGAIN)
            {
                if (state->img_magic_number_2 != FW_IMG_BOOT_FW_IMG_V1_MAGIC_2)
                {
                    ret = FW_IMG_STATUS_FAIL;
                }
                else
                {
                    state->state = FW_IMG_BOOT_STATE_READ_CHECKSUM - 1; // Will be incremented
                }
            }
            break;

        case FW_IMG_BOOT_STATE_READ_CHECKSUM:
            ret = fw_img_copy_data_cs(state, (uint32_t *)&state->img_checksum, sizeof(state->img_checksum), false);
            if (ret == FW_IMG_STATUS_AGAIN)
            {
                if (fw_info->preheader.img_format_rev != 1 && state->img_checksum != (state->c0 + (state->c1 << 16)))
                {
                    ret = FW_IMG_STATUS_FAIL;
                }
                else
                {
                    state->state = FW_IMG_BOOT_STATE_DONE;
                    ret = FW_IMG_STATUS_OK;
                }
            }
            break;

        case FW_IMG_BOOT_STATE_DONE:
            state->state = FW_IMG_BOOT_STATE_INIT - 1; // Will be incremented
            break;

        default:
            ret = FW_IMG_STATUS_FAIL;
            break;
    }

    if (ret == FW_IMG_STATUS_AGAIN)
    {
        state->state++;
        state->count = 0;
    }

    return ret;
}

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/

/**
 * Read fw_img header
 *
 */
uint32_t fw_img_read_header(fw_img_boot_state_t *state)
{
    uint32_t ret = FW_IMG_STATUS_OK;
    fw_img_info_t *fw_info = &state->fw_info;

    if (state == NULL || state->fw_img_blocks == NULL || state->fw_img_blocks_size ==  0)
    {
        return FW_IMG_STATUS_FAIL;
    }

    state->fw_img_blocks_end = state->fw_img_blocks + state->fw_img_blocks_size;

    ret = fw_img_copy_data(state, (uint32_t *)&fw_info->preheader, sizeof(fw_img_preheader_t));
    if (ret != FW_IMG_STATUS_AGAIN ||
        fw_info->preheader.img_magic_number_1 != FW_IMG_BOOT_FW_IMG_V1_MAGIC_1)
    {
        ret = FW_IMG_STATUS_FAIL;
    }
    else
    {
        state->count = 0;
        switch (fw_info->preheader.img_format_rev)
        {
            case 1:
                ret = fw_img_copy_data(state, (uint32_t *)(&fw_info->header), sizeof(fw_img_v1_header_t));
                if (ret != FW_IMG_STATUS_AGAIN)
                {
                    ret = FW_IMG_STATUS_FAIL;
                }
                state->count = 0;
                break;
            case 2:
                ret = fw_img_copy_data(state, (uint32_t *)(&fw_info->header), sizeof(fw_img_v2_header_t));
                if (ret != FW_IMG_STATUS_AGAIN)
                {
                    ret = FW_IMG_STATUS_FAIL;
                }
                state->count = 0;
                break;
            default:
                ret = FW_IMG_STATUS_FAIL;
                break;
        }
    }

    return FW_IMG_STATUS_OK;
}

/**
 * Process more fw_img bytes
 *
 */
uint32_t fw_img_process(fw_img_boot_state_t *state)
{
    uint32_t ret = FW_IMG_STATUS_OK;

    if (state == NULL || state->fw_img_blocks == NULL || state->fw_img_blocks_size == 0 || state->block_data == NULL)
    {
        return FW_IMG_STATUS_FAIL;
    }

    if (state->fw_img_blocks_end == NULL)
    {
        state->fw_img_blocks_end = state->fw_img_blocks + state->fw_img_blocks_size;
    }

    do {
        ret = fw_img_process_data(state);
    } while (ret == FW_IMG_STATUS_AGAIN);

    if (ret == FW_IMG_STATUS_NODATA)
    {
        state->fw_img_blocks_end = NULL;
    }

    return ret;
}

/**
 * Find if a symbol is in the symbol table and return its address if it is.
 *
 */
uint32_t fw_img_find_symbol(fw_img_info_t *fw_info, uint32_t symbol_id)
{
    if (fw_info)
    {
        for (uint32_t i = 0; i < fw_info->header.sym_table_size; i++)
        {
            if (fw_info->sym_table[i].sym_id == symbol_id)
            {
                return fw_info->sym_table[i].sym_addr;
            }
        }
    }

    return 0;
}

/**
 * Find if an algorithm is in the algorithm list.
 */
bool fw_img_find_algid(fw_img_info_t *fw_info, uint32_t alg_id)
{
    if (fw_info)
    {
        for (uint32_t i = 0; i < fw_info->header.alg_id_list_size; i++)
        {
            if (fw_info->alg_id_list[i] == alg_id)
                return true;
        }
    }

    return false;
}
