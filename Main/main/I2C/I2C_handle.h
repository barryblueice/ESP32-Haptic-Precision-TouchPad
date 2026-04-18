#ifndef I2C_HANDLE_H
#define I2C_HANDLE_H

#define TP_I2C_PORT         I2C_NUM_0
#define SUB_I2C_PORT        I2C_NUM_1

#define TP_I2C_SDA          36
#define TP_I2C_SCL          35

#define SUB_I2C_SDA         17
#define SUB_I2C_SCL         18

#define I2C_FREQ_HZ         400000

#define TP_I2C_ADDR         0x2C
#define HAPTIC_MOTOR_ADDR   0x43
#define MP28167_ADDR        0x60
#define IP5X09_ADDR         0xEA >> 1

#define TP_RESET_GPIO       33
#define TP_INT_GPIO         34

#define HID_DESC_REG        0x0001
#define HID_REPORT_REG      0x0021
#define HID_COMMAND_REG     0x0022
#define HID_INPUT_REG       0x0000
#define CMD_RESET           0x01
#define CMD_SET_POWER       0x08
#define POWER_ON            0x00

#endif