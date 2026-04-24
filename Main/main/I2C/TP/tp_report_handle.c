#include "SYS/hid_msg.h"
#include "I2C/TP/i2c_hid.h"
#include "esp_log.h"

#include <math.h>

#include "sdkconfig.h"

#define TAG "TP_REPORT"

#define SENSITIVITY 2.0f
#define CONSTRAIN(amt, low, high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
#define DEADZONE 1.2f
#define JUMP_THRESHOLD 200.0f
#define TAP_MAX_MOVE 60.0f
#define TAP_MAX_TIME 3000
#define SCROLL_GAIN 0.12f

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
    uint8_t tap_max_count;
    bool has_move_anchor;
    bool has_scroll_anchor;
    bool tap_active;
    bool tap_moved;
    bool click_release_pending;
} m_state = {0};

static int find_active_finger(const tp_multi_msg_t *msg, int start_index) {
    for (int i = start_index; i < 5; i++) {
        if (msg->fingers[i].tip_switch) {
            return i;
        }
    }
    return -1;
}

static void reset_move_state(void) {
    m_state.has_move_anchor = false;
    m_state.rem_x = 0;
    m_state.rem_y = 0;
}

static void reset_scroll_state(void) {
    m_state.has_scroll_anchor = false;
    m_state.rem_wheel = 0;
    m_state.rem_pan = 0;
    m_state.last_scroll_x = 0;
    m_state.last_scroll_y = 0;
}

static uint16_t scan_time_delta(uint16_t now, uint16_t then) {
    return (uint16_t)(now - then);
}

static void get_active_centroid(const tp_multi_msg_t *msg, int active_count, float *x, float *y) {
    float sum_x = 0.0f;
    float sum_y = 0.0f;

    for (int i = 0; i < 5; i++) {
        if (msg->fingers[i].tip_switch) {
            sum_x += (float)msg->fingers[i].x;
            sum_y += (float)msg->fingers[i].y;
        }
    }

    *x = sum_x / (float)active_count;
    *y = sum_y / (float)active_count;
}

static void update_tap_state(const tp_multi_msg_t *msg, int active_count, float centroid_x, float centroid_y) {
    if (!m_state.tap_active) {
        m_state.tap_active = true;
        m_state.tap_moved = false;
        m_state.tap_max_count = active_count;
        m_state.tap_start_time = msg->scan_time;
        m_state.tap_start_x = centroid_x;
        m_state.tap_start_y = centroid_y;
        return;
    }

    if (active_count > m_state.tap_max_count) {
        m_state.tap_max_count = active_count;
    }

    float dx = centroid_x - m_state.tap_start_x;
    float dy = centroid_y - m_state.tap_start_y;
    if (sqrtf((dx * dx) + (dy * dy)) > TAP_MAX_MOVE) {
        m_state.tap_moved = true;
    }
}

static uint8_t tap_button_mask(uint8_t finger_count) {
    switch (finger_count) {
    case 1:
        return 0x01;
    case 2:
        return 0x02;
    case 3:
        return 0x04;
    default:
        return 0x00;
    }
}

static void handle_tap_release(const tp_multi_msg_t *msg, mouse_hid_report_t *out_report) {
    if (!m_state.tap_active) {
        return;
    }

    uint8_t buttons = tap_button_mask(m_state.tap_max_count);
    uint16_t elapsed = scan_time_delta(msg->scan_time, m_state.tap_start_time);
    if (!m_state.tap_moved && elapsed <= TAP_MAX_TIME && buttons != 0) {
        out_report->buttons = buttons;
        m_state.click_release_pending = true;
    }

    m_state.tap_active = false;
    m_state.tap_max_count = 0;
    m_state.tap_moved = false;
}

bool ptp_simulated_mouse_click_needs_release(void) {
    bool needs_release = m_state.click_release_pending;
    m_state.click_release_pending = false;
    return needs_release;
}

static void handle_single_finger_move(const tp_multi_msg_t *msg, int finger_index, mouse_hid_report_t *out_report) {
    uint16_t curr_x = msg->fingers[finger_index].x;
    uint16_t curr_y = msg->fingers[finger_index].y;

    if (!m_state.has_move_anchor) {
        m_state.last_x = curr_x;
        m_state.last_y = curr_y;
        m_state.has_move_anchor = true;
        return;
    }

    float dx = ((float)curr_x - (float)m_state.last_x) * SENSITIVITY;
    float dy = ((float)curr_y - (float)m_state.last_y) * SENSITIVITY;

    m_state.last_x = curr_x;
    m_state.last_y = curr_y;

    if (fabsf(dx) > JUMP_THRESHOLD || fabsf(dy) > JUMP_THRESHOLD) {
        return;
    }

    if (fabsf(dx) < DEADZONE) {
        dx = 0.0f;
    }
    if (fabsf(dy) < DEADZONE) {
        dy = 0.0f;
    }

    m_state.rem_x += dx;
    m_state.rem_y += dy;

    int32_t x = (int32_t)m_state.rem_x;
    int32_t y = (int32_t)m_state.rem_y;

    out_report->x = (int8_t)CONSTRAIN(x, -127, 127);
    out_report->y = (int8_t)CONSTRAIN(y, -127, 127);

    m_state.rem_x -= (float)out_report->x;
    m_state.rem_y -= (float)out_report->y;
}

static void handle_dual_finger_scroll(const tp_multi_msg_t *msg, int first_index, int second_index, mouse_hid_report_t *out_report) {
    float avg_x = ((float)msg->fingers[first_index].x + (float)msg->fingers[second_index].x) / 2.0f;
    float avg_y = ((float)msg->fingers[first_index].y + (float)msg->fingers[second_index].y) / 2.0f;

    if (!m_state.has_scroll_anchor) {
        m_state.last_scroll_x = avg_x;
        m_state.last_scroll_y = avg_y;
        m_state.has_scroll_anchor = true;
        return;
    }

    m_state.rem_pan += (avg_x - m_state.last_scroll_x) * SCROLL_GAIN;
    m_state.rem_wheel += (avg_y - m_state.last_scroll_y) * SCROLL_GAIN;
    m_state.last_scroll_x = avg_x;
    m_state.last_scroll_y = avg_y;

    int32_t pan = (int32_t)m_state.rem_pan;
    int32_t wheel = (int32_t)m_state.rem_wheel;
    out_report->pan = (int8_t)CONSTRAIN(pan, -127, 127);
    out_report->wheel = (int8_t)CONSTRAIN(wheel, -127, 127);
    m_state.rem_pan -= (float)out_report->pan;
    m_state.rem_wheel -= (float)out_report->wheel;
}

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

    if (active_count > 0) {
        float centroid_x = 0.0f;
        float centroid_y = 0.0f;
        get_active_centroid(msg, active_count, &centroid_x, &centroid_y);
        update_tap_state(msg, active_count, centroid_x, centroid_y);
    }

    if (active_count == 1) {
        reset_scroll_state();
        handle_single_finger_move(msg, first_active, out_report);
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
