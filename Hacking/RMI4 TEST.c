#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "RMI4_TEST";

#define I2C_MASTER_SCL_IO           35
#define I2C_MASTER_SDA_IO           36
#define SYNA_TOUCH_ADDR             0x78

void app_main(void)
{
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SYNA_TOUCH_ADDR,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t dev_handle;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle));

    ESP_LOGI(TAG, "I2C Master Bus and Device (0x%02X) initialized.", SYNA_TOUCH_ADDR);

    uint8_t page_cmd[] = {0xFF, 0x00};
    esp_err_t ret = i2c_master_transmit(dev_handle, page_cmd, sizeof(page_cmd), -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write Page Select! Is the address 0x%02X correct?", SYNA_TOUCH_ADDR);
        return;
    }
    ESP_LOGI(TAG, "Page 0 selected.");

    uint8_t reg_addr = 0xE9;
    uint8_t pdt_buffer[6] = {0};
    ret = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, pdt_buffer, 6, -1);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "PDT Reading Success!");
        ESP_LOGI(TAG, "Data at 0xE9-0xEE: [%02X %02X %02X %02X %02X %02X]",
                 pdt_buffer[0], pdt_buffer[1], pdt_buffer[2], 
                 pdt_buffer[3], pdt_buffer[4], pdt_buffer[5]);
        
        if (pdt_buffer[5] == 0x01) {
            ESP_LOGI(TAG, "Confirmed: This is a Synaptics RMI4 device (Found F01).");
        } else {
            ESP_LOGW(TAG, "Device responded but Function ID (0x%02X) is unexpected.", pdt_buffer[5]);
        }
    } else {
        ESP_LOGE(TAG, "No response from PDT registers.");
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}