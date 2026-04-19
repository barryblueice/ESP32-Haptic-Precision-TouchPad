#ifndef HID_MSG_H
#define HID_MSG_H

#include <stdio.h>
#include <stdlib.h>

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t tip_switch;
    uint8_t contact_id;
    uint8_t confidence;
} tp_finger_t;

typedef struct {
    tp_finger_t fingers[5];
    uint8_t actual_count;
    uint8_t button_mask;
    uint16_t scan_time;
} tp_multi_msg_t;

#if CONFIG_ORI_MOUSE_MODE:
typedef struct __attribute__((packed)) {
    uint8_t buttons;
    int8_t  x;
    int8_t  y;
} mouse_msg_t;
#else
typedef struct __attribute__((packed)) {
    uint8_t buttons;
    int8_t  x;
    int8_t  y;
    int8_t  wheel;
} mouse_msg_t;
#endif

typedef struct __attribute__((packed)) {
    uint8_t vbus_level;
} vbus_msg_t;

typedef struct __attribute__((packed)) {
    uint8_t vbus_level;
    uint8_t battery_level;
    uint32_t uptime;
} alive_msg_t;

typedef struct {
    bool active;
    uint32_t down_time;
    uint16_t start_x;
    uint16_t start_y;
    uint16_t max_move;
    bool tap_detected;
} tp_finger_life_t;

extern QueueHandle_t tp_queue;
extern QueueHandle_t mouse_queue;
extern QueueSetHandle_t main_queue_set;

typedef enum {
    MOUSE_MODE = 0,
    PTP_MODE = 1,
    VBUS_STATUS = 2,
    ALIVE_MODE = 3
} input_mode_t;

typedef struct __attribute__((packed)) {
    uint8_t tip_conf_id;  // Bit0:None, Bit1:Confidence, Bit2:Tip, Bit3:Confidence Tip
    uint16_t x;           
    uint16_t y;           
} finger_t;

typedef struct __attribute__((packed)) {
    finger_t fingers[5];   // 5 * 5 = 25 bytes
    uint16_t scan_time;    // 2 bytes
    uint8_t contact_count; // 1 byte
    uint8_t buttons;       // 1 byte
} ptp_report_t;

typedef struct __attribute__((packed)) {
    uint8_t buttons;
    int8_t  x;
    int8_t  y;
} mouse_hid_report_t;

typedef struct __attribute__((packed)) {
    input_mode_t type;
    union {
        mouse_hid_report_t mouse;
        ptp_report_t       ptp;
        vbus_msg_t         vbus;
        alive_msg_t        alive;
    } payload;
} wireless_msg_t;

typedef enum {
    WIRED_MODE = 0,
    _2_4_MODE = 1,
    BLE_MODE = 2
} device_mode_t;

extern int32_t current_mode;
extern uint8_t current_tp_mode;

#endif