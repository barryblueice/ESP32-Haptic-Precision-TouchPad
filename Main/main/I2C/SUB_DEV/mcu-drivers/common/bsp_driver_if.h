/**
 * @file bsp_driver_if.h
 *
 * @brief Functions and prototypes that define the BSP-to-Device Driver Interface
 *
 * @copyright
 * Copyright (c) Cirrus Logic 2019-2020, 2022 All Rights Reserved, http://www.cirrus.com/
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

#ifndef BSP_DRIVER_IF_H
#define BSP_DRIVER_IF_H

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/***********************************************************************************************************************
 * LITERALS & CONSTANTS
 **********************************************************************************************************************/

/**
 * @defgroup BSP_STATUS_
 * @brief Return values for all public and most private API calls
 *
 * @{
 */
#define BSP_STATUS_OK               (0)
#define BSP_STATUS_FAIL             (1)
/** @} */

/**
 * @defgroup BSP_TIMER_DURATION_
 * @brief Values used for calls to BSP timer APIs
 *
 * @see bsp_driver_if_t.set_timer
 *
 * @{
 */
#define BSP_TIMER_DURATION_1MS      (1)
#define BSP_TIMER_DURATION_2MS      (2)
#define BSP_TIMER_DURATION_5MS      (5)
#define BSP_TIMER_DURATION_10MS     (10)
#define BSP_TIMER_DURATION_2S       (2000)
/** @} */

/**
 * Value to indicate driving a GPIO low
 *
 * @see bsp_driver_if_t.set_gpio
 *
 */
#define BSP_GPIO_LOW                (0)

/**
 * Value to indicate driving a GPIO high
 *
 * @see bsp_driver_if_t.set_gpio
 *
 */
#define BSP_GPIO_HIGH               (1)

/**
 * Value to indicate enabling or disabling a supply
 *
 * @see bsp_driver_if_t.set_supply
 *
 */
#define BSP_SUPPLY_DISABLE              (0)
#define BSP_SUPPLY_ENABLE               (1)


/***********************************************************************************************************************
 * MACROS
 **********************************************************************************************************************/

/**
 * Macro to extract byte from multi-byte word
 *
 * @param [in] A                multi-byte word
 * @param [in] B                zero-indexed byte position
 *
 * @return                      byte at position B in word A
 */
#define GET_BYTE_FROM_WORD(A, B)   ((A >> (B * 8)) & 0xFF)

/**
 * Macro to insert byte into multi-byte word
 *
 * @param [in, out] A           multi-byte word
 * @param [in] B                byte value
 * @param [in] C                zero-indexed byte position
 *
 * @return none
 */
#define ADD_BYTE_TO_WORD(A, B, C) \
{ \
    A &= (0xFFFFFF00 << (C * 8)); \
    A |= ((B & 0xFF) << (C * 8)); \
}

/***********************************************************************************************************************
 * ENUMS, STRUCTS, UNIONS, TYPEDEFS
 **********************************************************************************************************************/

/**
 * Callback type for BSP-to-Driver callbacks
 *
 * @param [in] status           Result of BSP call
 * @param [in] arg              Argument registered when BSP call was issued
 *
 * @return none
 *
 * @see BSP_STATUS_
 *
 */
typedef void (*bsp_callback_t)(uint32_t status, void* arg);

/**
 * BSP-to-Driver public API
 *
 * All API calls return a status @see CS35L41_STATUS_
 *
 */
