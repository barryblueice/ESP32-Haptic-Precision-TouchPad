/**
 * @file platform_bsp.h
 *
 * @brief Functions and prototypes exported by the Platform BSP module
 *
 * @copyright
 * Copyright (c) Cirrus Logic 2021-2024 All Rights Reserved, http://www.cirrus.com/
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

#ifndef PLATFORM_BSP_H
#define PLATFORM_BSP_H

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/
#include "I2C/SUB_DEV/mcu-drivers/common/bsp_driver_if.h"
#include <stdbool.h>
#include <stddef.h>
#include "I2C/SUB_DEV/mcu-drivers/cs40l25/bsp/bsp_dut.h"
#include <stdio.h>

/***********************************************************************************************************************
 * LITERALS & CONSTANTS
 **********************************************************************************************************************/
#define BSP_DEV_ID_NULL                 (0)
#define BSP_DUT_DEV_ID                  (1)
#define BSP_LN2_DEV_ID                  (2)
#define BSP_DUT_DEV_ID_SPI2             (3)
#define BSP_INTP_EXP_DEV_ID             (4)
#define BSP_EEPROM_DEV_ID               (5)
#define BSP_DUT_DEV_ID_I2C2             (6)
#define BSP_DUT_DEV_ID_BROADCAST        (7)

#define BSP_GPIO_ID_NULL                (0)
#define BSP_GPIO_ID_DUT_CDC_RESET       (1)
#define BSP_GPIO_ID_DUT_DSP_RESET       (2)
#define BSP_GPIO_ID_DUT_CDC_INT         (3)
#define BSP_GPIO_ID_DUT_DSP_INT         (4)
#define BSP_GPIO_ID_LN2_CDC_GPIO1       (5)
#define BSP_GPIO_ID_GF_GPIO7            (6)
#define BSP_GPIO_ID_GF_GPIO2            (7)
#define BSP_GPIO_ID_INTP_LED1           (8)
#define BSP_GPIO_ID_INTP_LED2           (9)
#define BSP_GPIO_ID_INTP_LED3           (10)
#define BSP_GPIO_ID_INTP_LED4           (11)
#define BSP_GPIO_ID_INTP_LED_ALL        (12)
#define BSP_GPIO_ID_INTP_LED5           (13)
#define BSP_GPIO_ID_GF_GPIO10           (14)
#define BSP_GPIO_ID_GF_GPIO11           (15)
#define BSP_GPIO_ID_GF_GPIO12           (16)
#define BSP_GPIO_ID_GF_GPIO13           (17)

#define BSP_SUPPLY_ID_LN2_DCVDD         (1)

#define BSP_PB_ID_USER                  (0)
#define BSP_PB_ID_SW1                   (1)
#define BSP_PB_ID_SW2                   (2)
#define BSP_PB_ID_SW3                   (3)
#define BSP_PB_ID_SW4                   (4)
#define BSP_PB_ID_NUM                   (5)

#define BSP_PLAY_SILENCE                (0)
#define BSP_PLAY_STEREO_1KHZ_20DBFS     (1)
#define BSP_PLAY_STEREO_100HZ_20DBFS    (2)
#define BSP_PLAY_STEREO_PATTERN         (3)
#define BSP_PLAY_STEREO_TEST            (4)

#define BSP_BUS_TYPE_I2C                (0)
#define BSP_BUS_TYPE_SPI                (1)

#define BSP_STATUS_DUT_EVENTS           (2)

#define BSP_AUDIO_FS_8000_HZ            (8000)
#define BSP_AUDIO_FS_16000_HZ           (16000)
#define BSP_AUDIO_FS_48000_HZ           (48000)
#define BSP_AUDIO_FS_44100_HZ           (44100)

#define BSP_LD2_MODE_OFF                (0)
#define BSP_LD2_MODE_ON                 (1)
#define BSP_LD2_MODE_BLINK              (2)

#define BSP_GPIO_ID_LD2                 (0)

#ifndef BSP_DUT_I2C_ADDRESS_8BIT
#define BSP_DUT_I2C_ADDRESS_8BIT (0x60)
#endif

#define BSP_DUT_I2C2_ADDRESS_8BIT (0x62)
#define BSP_DUT_I2C_BROADCAST_ADDRESS_8BIT (0x88)

/* Commands available for AT25SL128A EEPROM on RevB interposer */
#define BSP_EEPROM_OPCODE_WRITE_ENABLE              (0x06)
#define BSP_EEPROM_OPCODE_WRITE_DISBLE              (0x04)
#define BSP_EEPROM_OPCODE_READ_STS_REG_1            (0x05)
#define BSP_EEPROM_OPCODE_READ_STS_REG_2            (0x35)
#define BSP_EEPROM_OPCODE_READ_DATA                 (0x03)
#define BSP_EEPROM_OPCODE_PAGE_PROGRAM              (0x02)
#define BSP_EEPROM_OPCODE_CHIP_ERASE                (0xC7)
#define BSP_EEPROM_OPCODE_READ_JEDEC_ID             (0x9F)
#define BSP_EEPROM_OPCODE_RESET_ENABLE              (0x66)
#define BSP_EEPROM_OPCODE_RESET                     (0x99)
#define BSP_EEPROM_OPCODE_BLOCK_ERASE_4KB           (0x20)
#define BSP_EEPROM_OPCODE_BLOCK_ERASE_32KB          (0x52)
#define BSP_EEPROM_OPCODE_BLOCK_ERASE_64KB          (0xD8)

