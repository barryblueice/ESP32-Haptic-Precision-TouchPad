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

#define TAG "I2C_QUENE"
    
#define HISTORY_LEN 3

#define TAP_DEADZONE 30
#define FILTER_ALPHA 0.5f

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
static uint32_t filtered_x[5] = {0};
static uint32_t filtered_y[5] = {0};
static uint16_t last_raw_x[5] = {0};
static uint16_t last_raw_y[5] = {0};
static uint16_t origin_x[5] = {0};
static uint16_t origin_y[5] = {0};

static uint16_t history_actual_count = 0;

static int64_t last_frame_time = 0;
static uint32_t simulated_scan_time = 0;

static uint16_t get_median(uint16_t n1, uint16_t n2, uint16_t n3) {
    if ((n1 > n2) ^ (n1 > n3)) return n1;
    else if ((n2 > n1) ^ (n2 > n3)) return n2;
    else return n3;
}

static int32_t slot_filter_x[5] = {0};
static int32_t slot_filter_y[5] = {0};
static uint16_t slot_origin_x[5] = {0};
static uint16_t slot_origin_y[5] = {0};
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
            
            if (current_tp_mode == PTP_MODE) {

                tp_multi_msg_t tp_msg = {0};
                    int active_finger_count = 0;

                    update_simulated_scan_time(&tp_msg);

                    for (int id = 0; id < 5; id++) {
                        int offset = 4 + (id * 8); 
                        uint8_t finger_status = tp_packet[offset];
                        
                        uint16_t rx = tp_packet[offset + 1] | (tp_packet[offset + 2] << 8);
                        uint16_t ry_raw = tp_packet[offset + 3] | (tp_packet[offset + 4] << 8);
                        uint16_t ry = 1533 - (ry_raw > 1533 ? 1533 : ry_raw);

                        if (finger_status & 0x01) { 
                            if (!slot_active[id]) {
                                slot_filter_x[id] = rx << 8;
                                slot_filter_y[id] = ry << 8;
                                slot_origin_x[id] = rx;
                                slot_origin_y[id] = ry;
                                slot_active[id] = true;
                                tp_msg.fingers[id].confidence = 1; 
                            } else {
                                int dx = abs((int)rx - (int)(slot_filter_x[id] >> 8));
                                int dy = abs((int)ry - (int)(slot_filter_y[id] >> 8));
                                
                                uint32_t alpha = (dx + dy < 3) ? 64 : (dx + dy < 12 ? 115 : 218);
                                
                                slot_filter_x[id] = (alpha * (rx << 8) + (256 - alpha) * slot_filter_x[id]) >> 8;
                                slot_filter_y[id] = (alpha * (ry << 8) + (256 - alpha) * slot_filter_y[id]) >> 8;
                            }

                            tp_msg.fingers[id].x = (uint16_t)(slot_filter_x[id] >> 8);
                            tp_msg.fingers[id].y = (uint16_t)(slot_filter_y[id] >> 8);
                            tp_msg.fingers[id].tip_switch = 1;
                            tp_msg.fingers[id].contact_id = id;
                            active_finger_count++;

                        } else {
                            slot_active[id] = false;
                            tp_msg.fingers[id].tip_switch = 0;
                            tp_msg.fingers[id].contact_id = id;
                            slot_filter_x[id] = 0;
                            slot_filter_y[id] = 0;
                        }
                    }

                    tp_msg.actual_count = active_finger_count;
                    
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