#ifndef I2C_HID_H
#define I2C_HID_H

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

#include "driver/i2c_master.h"

extern i2c_master_bus_handle_t bus_handle;
extern i2c_master_dev_handle_t dev_handle;

void touchpad_init(void);
void read_touch_task(void *pvParameters);
esp_err_t touchpad_mode_set(bool is_ptp_mode);
void i2c_queue_task(void *arg);

#endif