typedef struct
{
    /**
     * Set GPIO to LOW/HIGH
     *
     * @param [in] gpio_id      ID for GPIO to change - can be defined in implementation header
     * @param [in] gpio_state   LOW or HIGH
     *
     * @return
     * - BSP_STATUS_FAIL        if gpio_id is invalid, or call to MCU HAL fails
     * - BSP_STATUS_OK          otherwise
     *
     * @see BSP_GPIO_LOW BSP_GPIO_HIGH
     *
     */
    uint32_t (*set_gpio)(uint32_t gpio_id, uint8_t gpio_state);

    /**
     * Enable or disable a supply
     *
     * @param [in] supply_id      ID for supply to change - can be defined in implementation header
     * @param [in] supply_state   Enable or Disable
     *
     * @return
     * - BSP_STATUS_FAIL        if gpio_id is invalid, or call to MCU HAL fails
     * - BSP_STATUS_OK          otherwise
     *
     * @note this function shouldn't return until the supply has finished rising or falling
     *
     * @see BSP_SUPPLY_ENABLE BSP_SUPPLY_DISABLE
     *
     */
    uint32_t (*set_supply)(uint32_t supply_id, uint8_t supply_state);

    /**
     * Register GPIO Callback
     *
     * Register a callback for when a GPIO changes state.
     *
     * @param [in] gpio_id      ID for GPIO to change - can be defined in implementation header
     * @param [in] cb           pointer to callback function
     * @param [in] cb_arg       pointer to argument to use when calling callback
     *
     * @return
     * - BSP_STATUS_FAIL        if gpio_id is invalid, if any pointers are NULL
     * - BSP_STATUS_OK          otherwise
     *
     */
    uint32_t (*register_gpio_cb)(uint32_t gpio_id, bsp_callback_t cb, void *cb_arg);

    /**
     * Set a timer to expire
     *
     * @param [in] duration_ms  Duration of timer in milliseconds
     * @param [in] cb           pointer to callback function
     * @param [in] cb_arg       pointer to argument to use when calling callback
     *
     * @return
     * - BSP_STATUS_FAIL        if duration_ms is invalid, if any pointers are NULL
     * - BSP_STATUS_OK          otherwise
     *
     */
    uint32_t (*set_timer)(uint32_t duration_ms, bsp_callback_t cb, void *cb_arg);

    /**
     * Reset I2C Port used for a specific device
     *
     * Abort the current I2C transaction and reset the I2C peripheral.  This is required for quickly handling of
     * CS35L41 IRQ events.
     *
     * @param [in] bsp_dev_id       ID of the I2C device corresponding to the I2C peripheral to reset
     * @param [out] was_i2c_busy    flag to indicate whether an I2C transaction was in progress when reset
     *
     * @return
     * - BSP_STATUS_FAIL        if bsp_dev_id is invalid
     * - BSP_STATUS_OK          otherwise
     *
     */
    uint32_t (*i2c_reset)(uint32_t bsp_dev_id, bool *was_i2c_busy);

    /**
     * Perform an I2C Write-Repeated Start-Read transaction
     *
     * This is the common way to read data from an I2C device with a register file, since the address of the
     * register to read must first be written to the device before reading any contents.
     *
     * Perform transaction in the order:
     * 1. I2C Start
     * 2. I2C write of \b write_length bytes from \b write_buffer
     * 3. I2C Repeated Start
     * 4. I2C read of \b read_length bytes into \b read_buffer
     * 5. I2C Stop
     *
     * BSP will decode \b bsp_dev_id to the correct I2C bus and I2C address.
     *
     * @param [in] bsp_dev_id       ID of the I2C device corresponding to the I2C peripheral to reset
     * @param [in] write_buffer     pointer to array of bytes to write
     * @param [in] write_length     total number of bytes in \b write_buffer
     * @param [in] read_buffer      pointer to array of bytes to load with I2C bytes read
     * @param [in] read_length      total number of bytes to read into \b read_buffer
     * @param [in] cb               pointer to callback function
     * @param [in] cb_arg           pointer to argument to use when calling callback
     *
     * @return
     * - BSP_STATUS_FAIL            if bsp_dev_id is invalid, if any portion of I2C transaction failed
     * - BSP_STATUS_OK              otherwise
     *
     */
    uint32_t (*i2c_read_repeated_start)(uint32_t bsp_dev_id,
                                        uint8_t *write_buffer,
                                        uint32_t write_length,
                                        uint8_t *read_buffer,
                                        uint32_t read_length,
                                        bsp_callback_t cb,
                                        void *cb_arg);

    /**
     * Perform I2C Write
     *
     * BSP will decode \b bsp_dev_id to the correct I2C bus and I2C address.
     *
     * @param [in] bsp_dev_id       ID of the I2C device corresponding to the I2C peripheral to reset
     * @param [in] write_buffer     pointer to array of bytes to write
     * @param [in] write_length     total number of bytes in \b write_buffer
     * @param [in] cb               pointer to callback function
     * @param [in] cb_arg           pointer to argument to use when calling callback
     *
     * @return
     * - BSP_STATUS_FAIL            if bsp_dev_id is invalid, if any portion of I2C transaction failed
     * - BSP_STATUS_OK              otherwise
     *
     */
    uint32_t (*i2c_write)(uint32_t bsp_dev_id,
                          uint8_t *write_buffer,
                          uint32_t write_length,
                          bsp_callback_t cb,
                          void *cb_arg);

    /**
     * Perform a Double-Buffered ("db") I2C Write
     *
     * This will first write the contents of \b write_buffer_0 to the I2C device, and then write the contents of
     * \b write_buffer_1.
     *
     * @param [in] bsp_dev_id       ID of the I2C device corresponding to the I2C peripheral to reset
     * @param [in] write_buffer_0   pointer to array of first batch of bytes to write
     * @param [in] write_length_0   total number of bytes in \b write_buffer_0
     * @param [in] write_buffer_1   pointer to array of second batch of bytes to write
     * @param [in] write_length_1   total number of bytes in \b write_buffer_1
     * @param [in] cb               pointer to callback function
     * @param [in] cb_arg           pointer to argument to use when calling callback
     *
     * @return
     * - BSP_STATUS_FAIL            if bsp_dev_id is invalid, if any portion of I2C transaction failed
     * - BSP_STATUS_OK              otherwise
     *
     */
    uint32_t (*i2c_db_write)(uint32_t bsp_dev_id,
                          uint8_t *write_buffer_0,
                          uint32_t write_length_0,
                          uint8_t *write_buffer_1,
                          uint32_t write_length_1,
                          bsp_callback_t cb,
                          void *cb_arg);

    /**
     * Perform a SPI read
     *
     * This function will write and then read back data from a SPI device with a register file. Padding
     * will automatically be added.
     *
     * Perform transaction in the order:
     * 1. SPI CS low
     * 2. SPI write of \b addr_length bytes from \b addr_buffer
     * 3. SPI write of pad_len padding clock cycles
     * 4. SPI read of \b data_length bytes into \b data_buffer
     * 5. SPI CS high
     *
     * BSP will decode \b bsp_dev_id to the correct SPI bus and SPI address.
     *
     * @param [in] bsp_dev_id       ID of the SPI device corresponding to the SPI peripheral to reset
     * @param [in] addr_buffer      pointer to array of bytes to write
     * @param [in] addr_length      total number of bytes in \b write_buffer
     * @param [in] data_buffer      pointer to array of bytes to load with SPI bytes read
     * @param [in] data_length      total number of bytes to read into \b read_buffer
     * @param [in] pad_len          total number of bytes of padding between the addr write transaction and the data read
     *
     * @return
     * - BSP_STATUS_FAIL            if bsp_dev_id is invalid, if any portion of SPI transaction failed
     * - BSP_STATUS_OK              otherwise
     *
     */
    uint32_t (*spi_read)(uint32_t bsp_dev_id,
                         uint8_t *addr_buffer,
                         uint32_t addr_length,
                         uint8_t *data_buffer,
                         uint32_t data_length,
                         uint32_t pad_len);

    /**
     * Perform a SPI write
     *
     * This function will write data to a SPI device with a register file. Padding will automatically
     * be added.
     *
     * Perform transaction in the order:
     * 1. SPI CS low
     * 2. SPI write of \b addr_length bytes from \b addr_buffer
     * 3. SPI write of pad_len padding clock cycles
     * 4. SPI write of \b data_length bytes into \b data_buffer
     * 5. SPI CS high
     *
     * BSP will decode \b bsp_dev_id to the correct SPI bus and SPI address.
     *
     * @param [in] bsp_dev_id       ID of the SPI device corresponding to the SPI peripheral to reset
     * @param [in] addr_buffer      pointer to array of bytes to write
     * @param [in] addr_length      total number of bytes in \b write_buffer
     * @param [in] data_buffer      pointer to array of bytes to load with SPI bytes read
     * @param [in] data_length      total number of bytes to read into \b read_buffer
     * @param [in] pad_len          total number of bytes of padding between the addr write transaction and the data write
     *
     * @return
     * - BSP_STATUS_FAIL            if bsp_dev_id is invalid, if any portion of SPI transaction failed
     * - BSP_STATUS_OK              otherwise
     *
     */
    uint32_t (*spi_write)(uint32_t bsp_dev_id,
                          uint8_t *addr_buffer,
                          uint32_t addr_length,
                          uint8_t *data_buffer,
                          uint32_t data_length,
                          uint32_t pad_len);

    /**
     * Global enable of interrupts
     *
     * Since this is MCU-platform specific, it is included as part of the BSP-Driver interface.
     *
     * @return
     * - BSP_STATUS_FAIL            if bsp_dev_id is invalid, if any portion of I2C transaction failed
     * - BSP_STATUS_OK              otherwise
     *
     */
    uint32_t (*enable_irq)(void);

    /**
     * Global disable of interrupts
     *
     * Since this is MCU-platform specific, it is included as part of the BSP-Driver interface.
     *
     * @return
     * - BSP_STATUS_FAIL            if bsp_dev_id is invalid, if any portion of I2C transaction failed
     * - BSP_STATUS_OK              otherwise
     *
     */
    uint32_t (*disable_irq)(void);

    /**
     * Temporarily change the clock speed of the SPI bus
     *
     * Since portions of a driver may have a maximum bus speed limitation, this API allows for temporarily specifying
     * the maximum bus speed.
     *
     * @return
     * - BSP_STATUS_FAIL            if a slower speed is requested by the current SPI speed is already slowest available
     * - BSP_STATUS_OK              otherwise
     *
     */
    uint32_t (*spi_throttle_speed)(uint32_t speed_hz);

    /**
     * Restore the clock speed of the SPI bus to the original configuration
     *
     * After a call to spi_throttle_speed(), this API allows for restoring the bus clock speed of the SPI bus to the
     * original configuration given during BSP initialization.
     *
     * @return                      BSP_STATUS_OK always
     *
     */
    uint32_t (*spi_restore_speed)(void);
} bsp_driver_if_t;

/***********************************************************************************************************************
 * GLOBAL VARIABLES
 **********************************************************************************************************************/

/**
 * Pointer to BSP-to-Driver API implementation
 */
extern bsp_driver_if_t *bsp_driver_if_g;

/***********************************************************************************************************************
 * API FUNCTIONS
 **********************************************************************************************************************/
#ifdef __cplusplus
}
#endif

#endif // BSP_DRIVER_IF_H
