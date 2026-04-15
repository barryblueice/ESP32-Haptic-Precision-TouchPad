#include <stdio.h>
#include <string.h>
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "SurfaceTouch"

#define I2C_PORT         I2C_NUM_0
#define I2C_SDA          36
#define I2C_SCL          35
#define I2C_FREQ_HZ      400000
#define TP_I2C_ADDR      0x2c

#define TP_RESET_GPIO    33
#define TP_INT_GPIO      34

#define HID_DESC_REG     0x0001
#define HID_REPORT_REG   0x0021
#define HID_COMMAND_REG  0x0022
#define HID_INPUT_REG    0x0000

#define CMD_RESET        0x01
#define CMD_SET_POWER    0x08
#define POWER_ON         0x00

static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t dev_handle;

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

void i2c_init(void) {
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

void tp_hw_reset(void) {
    gpio_set_direction(TP_RESET_GPIO, GPIO_MODE_OUTPUT);
    ESP_LOGI(TAG, "Hardware Reset...");
    gpio_set_level(TP_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(TP_RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
}

void hid_init_sequence(void) {
    ESP_LOGI(TAG, "Sending HID Power ON...");
    uint8_t pwr_data[] = { POWER_ON, CMD_SET_POWER };
    tp_write(HID_COMMAND_REG, pwr_data, 2);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "Sending HID Reset Command...");
    uint8_t rst_data[] = { 0x00, CMD_RESET };
    tp_write(HID_COMMAND_REG, rst_data, 2);
    vTaskDelay(pdMS_TO_TICKS(100));
}

void parse_touch_report(uint8_t *buf, size_t len) {
    uint8_t report_id = buf[2];

        if (report_id == 0x03) {
        bool tip_switch = buf[3] & 0x01;
        uint8_t contact_id = buf[4];
        uint16_t x = buf[9] | (buf[10] << 8);
        uint16_t y = buf[15] | (buf[16] << 8); 

        if (tip_switch) {
            printf("ID:%d | X:%4d | Y:%4d\n", contact_id, x, y);
        }
    }
}

void read_touch_task(void *pvParameters)
{
    uint8_t packet[64];
    ESP_LOGI(TAG, "Touch task started on Core 1");

    while (1) {
        if (gpio_get_level(TP_INT_GPIO) == 0) {

            esp_err_t ret = i2c_master_receive(dev_handle, packet, 64, 100);
            
            if (ret == ESP_OK) {
                if (ret == ESP_OK) {
                    printf("Raw Data: ");
                    for(int i=0; i<64; i++) printf("%02x ", packet[i]);
                    printf("\n");
                }
            }

        }
        
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

void app_main(void) {
    i2c_init();

    gpio_config_t int_conf = {
        .pin_bit_mask = (1ULL << TP_INT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE 
    };
    gpio_config(&int_conf);

    tp_hw_reset();

    hid_init_sequence();

    uint8_t magic_cmd[] = {
        0x00, 0x3f, // Register Address
        0x03,       // Report Type (Feature)
        0x0f,       // Report ID
        0x23, 0x00, // Length/Control
        0x04, 0x00, // Reserved/Padding
        0x0f,       // Target Report ID
        0x01        // 0x00: Standard, 0x01: Full RMI/PTP
    };

    uint8_t final_buf[11];
    final_buf[0] = 0x22;
    memcpy(&final_buf[1], magic_cmd, sizeof(magic_cmd));

    i2c_master_transmit(dev_handle, final_buf, sizeof(final_buf), -1);


    xTaskCreatePinnedToCore(
        read_touch_task,
        "touch_task",
        4096,
        NULL,
        10,
        NULL,
        1
    );

    ESP_LOGI(TAG, "Main task finishing, touch handler running in background...");
}