/***********************************************************************************************************************
 * MACROS
 **********************************************************************************************************************/
// Control flow escape for local exception handing. Throws to the corresponding catch within the same function.
#define CRUS_THROW(exception) goto exception //NOSONAR

// Control flow escape for local exception handing. Catches a local throw from within the same function.
#define CRUS_CATCH(exception) while(0) exception:

/***********************************************************************************************************************
 * ENUMS, STRUCTS, UNIONS, TYPEDEFS
 **********************************************************************************************************************/
typedef void (*bsp_app_callback_t)(uint32_t status, void *arg);
typedef enum
{
    BSP_I2S_PORT_PRIMARY = 0,
    BSP_I2S_PORT_SECONDARY,
    BSP_I2S_PORT_NULL
} bsp_i2s_port_t;

/***********************************************************************************************************************
 * GLOBAL VARIABLES
 **********************************************************************************************************************/
extern bool trigger_audio_change;
extern bool bsp_write_process_done;
extern bool bsp_read_process_done;

extern FILE* test_file;
extern FILE* coverage_file;
extern FILE* bridge_write_file;
extern FILE* bridge_read_file;

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/
uint32_t bsp_initialize(bsp_app_callback_t cb, void *cb_arg);
uint32_t bsp_audio_set_fs(uint32_t fs_hz);
uint32_t bsp_audio_play(bsp_i2s_port_t port, uint8_t content);
uint32_t bsp_audio_play_record(bsp_i2s_port_t port, uint8_t content);
uint32_t bsp_audio_play_stream(bsp_i2s_port_t port,
                               uint8_t *content,
                               uint32_t length,
                               bsp_callback_t half_cb,
                               void *half_cb_arg,
                               bsp_callback_t cplt_cb,
                               void *cplt_cb_arg);
uint32_t bsp_audio_continue_play_stream(bsp_i2s_port_t port, uint8_t *content, uint32_t length);
uint32_t bsp_audio_pause(bsp_i2s_port_t port);
uint32_t bsp_audio_resume(bsp_i2s_port_t port);
uint32_t bsp_audio_stop(bsp_i2s_port_t port);
uint32_t bsp_set_timer(uint32_t duration_ms, bsp_callback_t cb, void *cb_arg);
uint32_t bsp_set_gpio(uint32_t gpio_id, uint8_t gpio_state);
bool     bsp_was_pb_pressed(uint8_t pb_id);
void     bsp_sleep(void);
uint32_t bsp_register_pb_cb(uint32_t pb_id, bsp_app_callback_t cb, void *cb_arg);
void     bsp_notification_callback(uint32_t event_flags, void *arg);
uint32_t bsp_i2c_read_repeated_start(uint32_t bsp_dev_id,
                                     uint8_t *write_buffer,
                                     uint32_t write_length,
                                     uint8_t *read_buffer,
                                     uint32_t read_length,
                                     bsp_callback_t cb,
                                     void *cb_arg);
uint32_t bsp_i2c_write(uint32_t bsp_dev_id,
                       uint8_t *write_buffer,
                       uint32_t write_length,
                       bsp_callback_t cb,
                       void *cb_arg);
uint32_t bsp_set_ld2(uint8_t mode, uint32_t blink_100ms);
uint32_t bsp_toggle_gpio(uint32_t gpio_id);
uint32_t bsp_eeprom_control(uint8_t command);
uint32_t bsp_eeprom_read_status(uint8_t *buffer);
uint32_t bsp_eeprom_read_jedecid(uint8_t *buffer);
uint32_t bsp_eeprom_read(uint32_t addr,
                         uint8_t *data_buffer,
                         uint32_t data_length);
uint32_t bsp_eeprom_program(uint32_t addr,
                            uint8_t *data_buffer,
                            uint32_t data_length);
uint32_t bsp_eeprom_program_verify(uint32_t addr,
                                   uint8_t *data_buffer,
                                   uint32_t data_length);
uint32_t bsp_eeprom_erase(uint8_t command, uint32_t addr);
uint32_t bsp_set_led(uint32_t index, uint8_t mode, uint32_t blink_100ms);
void bsp_get_switch_state_changes(uint8_t *state, uint8_t *change_mask);

/**********************************************************************************************************************/
#ifdef __cplusplus
}
#endif

#endif // PLATFORM_BSP_H
