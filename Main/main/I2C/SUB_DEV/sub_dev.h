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

typedef enum {
    BQ24195_STATE_NOT_CHARGING = 0, // 00: Not charging
    BQ24195_STATE_PRE_CHARGE   = 1, // 01: Pre-charge
    BQ24195_STATE_FAST_CHARGE  = 2, // 10: Fast charge
    BQ24195_STATE_CHARGE_DONE  = 3  // 11: Charge termination done
} bq24195_chg_state_t;

typedef struct {
    uint8_t vsys_stat  : 1; // Bit 0
    uint8_t therm_stat : 1; // Bit 1
    uint8_t pg_stat    : 1; // Bit 2
    uint8_t dpm_stat   : 1; // Bit 3
    uint8_t chrg_stat  : 2; // Bit 5:4
    uint8_t vbus_stat  : 2; // Bit 7:6
} bq24195_reg_08_t;

typedef union {
    uint8_t val;
    bq24195_reg_08_t reg;
} bq24195_status_reg_t;

#define MAX17048_REG_VCELL   0x02
#define MAX17048_REG_SOC     0x04
#define MAX17048_REG_VERSION 0x08

void sub_dev_init(void);

uint8_t bq24195_read_reg(uint8_t reg_addr);
esp_err_t bq24195_write_reg(uint8_t reg_addr, uint8_t value);
esp_err_t bq24195_enable_charging(void);
float bq24195_get_battery_voltage(void);
uint16_t max17048_read_reg16(uint8_t reg_addr);
float max17048_get_battery_voltage(void);
float max17048_get_battery_soc(void);
int get_battery_percentage();
void bq24195_dump_charge_config(void);

void charging_state_monitor_task(void *pvParameters);

esp_err_t mp28167_set_vref_mv(uint16_t mv);
float mp28167_get_vref_mv();

extern i2c_master_dev_handle_t sub_dev_mp28167_handle;
extern i2c_master_dev_handle_t sub_dev_bq24195_handle;
extern i2c_master_dev_handle_t sub_dev_max17048_handle;
extern i2c_master_bus_handle_t sub_bus_handle;
extern int battery_percentage;

#endif
