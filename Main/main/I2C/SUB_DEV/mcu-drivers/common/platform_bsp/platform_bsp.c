#include "platform_bsp.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "GPIO/GPIO_handle.h"
#include "I2C/I2C_handle.h"
#include "I2C/SUB_DEV/sub_dev.h"
#include "I2C/TP/i2c_hid.h"

#define BSP_I2C_TIMEOUT_MS (100)

static const char *TAG = "platform_bsp";

static bsp_app_callback_t app_cb = NULL;
static void *app_cb_arg = NULL;

static bsp_callback_t bsp_dut_int_cb = NULL;
static void *bsp_dut_int_cb_arg = NULL;
static bool bsp_dut_int_isr_registered = false;

static i2c_master_dev_handle_t bsp_dev_i2c2_handle = NULL;
static i2c_master_dev_handle_t bsp_dev_broadcast_handle = NULL;

bool trigger_audio_change = false;
bool bsp_write_process_done = false;
bool bsp_read_process_done = false;

FILE *test_file = NULL;
FILE *coverage_file = NULL;
FILE *bridge_write_file = NULL;
FILE *bridge_read_file = NULL;

// static esp_err_t bsp_ensure_isr_service(void)
// {
//     esp_err_t ret = gpio_install_isr_service(0);

//     if ((ret == ESP_OK) || (ret == ESP_ERR_INVALID_STATE))
//     {
//         return ESP_OK;
//     }

//     return ret;
// }

static void IRAM_ATTR bsp_dut_alert_isr(void *arg)
{
    (void)arg;

    if (bsp_dut_int_cb != NULL)
    {
        bsp_dut_int_cb(BSP_STATUS_OK, bsp_dut_int_cb_arg);
    }

    if (app_cb != NULL)
    {
        app_cb(BSP_STATUS_DUT_EVENTS, app_cb_arg);
    }
}

