#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "IP5209_TEST";

#define I2C_MASTER_SCL_IO           36
#define I2C_MASTER_SDA_IO           35
#define I2C_MASTER_NUM              0
#define IP5209_SLAVE_ADDR           0xEA

i2c_master_dev_handle_t dev_handle;

void ip5209_i2c_init() {
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_MASTER_NUM,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = IP5209_SLAVE_ADDR >> 1,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));
}

uint8_t ip5209_read_reg(uint8_t reg_addr) {
    uint8_t data = 0;
    ESP_ERROR_CHECK(i2c_master_transmit_receive(dev_handle, &reg_addr, 1, &data, 1, -1));
    return data;
}

float ip5209_get_battery_voltage() {
    uint8_t v_low = ip5209_read_reg(0xA2);
    uint8_t v_high = ip5209_read_reg(0xA3);

    uint32_t adc_val = (uint32_t)(v_high & 0x3F) << 8 | v_low;
    
    float voltage_mv = 2600.0f + (float)adc_val * 0.26855f;
    return voltage_mv / 1000.0f; // 转为 V
}

void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(500)); 
    
    ip5209_i2c_init();
    ESP_LOGI(TAG, "IP5209 I2C 驱动初始化完成");

    while (1) {
        uint8_t status = ip5209_read_reg(0x71);
        uint8_t chg_state = (status >> 5) & 0x07;
        
        float vbat = ip5209_get_battery_voltage();

        ESP_LOGI(TAG, "电池电压: %.3f V | 充电状态码: 0x%02X (状态: %d)", vbat, status, chg_state);
        
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}