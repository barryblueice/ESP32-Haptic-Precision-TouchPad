#include "I2C/i2c_hid.h"
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "SYS/rtos_queue.h"
#include "SYS/hid_msg.h"

#define TAG "I2C_QUEUE"
    
#define HISTORY_LEN 3

#define TAP_DEADZONE 30
#define FILTER_ALPHA 0.5f

#define SENSITIVITY 1.0f

#define SCAN_INTERVAL_PER_FINGER 80

typedef enum {
    TOUCH_NONE = 0,
    TOUCH_IDLE,
    TOUCH_TAP_CANDIDATE,
    TOUCH_DRAG
} touch_state_t;

static touch_state_t touch_state[5] = {0};

static bool tap_frozen[5] = {false};

uint16_t global_scan_time = 0;

static uint16_t raw_x_history[5][HISTORY_LEN] = {0};
static uint16_t raw_y_history[5][HISTORY_LEN] = {0};
static uint16_t last_raw_x[5] = {0};
static uint16_t last_raw_y[5] = {0};
static uint16_t origin_x[5] = {0};
static uint16_t origin_y[5] = {0};

static int64_t last_frame_time = 0;
static uint32_t simulated_scan_time = 0;

static uint16_t get_median(uint16_t n1, uint16_t n2, uint16_t n3) {
    if ((n1 > n2) ^ (n1 > n3)) return n1;
    else if ((n2 > n1) ^ (n2 > n3)) return n2;
    else return n3;
}

static int32_t slot_filter_x[5] = {0};
static int32_t slot_filter_y[5] = {0};
static uint16_t history_x[5] = {0};
static uint16_t history_y[5] = {0};

static uint8_t consecutive_errors[5] = {0};

static bool slot_active[5] = {false};

void update_simulated_scan_time(tp_multi_msg_t *msg) {
    int64_t now = esp_timer_get_time();

    if (last_frame_time != 0) {
        uint32_t delta = (uint32_t)((now - last_frame_time) / 100);
        
        simulated_scan_time += delta;
    }
    
    msg->scan_time = (uint16_t)(simulated_scan_time & 0xFFFF);
    
    last_frame_time = now;
}

