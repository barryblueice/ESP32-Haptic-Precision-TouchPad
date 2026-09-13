#include "SYS/hid_msg.h"
#include "I2C/TP/i2c_hid.h"
#include "esp_log.h"

#include "sdkconfig.h"
#include <string.h>

#define TAG "TP_REPORT"

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
    #if CONFIG_PTP_SIMULATED_MOUSE_MODE
    report->wheel = msg->wheel;
    report->pan = msg->pan;
    #endif
}

/* Owned by the input parser. Hardware slots have stable IDs 0 through 4. */
static uint8_t ptp_active_mask;
static finger_t ptp_last_contact[5];

void ptp_report_reset(void)
{
    ptp_active_mask = 0;
    memset(ptp_last_contact, 0, sizeof(ptp_last_contact));
}

void parse_ptp_report(const tp_multi_msg_t *msg, ptp_report_t *report)
{
    *report = (ptp_report_t){.scan_time = msg->scan_time,
        .buttons = msg->button_mask ? 1 : 0};
    uint8_t active_mask = 0;
    for (unsigned id = 0; id < 5; ++id) {
        const tp_finger_t *contact = &msg->fingers[id];
        uint8_t bit = 1U << id;
        finger_t finger;
        if (contact->tip_switch) {
            active_mask |= bit;
            finger = (finger_t){.tip_conf_id = (id << 2) | 2 | (contact->confidence & 1U),
                .x = contact->x, .y = contact->y, .pressure_z = contact->pressure_z};
            ptp_last_contact[id] = finger;
        } else if (ptp_active_mask & bit) {
            /* Count the lift once, retaining the last reported position/confidence. */
            finger = ptp_last_contact[id];
            finger.tip_conf_id &= ~2U;
            finger.pressure_z = 0;
        } else {
            continue;
        }
        report->fingers[report->contact_count++] = finger;
    }
    ptp_active_mask = active_mask;
}
