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
    IP5X09_STATE_IDLE               = 0, // 000: Idle / No charging
    IP5X09_STATE_TRICKLE            = 1, // 001: Trickle charging (low current pre-charge)
    IP5X09_STATE_CONSTANT_CURRENT   = 2, // 010: Constant Current (CC) charging
    IP5X09_STATE_CONSTANT_VOLTAGE   = 3, // 011: Constant Voltage (CV) charging
    IP5X09_STATE_CV_STOP_DETECT     = 4, // 100: CV termination detection phase
    IP5X09_STATE_FULL               = 5, // 101: Charging complete / Battery full
    IP5X09_STATE_TIMEOUT            = 6  // 110: Charging timeout error
} ip5x09_chg_state_t;

typedef struct {
    uint8_t trickle_chg_timeout : 1; // Bit 0: Trickle charge timeout flag
    uint8_t chg_timeout         : 1; // Bit 1: Overall charging timeout flag
    uint8_t cv_timeout          : 1; // Bit 2: Constant Voltage (CV) timeout flag
    uint8_t chg_end_flag        : 1; // Bit 3: Charging end/complete flag
    uint8_t chgop_status        : 1; // Bit 4: Charging operation indicator
    uint8_t state               : 3; // Bit 7:5: Primary charging state
} ip5x09_reg_71_t;

typedef union {
    uint8_t val;
    ip5x09_reg_71_t reg;
} ip5x09_status_reg_t;

void sub_dev_init(void);

uint8_t ip5x09_read_reg(uint8_t reg_addr);
float ip5x09_get_battery_voltage();
int get_battery_percentage();

void charging_state_monitor_task(void *pvParameters);

esp_err_t mp28167_set_vref_mv(uint16_t mv);
float mp28167_get_vref_mv();

extern i2c_master_dev_handle_t sub_dev_mp28167_handle;
extern i2c_master_dev_handle_t sub_dev_ip5x09_handle;
extern i2c_master_bus_handle_t sub_bus_handle;
extern int battery_percentage;

#endif