void i2c_queue_task(void *arg) {

    uint8_t tp_packet[64];

    while (1) {

        tp_multi_msg_t tp_msg = {0}; 
        mouse_msg_t mouse_msg = {0};

        if (xQueueReceive(tp_data_queue, tp_packet, portMAX_DELAY) == pdPASS) {

            // printf("Raw Data: ");
            // for(int i=0; i<64; i++) printf("%02x ", tp_packet[i]);
            // printf("\n");

            if (current_tp_mode == PTP_MODE) {
                int active_finger_count = 0;

                update_simulated_scan_time(&tp_msg);

                for (int id = 0; id < 5; id++) {
                    int offset = 4 + (id * 8); 
                    uint8_t finger_status = tp_packet[offset];
                    
                    uint16_t rx = tp_packet[offset + 1] | (tp_packet[offset + 2] << 8);
                    uint16_t ry_raw = tp_packet[offset + 3] | (tp_packet[offset + 4] << 8);
                    uint16_t ry = 1533 - (ry_raw > 1533 ? 1533 : ry_raw);

                    tp_msg.fingers[id].contact_id = id;

                    if (finger_status & 0x01) {
                        for (int h = 0; h < HISTORY_LEN - 1; h++) {
                            raw_x_history[id][h] = raw_x_history[id][h+1];
                            raw_y_history[id][h] = raw_y_history[id][h+1];
                        }
                        raw_x_history[id][HISTORY_LEN-1] = rx;
                        raw_y_history[id][HISTORY_LEN-1] = ry;

                        if (!slot_active[id]) {
                            slot_filter_x[id] = rx << 8;
                            slot_filter_y[id] = ry << 8;
                            origin_x[id] = rx;
                            origin_y[id] = ry;
                            tap_frozen[id] = true; 
                            touch_state[id] = TOUCH_TAP_CANDIDATE;
                            slot_active[id] = true;
                            consecutive_errors[id] = 0;

                            for(int h=0; h<HISTORY_LEN; h++) {
                                raw_x_history[id][h] = rx; raw_y_history[id][h] = ry;
                            }
                        }

                        uint16_t mx = get_median(raw_x_history[id][HISTORY_LEN-3], 
                                                raw_x_history[id][HISTORY_LEN-2], 
                                                raw_x_history[id][HISTORY_LEN-1]);
                        uint16_t my = get_median(raw_y_history[id][HISTORY_LEN-3], 
                                                raw_y_history[id][HISTORY_LEN-2], 
                                                raw_y_history[id][HISTORY_LEN-1]);

                        int dx_jump = mx - (slot_filter_x[id] >> 8);
                        int dy_jump = my - (slot_filter_y[id] >> 8);
                        if ((dx_jump*dx_jump + dy_jump*dy_jump) > (300*300)) {
                            if (consecutive_errors[id] < 2) { 
                                mx = (uint16_t)(slot_filter_x[id] >> 8); 
                                my = (uint16_t)(slot_filter_y[id] >> 8);
                                consecutive_errors[id]++;
                            } else {
                                consecutive_errors[id] = 0;
                            }
                        } else {
                            consecutive_errors[id] = 0;
                        }

                        int alpha_speed = abs(rx - (int)last_raw_x[id]) + abs(ry - (int)last_raw_y[id]);
                        uint32_t dynamic_alpha = (alpha_speed < 3) ? 64 : (alpha_speed < 12 ? 115 : 218);
                        
                        slot_filter_x[id] = (dynamic_alpha * (mx << 8) + (256 - dynamic_alpha) * slot_filter_x[id]) >> 8;
                        slot_filter_y[id] = (dynamic_alpha * (my << 8) + (256 - dynamic_alpha) * slot_filter_y[id]) >> 8;

                        uint16_t fx = (uint16_t)(slot_filter_x[id] >> 8);
                        uint16_t fy = (uint16_t)(slot_filter_y[id] >> 8);

                        int dx_from_origin = abs((int)rx - (int)origin_x[id]);
                        int dy_from_origin = abs((int)ry - (int)origin_y[id]);

                        int active_dz = (active_finger_count > 0) ? (TAP_DEADZONE / 2) : TAP_DEADZONE;

                        if (dx_from_origin > active_dz || dy_from_origin > active_dz) {
                            tap_frozen[id] = false;
                            if (touch_state[id] == TOUCH_TAP_CANDIDATE) touch_state[id] = TOUCH_DRAG;
                        }

                        if (tap_frozen[id]) {
                            tp_msg.fingers[id].x = origin_x[id];
                            tp_msg.fingers[id].y = origin_y[id];
                        } else {
                            tp_msg.fingers[id].x = fx;
                            tp_msg.fingers[id].y = fy;
                        }

                        history_x[id] = tp_msg.fingers[id].x;
                        history_y[id] = tp_msg.fingers[id].y;
                        last_raw_x[id] = rx;
                        last_raw_y[id] = ry;
                        
                        tp_msg.fingers[id].tip_switch = 1;
                        tp_msg.fingers[id].confidence = 1;
                        active_finger_count++;

                    } else {
                        tp_msg.fingers[id].x = history_x[id];
                        tp_msg.fingers[id].y = history_y[id];
                        tp_msg.fingers[id].tip_switch = 0;
                        tp_msg.fingers[id].confidence = 0;

                        slot_active[id] = false;
                        last_raw_x[id] = 0; last_raw_y[id] = 0;
                        origin_x[id] = 0; origin_y[id] = 0;
                        slot_filter_x[id] = 0; slot_filter_y[id] = 0;
                        touch_state[id] = TOUCH_NONE;
                        tap_frozen[id] = false;
                        consecutive_errors[id] = 0;
                        for(int h=0; h<HISTORY_LEN; h++) {
                            raw_x_history[id][h] = 0; raw_y_history[id][h] = 0;
                        }
                    }
                }

                tp_msg.actual_count = (active_finger_count > 0) ? active_finger_count : 1;
                xQueueOverwrite(tp_queue, &tp_msg);
            } else {
                mouse_msg.x = (int8_t)tp_packet[4];
                mouse_msg.y = (int8_t)tp_packet[5];
                mouse_msg.buttons = tp_packet[3];

                xQueueOverwrite(mouse_queue, &mouse_msg);
            }
        }
    }
}

void parse_mouse_report(const mouse_msg_t *msg, mouse_hid_report_t *report) {
    int move_x = (int)(msg->x * SENSITIVITY);
    int move_y = (int)(msg->y * SENSITIVITY);

    if (move_x > 127)  move_x = 127;
    if (move_x < -127) move_x = -127;
    if (move_y > 127)  move_y = 127;
    if (move_y < -127) move_y = -127;

    report->x = (int8_t)move_x;
    report->y = (int8_t)move_y;
    report->buttons = msg->buttons & 0x07;
}

void parse_ptp_report(const tp_multi_msg_t *msg, ptp_report_t *report) {
    report->scan_time = msg->scan_time;
    report->contact_count = msg->actual_count;
    report->buttons = (msg->button_mask > 0) ? 0x01 : 0x00;

    for (int i = 0; i < 5; i++) {
        report->fingers[i].x = msg->fingers[i].x;
        report->fingers[i].y = msg->fingers[i].y;

        uint8_t contact_id = (uint8_t)i; 
        uint8_t status_byte = 0x01;

        if (msg->fingers[i].tip_switch) {
            status_byte |= 0x02;
        }

        report->fingers[i].tip_conf_id = (contact_id << 2) | status_byte;
    }
}