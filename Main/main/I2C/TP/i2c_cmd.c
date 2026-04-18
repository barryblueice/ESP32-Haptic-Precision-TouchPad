#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "I2C/TP/i2c_hid.h"

#include "I2C/I2C_handle.h"

#define TAG "I2C_CMD"

i2c_master_bus_handle_t bus_handle;
i2c_master_dev_handle_t dev_handle;

bool global_watchdog_start = false;

esp_err_t tp_read(uint16_t reg, uint8_t *data, size_t len) {
    uint8_t reg_buf[2] = { reg & 0xFF, reg >> 8 };
    return i2c_master_transmit_receive(dev_handle, reg_buf, 2, data, len, 100);
}

esp_err_t tp_write(uint16_t reg, uint8_t *data, size_t len) {
    uint8_t buf[2 + len];
    buf[0] = reg & 0xFF;
    buf[1] = reg >> 8;
    memcpy(&buf[2], data, len);
    return i2c_master_transmit(dev_handle, buf, sizeof(buf), 100);
}

void hid_init_sequence(void) {
    // ESP_LOGI(TAG, "Sending HID Power ON...");
    uint8_t pwr_data[] = { POWER_ON, CMD_SET_POWER };
    tp_write(HID_COMMAND_REG, pwr_data, 2);
    vTaskDelay(pdMS_TO_TICKS(50));

    // ESP_LOGI(TAG, "Sending HID Reset Command...");
    uint8_t rst_data[] = { 0x00, CMD_RESET };
    tp_write(HID_COMMAND_REG, rst_data, 2);
    vTaskDelay(pdMS_TO_TICKS(100));
}

void tp_i2c_init(void) {
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = TP_I2C_PORT,
        .sda_io_num = TP_I2C_SDA,
        .scl_io_num = TP_I2C_SCL,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TP_I2C_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));
}

void tp_hw_reset(void) {
    gpio_set_direction(TP_RESET_GPIO, GPIO_MODE_OUTPUT);
    ESP_LOGI(TAG, "Hardware Reset...");
    gpio_set_level(TP_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(TP_RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
}

esp_err_t touchpad_mode_set(bool is_ptp_mode) {

    uint8_t magic_cmd[] = {
        0x00, 0x3f,                     // Register Address
        0x03,                           // Report Type (Feature)
        0x0f,                           // Report ID
        0x23, 0x00,                     // Length/Control
        0x04, 0x00,                     // Reserved/Padding
        0x0f,                           // Target Report ID
        is_ptp_mode ? 0x01 : 0x00       // 0x00: Standard, 0x01: Full RMI/PTP
    };

    uint8_t final_buf[11];
    final_buf[0] = 0x22;
    memcpy(&final_buf[1], magic_cmd, sizeof(magic_cmd));

    return i2c_master_transmit(dev_handle, final_buf, sizeof(final_buf), -1);
}

void touchpad_init(void) {
    tp_i2c_init();

    gpio_config_t int_conf = {
        .pin_bit_mask = (1ULL << TP_INT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE 
    };

    gpio_config(&int_conf);
    tp_hw_reset();
    hid_init_sequence();

}