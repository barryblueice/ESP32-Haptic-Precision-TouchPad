#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "I2C/TP/i2c_hid.h"
#include "I2C/SUB_DEV/sub_dev.h"
#include "I2C/I2C_handle.h"

#include "GPIO/GPIO_handle.h"

#include "esp_log.h"

#define TAG "SUB_I2C_INIT"

int battery_percentage = 100;

i2c_master_bus_handle_t sub_bus_handle;
i2c_master_dev_handle_t sub_dev_mp28167_handle;
i2c_master_dev_handle_t sub_dev_ip5x09_handle;

void sub_dev_i2c_init(void) {
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = SUB_I2C_PORT,
        .sda_io_num = SUB_I2C_SDA,
        .scl_io_num = SUB_I2C_SCL,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &sub_bus_handle));

    i2c_device_config_t dev_mp28167_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MP28167_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(sub_bus_handle, &dev_mp28167_cfg, &sub_dev_mp28167_handle));

    i2c_device_config_t dev_ip5x09_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = IP5X09_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(sub_bus_handle, &dev_ip5x09_cfg, &sub_dev_ip5x09_handle));
}

void sub_dev_init(void) {

    sub_dev_i2c_init();

    battery_percentage = get_battery_percentage();

    ESP_LOGI(TAG, "battery level: %d %%", battery_percentage);

    float current_vref = mp28167_get_vref_mv();
    ESP_LOGI(TAG, "Current VREF in chip: %.2f mV", current_vref);

    xTaskCreatePinnedToCore(charging_state_monitor_task, "charging_state_monitor_task", 4096, NULL, 5, NULL, 1);

    uint8_t mp28167_vref = mp28167_get_vref_mv();
    if (mp28167_vref != 840.0f) {
        ESP_LOGW(TAG, "Over-Vref Detecting! setting Vref to 840mV");
        mp28167_set_vref_mv(840);
    }
}