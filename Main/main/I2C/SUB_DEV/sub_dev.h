#ifndef SUB_DEV_H
#define SUB_DEV_H

#include "driver/i2c_master.h"

#define MP28167_REG_VREF_L                  0x00
#define MP28167_REG_VREF_H                  0x01
#define MP28167_REG_VREF_GO                 0x02
#define MP28167_REG_IOUT_LIM                0x03

typedef struct {
    float voltage;
    int percentage;
} v_cap_map_t;

void sub_dev_init(void);

uint8_t ip5209_read_reg(uint8_t reg_addr);
float ip5209_get_battery_voltage();
int get_battery_percentage();

esp_err_t mp28167_set_vref_mv(i2c_master_dev_handle_t dev, uint16_t mv);
float mp28167_get_vref_mv(i2c_master_dev_handle_t dev);

extern i2c_master_dev_handle_t sub_dev_mp28167_handle;
extern i2c_master_dev_handle_t sub_dev_ip5x09_handle;
extern i2c_master_bus_handle_t sub_bus_handle;

#endif