static esp_err_t bsp_add_i2c_device(i2c_master_bus_handle_t bus,
                                    uint16_t address_7bit,
                                    i2c_master_dev_handle_t *handle)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address_7bit,
        .scl_speed_hz = I2C_FREQ_HZ,
    };

    if ((bus == NULL) || (handle == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (*handle != NULL)
    {
        return ESP_OK;
    }

    return i2c_master_bus_add_device(bus, &dev_cfg, handle);
}

static i2c_master_dev_handle_t bsp_get_i2c_handle(uint32_t bsp_dev_id)
{
    esp_err_t ret;

    switch (bsp_dev_id)
    {
        case BSP_DUT_DEV_ID:
            return dev_haptic_motor_handle;

        case BSP_DUT_DEV_ID_SPI2:
            // Keep the legacy ID as an alias, but always route the DUT over I2C.
            return dev_haptic_motor_handle;

        case BSP_DUT_DEV_ID_I2C2:
            ret = bsp_add_i2c_device(bus_handle,
                                     BSP_DUT_I2C2_ADDRESS_8BIT >> 1,
                                     &bsp_dev_i2c2_handle);
            return (ret == ESP_OK) ? bsp_dev_i2c2_handle : NULL;

        case BSP_DUT_DEV_ID_BROADCAST:
            ret = bsp_add_i2c_device(bus_handle,
                                     BSP_DUT_I2C_BROADCAST_ADDRESS_8BIT >> 1,
                                     &bsp_dev_broadcast_handle);
            return (ret == ESP_OK) ? bsp_dev_broadcast_handle : NULL;

        default:
            return NULL;
    }
}

static bool bsp_get_gpio_num(uint32_t gpio_id, gpio_num_t *gpio_num)
{
    if (gpio_num == NULL)
    {
        return false;
    }

    switch (gpio_id)
    {
        case BSP_GPIO_ID_DUT_CDC_RESET:
            *gpio_num = TP_RESET_GPIO;
            return true;

        case BSP_GPIO_ID_DUT_CDC_INT:
            *gpio_num = GPIO_HAPTIC_ALERT_N;
            return true;

        case BSP_GPIO_ID_INTP_LED1:
            *gpio_num = GPIO_LED_1;
            return true;

        case BSP_GPIO_ID_INTP_LED2:
            *gpio_num = GPIO_LED_2;
            return true;

        case BSP_GPIO_ID_INTP_LED3:
            *gpio_num = GPIO_LED_3;
            return true;

        case BSP_GPIO_ID_GF_GPIO2:
            *gpio_num = GPIO_HAPTIC_FUNC_FOR_TP_EN;
            return true;

        case BSP_GPIO_ID_GF_GPIO7:
            *gpio_num = GPIO_HAPTIC_BUCK_BOOST_EN;
            return true;

        default:
            return false;
    }
}

static void bsp_prepare_output(uint32_t gpio_id, gpio_num_t gpio_num)
{
    gpio_mode_t mode = GPIO_MODE_OUTPUT;

    if ((gpio_id == BSP_GPIO_ID_INTP_LED1) ||
        (gpio_id == BSP_GPIO_ID_INTP_LED2) ||
        (gpio_id == BSP_GPIO_ID_INTP_LED3) ||
        (gpio_id == BSP_GPIO_ID_GF_GPIO7))
    {
        mode = GPIO_MODE_OUTPUT_OD;
    }

    gpio_set_direction(gpio_num, mode);
}

uint32_t bsp_initialize(bsp_app_callback_t cb, void *cb_arg)
{
    gpio_config_t reset_conf = {
        .pin_bit_mask = (1ULL << TP_RESET_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config_t alert_conf = {
        .pin_bit_mask = (1ULL << GPIO_HAPTIC_ALERT_N),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    app_cb = cb;
    app_cb_arg = cb_arg;

    bsp_dut_int_cb = NULL;
    bsp_dut_int_cb_arg = NULL;
    bsp_dut_int_isr_registered = false;
    bsp_write_process_done = false;
    bsp_read_process_done = false;

    gpio_config(&reset_conf);
    gpio_set_level(TP_RESET_GPIO, 1);
    gpio_config(&alert_conf);

    return BSP_STATUS_OK;
}

uint32_t bsp_audio_set_fs(uint32_t fs_hz)
{
    (void)fs_hz;
    return BSP_STATUS_FAIL;
}

uint32_t bsp_audio_play(bsp_i2s_port_t port, uint8_t content)
{
    (void)port;
    (void)content;
    return BSP_STATUS_FAIL;
}

uint32_t bsp_audio_play_record(bsp_i2s_port_t port, uint8_t content)
{
    (void)port;
    (void)content;
    return BSP_STATUS_FAIL;
}

uint32_t bsp_audio_play_stream(bsp_i2s_port_t port,
                               uint8_t *content,
                               uint32_t length,
                               bsp_callback_t half_cb,
                               void *half_cb_arg,
                               bsp_callback_t cplt_cb,
                               void *cplt_cb_arg)
{
    (void)port;
    (void)content;
    (void)length;
    (void)half_cb;
    (void)half_cb_arg;
    (void)cplt_cb;
    (void)cplt_cb_arg;
    return BSP_STATUS_FAIL;
}

uint32_t bsp_audio_continue_play_stream(bsp_i2s_port_t port, uint8_t *content, uint32_t length)
{
    (void)port;
    (void)content;
    (void)length;
    return BSP_STATUS_FAIL;
}

uint32_t bsp_audio_pause(bsp_i2s_port_t port)
{
    (void)port;
    return BSP_STATUS_FAIL;
}

uint32_t bsp_audio_resume(bsp_i2s_port_t port)
{
    (void)port;
    return BSP_STATUS_FAIL;
}

uint32_t bsp_audio_stop(bsp_i2s_port_t port)
{
    (void)port;
    return BSP_STATUS_FAIL;
}

uint32_t bsp_set_timer(uint32_t duration_ms, bsp_callback_t cb, void *cb_arg)
{
    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    if (cb != NULL)
    {
        cb(BSP_STATUS_OK, cb_arg);
    }

    return BSP_STATUS_OK;
}

uint32_t bsp_set_gpio(uint32_t gpio_id, uint8_t gpio_state)
{
    gpio_num_t gpio_num;

    if (!bsp_get_gpio_num(gpio_id, &gpio_num))
    {
        return BSP_STATUS_FAIL;
    }

    if (gpio_id == BSP_GPIO_ID_DUT_CDC_INT)
    {
        return BSP_STATUS_FAIL;
    }

    bsp_prepare_output(gpio_id, gpio_num);
    gpio_set_level(gpio_num, gpio_state ? 1 : 0);

    return BSP_STATUS_OK;
}

bool bsp_was_pb_pressed(uint8_t pb_id)
{
    (void)pb_id;
    return (gpio_get_level(BOOT_BUTTON_GPIO) == 0);
}

void bsp_sleep(void)
{
    vTaskDelay(pdMS_TO_TICKS(1));
}

uint32_t bsp_register_pb_cb(uint32_t pb_id, bsp_app_callback_t cb, void *cb_arg)
{
    (void)pb_id;
    (void)cb;
    (void)cb_arg;
    return BSP_STATUS_FAIL;
}

void bsp_notification_callback(uint32_t event_flags, void *arg)
{
    (void)event_flags;
    (void)arg;
}

uint32_t bsp_register_gpio_cb(uint32_t gpio_id, bsp_callback_t cb, void *cb_arg)
{
    gpio_config_t int_conf = {
        .pin_bit_mask = (1ULL << GPIO_HAPTIC_ALERT_N),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };

    if ((gpio_id != BSP_GPIO_ID_DUT_CDC_INT) || (cb == NULL))
    {
        return BSP_STATUS_FAIL;
    }

    // if (bsp_ensure_isr_service() != ESP_OK)
    // {
    //     ESP_LOGE(TAG, "failed to install GPIO ISR service");
    //     return BSP_STATUS_FAIL;
    // }

    gpio_config(&int_conf);

    if (bsp_dut_int_isr_registered)
    {
        gpio_isr_handler_remove(GPIO_HAPTIC_ALERT_N);
    }

    bsp_dut_int_cb = cb;
    bsp_dut_int_cb_arg = cb_arg;

    if (gpio_isr_handler_add(GPIO_HAPTIC_ALERT_N, bsp_dut_alert_isr, NULL) != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to register DUT IRQ handler");
        bsp_dut_int_cb = NULL;
        bsp_dut_int_cb_arg = NULL;
        return BSP_STATUS_FAIL;
    }

    bsp_dut_int_isr_registered = true;

    return BSP_STATUS_OK;
}

uint32_t bsp_i2c_read_repeated_start(uint32_t bsp_dev_id,
                                     uint8_t *write_buffer,
                                     uint32_t write_length,
                                     uint8_t *read_buffer,
                                     uint32_t read_length,
                                     bsp_callback_t cb,
                                     void *cb_arg)
{
    i2c_master_dev_handle_t handle = bsp_get_i2c_handle(bsp_dev_id);
    esp_err_t ret;

    if ((handle == NULL) || (write_buffer == NULL) || (read_buffer == NULL))
    {
        ESP_LOGE(TAG, "invalid I2C read request, dev_id=%" PRIu32, bsp_dev_id);
        return BSP_STATUS_FAIL;
    }

    ret = i2c_master_transmit_receive(handle,
                                      write_buffer,
                                      write_length,
                                      read_buffer,
                                      read_length,
                                      BSP_I2C_TIMEOUT_MS);

    bsp_read_process_done = (ret == ESP_OK);

    if (cb != NULL)
    {
        cb((ret == ESP_OK) ? BSP_STATUS_OK : BSP_STATUS_FAIL, cb_arg);
    }

    return (ret == ESP_OK) ? BSP_STATUS_OK : BSP_STATUS_FAIL;
}

uint32_t bsp_i2c_write(uint32_t bsp_dev_id,
                       uint8_t *write_buffer,
                       uint32_t write_length,
                       bsp_callback_t cb,
                       void *cb_arg)
{
    i2c_master_dev_handle_t handle = bsp_get_i2c_handle(bsp_dev_id);
    esp_err_t ret;

    if ((handle == NULL) || (write_buffer == NULL))
    {
        ESP_LOGE(TAG, "invalid I2C write request, dev_id=%" PRIu32, bsp_dev_id);
        return BSP_STATUS_FAIL;
    }

    ret = i2c_master_transmit(handle, write_buffer, write_length, BSP_I2C_TIMEOUT_MS);

    bsp_write_process_done = (ret == ESP_OK);

    if (cb != NULL)
    {
        cb((ret == ESP_OK) ? BSP_STATUS_OK : BSP_STATUS_FAIL, cb_arg);
    }

    return (ret == ESP_OK) ? BSP_STATUS_OK : BSP_STATUS_FAIL;
}

uint32_t bsp_i2c_db_write(uint32_t bsp_dev_id,
                          uint8_t *write_buffer_0,
                          uint32_t write_length_0,
                          uint8_t *write_buffer_1,
                          uint32_t write_length_1,
                          bsp_callback_t cb,
                          void *cb_arg)
{
    uint8_t *buffer;
    uint32_t total_length = write_length_0 + write_length_1;
    uint32_t ret;

    if ((write_buffer_0 == NULL) || (write_buffer_1 == NULL) || (total_length == 0))
    {
        return BSP_STATUS_FAIL;
    }

    buffer = (uint8_t *)malloc(total_length);
    if (buffer == NULL)
    {
        return BSP_STATUS_FAIL;
    }

    memcpy(buffer, write_buffer_0, write_length_0);
    memcpy(buffer + write_length_0, write_buffer_1, write_length_1);

    ret = bsp_i2c_write(bsp_dev_id, buffer, total_length, cb, cb_arg);
    free(buffer);

    return ret;
}

uint32_t bsp_i2c_reset(uint32_t bsp_dev_id, bool *was_i2c_busy)
{
    (void)bsp_dev_id;

    if (was_i2c_busy != NULL)
    {
        *was_i2c_busy = false;
    }

    return BSP_STATUS_OK;
}

uint32_t bsp_enable_irq(void)
{
    return BSP_STATUS_OK;
}

uint32_t bsp_toggle_gpio(uint32_t gpio_id)
{
    gpio_num_t gpio_num;

    if (!bsp_get_gpio_num(gpio_id, &gpio_num))
    {
        return BSP_STATUS_FAIL;
    }

    if (gpio_id == BSP_GPIO_ID_DUT_CDC_INT)
    {
        return BSP_STATUS_FAIL;
    }

    bsp_prepare_output(gpio_id, gpio_num);
    gpio_set_level(gpio_num, !gpio_get_level(gpio_num));

    return BSP_STATUS_OK;
}

uint32_t bsp_eeprom_control(uint8_t command)
{
    (void)command;
    return BSP_STATUS_FAIL;
}

uint32_t bsp_eeprom_read_status(uint8_t *buffer)
{
    (void)buffer;
    return BSP_STATUS_FAIL;
}

uint32_t bsp_eeprom_read_jedecid(uint8_t *buffer)
{
    (void)buffer;
    return BSP_STATUS_FAIL;
}

uint32_t bsp_eeprom_read(uint32_t addr, uint8_t *data_buffer, uint32_t data_length)
{
    (void)addr;
    (void)data_buffer;
    (void)data_length;
    return BSP_STATUS_FAIL;
}

uint32_t bsp_eeprom_program(uint32_t addr, uint8_t *data_buffer, uint32_t data_length)
{
    (void)addr;
    (void)data_buffer;
    (void)data_length;
    return BSP_STATUS_FAIL;
}

uint32_t bsp_eeprom_program_verify(uint32_t addr, uint8_t *data_buffer, uint32_t data_length)
{
    (void)addr;
    (void)data_buffer;
    (void)data_length;
    return BSP_STATUS_FAIL;
}

uint32_t bsp_eeprom_erase(uint8_t command, uint32_t addr)
{
    (void)command;
    (void)addr;
    return BSP_STATUS_FAIL;
}

uint32_t bsp_set_ld2(uint8_t mode, uint32_t blink_100ms)
{
    (void)blink_100ms;

    switch (mode)
    {
        case BSP_LD2_MODE_OFF:
            bsp_prepare_output(BSP_GPIO_ID_INTP_LED1, GPIO_LED_1);
            gpio_set_level(GPIO_LED_1, LED_OFF);
            return BSP_STATUS_OK;

        case BSP_LD2_MODE_ON:
            bsp_prepare_output(BSP_GPIO_ID_INTP_LED1, GPIO_LED_1);
            gpio_set_level(GPIO_LED_1, LED_ON);
            return BSP_STATUS_OK;

        default:
            return BSP_STATUS_FAIL;
    }
}

uint32_t bsp_set_led(uint32_t index, uint8_t mode, uint32_t blink_100ms)
{
    gpio_num_t led_gpio;

    (void)blink_100ms;

    switch (index)
    {
        case 0:
            led_gpio = GPIO_LED_1;
            break;

        case 1:
            led_gpio = GPIO_LED_2;
            break;

        case 2:
            led_gpio = GPIO_LED_3;
            break;

        default:
            return BSP_STATUS_FAIL;
    }

    if (mode == BSP_LD2_MODE_OFF)
    {
        bsp_prepare_output(index + BSP_GPIO_ID_INTP_LED1, led_gpio);
        gpio_set_level(led_gpio, LED_OFF);
        return BSP_STATUS_OK;
    }

    if (mode == BSP_LD2_MODE_ON)
    {
        bsp_prepare_output(index + BSP_GPIO_ID_INTP_LED1, led_gpio);
        gpio_set_level(led_gpio, LED_ON);
        return BSP_STATUS_OK;
    }

    return BSP_STATUS_FAIL;
}

void bsp_get_switch_state_changes(uint8_t *state, uint8_t *change_mask)
{
    if (state != NULL)
    {
        *state = 0;
    }

    if (change_mask != NULL)
    {
        *change_mask = 0;
    }
}

static bsp_driver_if_t esp32_bsp_if = {
    .set_gpio = &bsp_set_gpio,
    .register_gpio_cb = &bsp_register_gpio_cb,
    .set_timer = &bsp_set_timer,
    .i2c_reset = &bsp_i2c_reset,
    .i2c_read_repeated_start = &bsp_i2c_read_repeated_start,
    .i2c_write = &bsp_i2c_write,
    .i2c_db_write = &bsp_i2c_db_write,
    .spi_write = NULL,
    .enable_irq = &bsp_enable_irq,
};

bsp_driver_if_t *bsp_driver_if_g = &esp32_bsp_if;
