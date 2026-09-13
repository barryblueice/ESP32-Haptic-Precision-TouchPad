#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Existing ESP-NOW ABI: little endian, four-byte type, packed payload. */
typedef uint32_t wireless_input_mode_t;
enum { MOUSE_MODE = 0, LEGACY_PTP_MODE = 1, VBUS_STATUS = 2,
       ALIVE_MODE = 3, HAPTIC_PTP_MODE = 4 };
typedef enum { TP_MOUSE_MODE = 0, TP_PTP_MODE = 1 } current_input_mode_t;
/* Input kind is also the USB instance, never the wireless mode command. */
typedef enum { REPORT_NONE = 0, REPORT_HAPTIC = 1,
               REPORT_LEGACY = 2, REPORT_MOUSE = 3 } report_kind_t;

typedef struct __attribute__((packed)) {
    uint8_t tip_conf_id; /* bit 0 confidence, bit 1 tip, bits 2..7 contact ID */
    uint16_t x, y;
} legacy_finger_t;
typedef struct __attribute__((packed)) {
    uint8_t tip_conf_id;
    uint16_t x, y;
    uint8_t pressure_z;
} haptic_finger_t;
typedef struct __attribute__((packed)) {
    legacy_finger_t fingers[5];
    uint16_t scan_time;
    uint8_t contact_count, buttons;
} legacy_ptp_report_t;
typedef struct __attribute__((packed)) {
    haptic_finger_t fingers[5];
    uint16_t scan_time;
    uint8_t contact_count, buttons;
} haptic_ptp_report_t;
typedef struct __attribute__((packed)) {
    uint8_t buttons;
    int8_t x, y, wheel, pan;
} mouse_hid_report_t;
typedef struct __attribute__((packed)) { uint8_t vbus_level; } vbus_msg_t;
typedef struct __attribute__((packed)) {
    uint8_t vbus_level, battery_level;
    uint32_t uptime;
} alive_msg_t;
typedef union {
    mouse_hid_report_t mouse;
    legacy_ptp_report_t legacy_ptp;
    haptic_ptp_report_t haptic_ptp;
    vbus_msg_t vbus;
    alive_msg_t alive;
} report_payload_t;
typedef struct __attribute__((packed)) {
    wireless_input_mode_t type;
    report_payload_t payload;
} wireless_msg_t;

_Static_assert(sizeof(wireless_input_mode_t) == 4, "wire type ABI");
_Static_assert(sizeof(mouse_hid_report_t) == 5, "mouse ABI");
_Static_assert(sizeof(legacy_ptp_report_t) == 29, "legacy ABI");
_Static_assert(sizeof(haptic_ptp_report_t) == 34, "haptic ABI");
_Static_assert(sizeof(alive_msg_t) == 6, "heartbeat ABI");
_Static_assert(offsetof(wireless_msg_t, payload) == 4, "payload offset ABI");
_Static_assert(sizeof(wireless_msg_t) == 38, "wire packet ABI");

#define REPORTID_HAPTIC_TOUCHPAD 0x01
#define REPORTID_LEGACY_TOUCHPAD 0x02
#define REPORTID_MOUSE 0x03
#define REPORTID_MAX_COUNT 0x04
#define REPORTID_HAPTIC_PTPHQA 0x05
#define REPORTID_LEGACY_PTPHQA 0x06
#define REPORTID_HAPTIC_FEATURE 0x06
#define REPORTID_LEGACY_FEATURE 0x07
#define REPORTID_FUNCTION_SWITCH 0x08
#define REPORTID_BUTTON_PRESS_THRESHOLD 0x40
#define REPORTID_HAPTIC_INTENSITY 0x41
#define REPORTID_HAPTIC_WAVEFORM_LIST 0x42
#define REPORTID_HAPTIC_MANUAL_TRIGGER 0x43
#define REPORTID_DFU_CMD 0xFF

bool wireless_decode(const uint8_t *data, int len, wireless_msg_t *out);
report_kind_t wireless_report_kind(uint32_t type);
size_t report_size(report_kind_t kind);
