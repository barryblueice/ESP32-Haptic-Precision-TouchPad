#include "SYS/hid_msg.h"
#include "I2C/TP/i2c_hid.h"
#include "esp_log.h"

#include <math.h>

#include "sdkconfig.h"

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

void parse_ptp_report(const tp_multi_msg_t *msg, ptp_report_t *report) {

    // uint8_t packet[4];

    // i2c_master_receive(dev_haptic_motor_handle, packet, 4, 100);

    // ESP_LOG_BUFFER_HEX(TAG, packet, sizeof(packet));

    report->scan_time = msg->scan_time;
    report->contact_count = msg->actual_count;
    report->buttons = (msg->button_mask > 0) ? 0x01 : 0x00;

    for (int i = 0; i < 5; i++) {
        report->fingers[i].x = msg->fingers[i].x;
        report->fingers[i].y = msg->fingers[i].y;
        report->fingers[i].pressure_z = msg->fingers[i].tip_switch ? msg->fingers[i].pressure_z : 0;

        uint8_t base_id = (msg->fingers[i].tip_switch << 1) | (msg->fingers[i].confidence & 0x01);

        report->fingers[i].tip_conf_id = (i << 2) | base_id;
    }

}

#if CONFIG_PTP_SIMULATED_MOUSE_MODE

static struct {
    float rem_x, rem_y, rem_wheel, rem_pan;
    uint16_t last_x, last_y;
    float last_scroll_x, last_scroll_y;
    float tap_start_x, tap_start_y;
    uint16_t tap_start_time;
    uint16_t last_single_tap_time;
    uint8_t tap_max_count;
    bool has_move_anchor;
    bool has_scroll_anchor;
    bool tap_active;
    bool tap_moved;
    bool has_last_single_tap;
    bool double_tap_drag_candidate;
    bool drag_active;
    bool suppress_tap_until_release;
    bool force_click_seen;
    bool click_release_pending;
} m_state = {0};

void parse_ptp_simulated_mouse_report(const tp_multi_msg_t *msg, mouse_hid_report_t *out_report) {
    out_report->buttons = msg->button_mask & 0x07;
    out_report->x = 0;
    out_report->y = 0;
    out_report->wheel = 0;
    out_report->pan = 0;

    int active_count = 0;
    int first_active = find_active_finger(msg, 0);
    int second_active = (first_active >= 0) ? find_active_finger(msg, first_active + 1) : -1;

    for (int i = first_active; i >= 0; i = find_active_finger(msg, i + 1)) {
        active_count++;
    }

    if (out_report->buttons != 0) {
        m_state.force_click_seen = true;
    }

    if (m_state.suppress_tap_until_release) {
        reset_move_state();
        reset_scroll_state();
        if (active_count == 0) {
            m_state.suppress_tap_until_release = false;
        }
        return;
    }

    if (m_state.tap_active && active_count < m_state.tap_max_count) {
        uint8_t released_tap_count = m_state.tap_max_count;
        reset_move_state();
        reset_scroll_state();
        handle_tap_release(msg, out_report);
        if (active_count > 0 && released_tap_count > 1) {
            m_state.suppress_tap_until_release = true;
        }
        return;
    }

    if (active_count > 0) {
        float centroid_x = 0.0f;
        float centroid_y = 0.0f;
        get_active_centroid(msg, active_count, &centroid_x, &centroid_y);
        update_tap_state(msg, active_count, centroid_x, centroid_y);
    }

    if (active_count == 1) {
        reset_scroll_state();
        handle_single_finger_move(msg, first_active, out_report);
        if (m_state.drag_active && out_report->buttons == 0) {
            out_report->buttons = 0x01;
        }
    } else if (active_count >= 2) {
        reset_move_state();
        handle_dual_finger_scroll(msg, first_active, second_active, out_report);
    } else {
        reset_move_state();
        reset_scroll_state();
        handle_tap_release(msg, out_report);
    }
}
#endif
