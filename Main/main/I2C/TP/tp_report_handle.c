#include "SYS/hid_msg.h"
#include "I2C/TP/i2c_hid.h"

#include <math.h>

#include "sdkconfig.h"

#define SENSITIVITY 1.0f
#define CONSTRAIN(amt, low, high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
#define DEADZONE 1.2f
#define JUMP_THRESHOLD 200.0f

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

        uint8_t base_id = (msg->fingers[i].tip_switch << 1) | (msg->fingers[i].confidence & 0x01);

        report->fingers[i].tip_conf_id = (i << 2) | base_id;
    }

}

#if CONFIG_PTP_SIMULATED_MOUSE_MODE

static struct {
    float rem_x, rem_y, rem_wheel;
    uint16_t last_x, last_y;
    gesture_state_t current_gesture;
    bool is_first_frame;
} m_state = {0, 0, 0, 0, 0, GESTURE_NONE, true};

static void reset_mouse_state() {
    m_state.is_first_frame = true;
    m_state.rem_x = 0;
    m_state.rem_y = 0;
    m_state.rem_wheel = 0;
    m_state.last_x = 0;
    m_state.last_y = 0;
}

static void handle_single_finger_move(const tp_multi_msg_t *msg, mouse_msg_t *out) {
    uint16_t curr_x = msg->fingers[0].x;
    uint16_t curr_y = msg->fingers[0].y;

    if (m_state.is_first_frame) {
        m_state.last_x = curr_x;
        m_state.last_y = curr_y;
        m_state.is_first_frame = false;
        return;
    }

    float dx = (float)curr_x - (float)m_state.last_x;
    float dy = (float)curr_y - (float)m_state.last_y;

    if (fabsf(dx) > JUMP_THRESHOLD || fabsf(dy) > JUMP_THRESHOLD) {
        m_state.last_x = curr_x;
        m_state.last_y = curr_y;
        return;
    }

    if (fabsf(dx) < DEADZONE) dx = 0.0f;
    if (fabsf(dy) < DEADZONE) dy = 0.0f;

    m_state.rem_x += dx;
    m_state.rem_y += dy;

    int32_t out_x = (int32_t)m_state.rem_x;
    int32_t out_y = (int32_t)m_state.rem_y;

    out->x = (int8_t)CONSTRAIN(out_x, -127, 127);
    out->y = (int8_t)CONSTRAIN(out_y, -127, 127);

    m_state.rem_x -= (float)out->x;
    m_state.rem_y -= (float)out->y;

    m_state.last_x = curr_x;
    m_state.last_y = curr_y;
}

void handle_dual_finger_scroll(const tp_multi_msg_t *msg, mouse_msg_t *out) {
    float avg_y = (msg->fingers[0].y + msg->fingers[1].y) / 2.0f;
    static float last_avg_y = 0;

    if (m_state.is_first_frame || last_avg_y == 0) {
        last_avg_y = avg_y;
        m_state.is_first_frame = false;
        return;
    }

    float dy = avg_y - last_avg_y;
    float scroll_gain = 0.12f;

    m_state.rem_wheel += (dy * scroll_gain);
    int32_t out_w = (int32_t)m_state.rem_wheel;

    if (out_w > 127)  out_w = 127;
    if (out_w < -127) out_w = -127;

    out->wheel = (int8_t)out_w;

    m_state.rem_wheel -= out->wheel;
    m_state.rem_wheel -= out->wheel;

    last_avg_y = avg_y;
}

void parse_ptp_simulated_mouse_report(const tp_multi_msg_t *msg, mouse_msg_t *out_report) {

    out_report->x = 0; out_report->y = 0; out_report->wheel = 0; out_report->buttons = 0;

    int active_count = msg->actual_count;

    switch (active_count) {
    case 1:
        handle_single_finger_move(msg, out_report);
        break;
    case 2:
        handle_dual_finger_scroll(msg, out_report);
        break;
    default:
        m_state.is_first_frame = true;
        m_state.current_gesture = GESTURE_NONE;
        break;
    }
}
#endif