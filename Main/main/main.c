#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const char *TAG = "MP28167_APP";

#define I2C_MASTER_SCL_IO           18
#define I2C_MASTER_SDA_IO           17
#define MP28167_ADDR                0x60    

#define REG_VREF_L                  0x00
#define REG_VREF_H                  0x01
#define REG_VREF_GO                 0x02
#define REG_IOUT_LIM                0x03

esp_err_t mp28167_set_vref_mv(i2c_master_dev_handle_t dev, uint16_t mv) {
    // 1. 公式转换: V_int = Vref / 0.8
    uint16_t v_int = (uint16_t)((mv / 0.8f) + 0.5f);
    if (v_int > 2047) v_int = 2047;

    uint8_t v_h = (uint8_t)((v_int >> 3) & 0xFF);
    uint8_t v_l = (uint8_t)(v_int & 0x07);

    uint8_t data_l[] = {REG_VREF_L, v_l};
    uint8_t data_h[] = {REG_VREF_H, v_h};
    uint8_t data_go[] = {REG_VREF_GO, 0x01};

    ESP_ERROR_CHECK(i2c_master_transmit(dev, data_l, 2, -1));
    ESP_ERROR_CHECK(i2c_master_transmit(dev, data_h, 2, -1));
    ESP_ERROR_CHECK(i2c_master_transmit(dev, data_go, 2, -1));

    ESP_LOGI(TAG, "VREF set to %d mV (Raw: 0x%03X)", mv, v_int);
    return ESP_OK;
}

float mp28167_get_vref_mv(i2c_master_dev_handle_t dev) {
    uint8_t addr_h = REG_VREF_H;
    uint8_t addr_l = REG_VREF_L;
    uint8_t val_h, val_l;

    i2c_master_transmit_receive(dev, &addr_h, 1, &val_h, 1, -1);
    i2c_master_transmit_receive(dev, &addr_l, 1, &val_l, 1, -1);

    uint16_t v_raw = ((uint16_t)val_h << 3) | (val_l & 0x07);
    return v_raw * 0.8f;
}

void app_main(void) {

    // gpio_set_direction(14, GPIO_MODE_OUTPUT_OD);
    // gpio_set_level(14, 0);

    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = 0,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MP28167_ADDR,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t dev_handle;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));

    mp28167_set_vref_mv(dev_handle, 400);
    vTaskDelay(pdMS_TO_TICKS(2000));

    // gpio_set_level(14, 1);

    while (1) {
        float current_vref = mp28167_get_vref_mv(dev_handle);
        ESP_LOGI(TAG, "Current VREF in chip: %.2f mV", current_vref);

        // mp28167_set_vref_mv(dev_handle, 200);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}