#ifndef I2C_HID_H
#define I2C_HID_H

#include "driver/i2c_master.h"
#include "SYS/hid_msg.h"
#include "esp_timer.h"

#define WATCHDOG_TIMEOUT_US (100 * 1000)

#define CLICK_LIGHT_WEIGHT_DEFAULT  80
#define CLICK_MIDIUM_WEIGHT_DEFAULT 100
#define CLICK_STRONG_WEIGHT_DEFAULT 130
#define CLICK_REGION_SPLIT_X         1150

#define SENSITIVITY 2.0f

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
extern uint8_t click_light_weight_threshold;
extern uint8_t click_midium_weight_threshold;
extern uint8_t click_strong_weight_threshold;

void touchpad_init(void);
void read_touch_task(void *pvParameters);
esp_err_t touchpad_mode_set(bool is_ptp_mode);
void click_thresholds_load_from_nvs(void);
void i2c_queue_task(void *arg);
void watchdog_refresh(bool touch_active);
void watchdog_timeout_callback(void* arg);

void parse_mouse_report(const mouse_msg_t *msg, mouse_hid_report_t *report);
void parse_ptp_report(const tp_multi_msg_t *msg, ptp_report_t *report);
void parse_ptp_simulated_mouse_report(const tp_multi_msg_t *msg, mouse_hid_report_t *out_report);
bool ptp_simulated_mouse_click_needs_release(void);

int find_active_finger(const tp_multi_msg_t *msg, int start_index);
void reset_move_state(void);
void reset_scroll_state(void);
void handle_tap_release(const tp_multi_msg_t *msg, mouse_hid_report_t *out_report);
void get_active_centroid(const tp_multi_msg_t *msg, int active_count, float *x, float *y);
void update_tap_state(const tp_multi_msg_t *msg, int active_count, float centroid_x, float centroid_y);
void handle_single_finger_move(const tp_multi_msg_t *msg, int finger_index, mouse_hid_report_t *out_report);
void handle_dual_finger_scroll(const tp_multi_msg_t *msg, int first_index, int second_index, mouse_hid_report_t *out_report);

#endif
