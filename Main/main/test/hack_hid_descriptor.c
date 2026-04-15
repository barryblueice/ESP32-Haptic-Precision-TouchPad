#include <stdio.h>
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "TP"

#define I2C_PORT        I2C_NUM_0
#define I2C_SDA         36
#define I2C_SCL         35
#define I2C_FREQ_HZ     400000

#define TP_I2C_ADDR     0x2c

#define TP_RESET_GPIO   33
#define TP_INT_GPIO     34

static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t dev_handle;


void i2c_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TP_I2C_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };

    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));
}

void scan_i2c(void)
{
    ESP_LOGI(TAG, "Scanning I2C...");

    for (uint8_t addr = 1; addr < 0x7F; addr++) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = 100000,
        };

        i2c_master_dev_handle_t tmp;
        if (i2c_master_bus_add_device(bus_handle, &dev_cfg, &tmp) == ESP_OK) {
            if (i2c_master_probe(bus_handle, addr, 50) == ESP_OK) {
                ESP_LOGI(TAG, "Found device at 0x%02X", addr);
            }
            i2c_master_bus_rm_device(tmp);
        }
    }
}

void tp_reset(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TP_RESET_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "Resetting touchpad...");
    gpio_set_level(TP_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TP_RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
}

esp_err_t tp_write(uint16_t reg, uint8_t *data, size_t len)
{
    uint8_t buf[2 + len];
    buf[0] = reg & 0xFF;
    buf[1] = reg >> 8;
    memcpy(&buf[2], data, len);

    return i2c_master_transmit(dev_handle, buf, sizeof(buf), 100);
}

esp_err_t tp_read(uint16_t reg, uint8_t *data, size_t len)
{
    uint8_t reg_buf[2] = {
        reg & 0xFF,
        reg >> 8
    };

    return i2c_master_transmit_receive(dev_handle,
                                       reg_buf, 2,
                                       data, len,
                                       100);
}

void probe_hid_descriptor(void)
{
    uint8_t buf[1024];

    if (tp_read(0x0021, buf, 1024) == ESP_OK) {
        ESP_LOGI(TAG, "Report Descriptor:");
        for (int i = 0; i < 1024; i++) {
            printf("%02X ", buf[i]);
        }
        printf("\n");
    } else {
        ESP_LOGE(TAG, "Failed to read report descriptor");
    }
}

void read_loop(void)
{
    uint8_t buf[64];

    while (1) {
        int level = gpio_get_level(TP_INT_GPIO);

        if (level == 0) {
            if (tp_read(0x0000, buf, sizeof(buf)) == ESP_OK) {
                ESP_LOGI(TAG, "DATA:");
                for (int i = 0; i < 16; i++) {
                    printf("%02X ", buf[i]);
                }
                printf("\n");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void vendor_init_sequence(void)
{
    ESP_LOGI(TAG, "Running vendor init (placeholder)");

    vTaskDelay(pdMS_TO_TICKS(50));
}


void app_main(void)
{
    i2c_init();
    scan_i2c();

    gpio_config_t int_conf = {
        .pin_bit_mask = (1ULL << TP_INT_GPIO),
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&int_conf);

    tp_reset();

    vendor_init_sequence();

    probe_hid_descriptor();

    read_loop();
}