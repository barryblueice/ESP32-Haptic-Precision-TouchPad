#ifndef WIRELESS_H
#define WIRELESS_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

extern QueueHandle_t legacy_tp_queue;
extern QueueHandle_t haptic_tp_queue;
extern QueueHandle_t mouse_queue;
extern QueueSetHandle_t main_queue_set;
extern uint32_t last_seen_timestamp;

typedef struct {
    struct {
        uint16_t x;
        uint16_t y;
        uint8_t  tip_switch;
        uint8_t  contact_id;
        uint8_t tip_switch_prev;
    } fingers[5];
    uint8_t actual_count;
    uint8_t button_mask;
} legacy_tp_multi_msg_t;

typedef struct __attribute__((packed)) {
    uint8_t buttons;
    int8_t  x;
    int8_t  y;
    int8_t  wheel;
    int8_t  pan;
} mouse_msg_t;

typedef struct __attribute__((packed)) {
    uint8_t vbus_level;
} vbus_msg_t;

typedef struct __attribute__((packed)) {
    uint8_t vbus_level;
    uint8_t battery_level;
    uint32_t uptime;
} alive_msg_t;

typedef enum {
    MOUSE_MODE = 0,
    LEGACY_PTP_MODE = 1,
    VBUS_STATUS = 2,
    ALIVE_MODE = 3,
    HAPTIC_PTP_MODE = 4,
} wireless_input_mode_t;

typedef enum {
    TP_MOUSE_MODE = 0,
    TP_PTP_MODE = 1
} current_input_mode_t;

typedef struct __attribute__((packed)) {
    uint8_t tip_conf_id;  // Bit0:Conf, Bit1:Tip, Bit2-7:ID
    uint16_t x;
    uint16_t y;
} legacy_finger_t;

typedef struct __attribute__((packed)) {
    uint8_t tip_conf_id;  // Bit0:None, Bit1:Confidence, Bit2:Tip, Bit3:Confidence Tip
    uint16_t x;
    uint16_t y;
    uint8_t pressure_z;
} haptic_finger_t;

typedef struct __attribute__((packed)) {
    legacy_finger_t fingers[5];   // 5 * 5 = 25 bytes
    uint16_t scan_time;    // 2 bytes
    uint8_t contact_count; // 1 byte
    uint8_t buttons;       // 1 byte
} legacy_ptp_report_t;

typedef struct __attribute__((packed)) {
    haptic_finger_t fingers[5];   // 5 * 5 = 25 bytes
    uint16_t scan_time;    // 2 bytes
    uint8_t contact_count; // 1 byte
    uint8_t buttons;       // 1 byte
} haptic_ptp_report_t;

typedef struct __attribute__((packed)) {
    uint8_t buttons;
    int8_t  x;
    int8_t  y;
    int8_t  wheel;
    int8_t  pan;
} mouse_hid_report_t;

typedef struct __attribute__((packed)) {
    wireless_input_mode_t type;
    union {
        mouse_hid_report_t        mouse;
        legacy_ptp_report_t       legacy_ptp;
        haptic_ptp_report_t       haptic_ptp;
        vbus_msg_t                vbus;
        alive_msg_t               alive;
    } payload;
} wireless_msg_t;

extern volatile uint8_t current_mode;
extern uint8_t broadcast_mac[6];

void wifi_recieve_task_init();
void broadcast_init();
void monitor_link_task(void *arg);

#endif