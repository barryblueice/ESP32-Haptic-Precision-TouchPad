#ifndef I2C_HID_H
#define I2C_HID_H

#include "driver/i2c_master.h"
#include "SYS/hid_msg.h"
#include "esp_timer.h"

#define WATCHDOG_TIMEOUT_US (100*1000)

extern i2c_master_bus_handle_t bus_handle;
extern i2c_master_dev_handle_t dev_handle;
extern i2c_master_dev_handle_t dev_haptic_motor_handle;

extern bool global_watchdog_start;
extern esp_timer_handle_t timeout_watchdog_timer;
extern uint16_t global_scan_time;

extern uint16_t watchdog_x;
extern uint16_t watchdog_y;
extern uint16_t watchdog_id;
extern uint16_t watchdog_tip_switch;

void touchpad_init(void);
void read_touch_task(void *pvParameters);
esp_err_t touchpad_mode_set(bool is_ptp_mode);
void i2c_queue_task(void *arg);
void watchdog_refresh(bool touch_active);
void watchdog_timeout_callback(void* arg);

void parse_mouse_report(const mouse_msg_t *msg, mouse_hid_report_t *report);
void parse_ptp_report(const tp_multi_msg_t *msg, ptp_report_t *report);
void parse_ptp_simulated_mouse_report(const tp_multi_msg_t *msg, mouse_hid_report_t *out_report);
bool ptp_simulated_mouse_click_needs_release(void);

#endif
