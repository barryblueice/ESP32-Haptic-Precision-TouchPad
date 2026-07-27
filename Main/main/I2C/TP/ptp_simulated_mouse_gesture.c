#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h"

#include "SYS/hid_msg.h"
#include "I2C/TP/i2c_hid.h"

#define MAX_TOUCH_CONTACTS 5
#define HID_AXIS_MIN (-127)
#define HID_AXIS_MAX 127

/* scan_time uses 100 us units (see update_simulated_scan_time()). */
#define TAP_MAX_MOVE 60.0f
#define TAP_MAX_TIME 3000U
#define DOUBLE_TAP_MAX_INTERVAL 2500U
#define DOUBLE_TAP_DRAG_MIN_MOVE 30.0f
#define DOUBLE_TAP_DRAG_HOLD_TIME 1800U

/* Accumulation removes stationary noise without losing slow movement. */
#define POINTER_MICRO_MOTION_THRESHOLD 1.5f
#define POINTER_JUMP_THRESHOLD 100.0f
#define POINTER_GAIN_MIN 0.70f
#define POINTER_ACCEL_START 2.0f
#define POINTER_ACCEL_END 24.0f

/* Delay scroll activation so two-finger taps do not leak wheel events. */
#define SCROLL_START_THRESHOLD 12.0f
#define SCROLL_JUMP_THRESHOLD 100.0f
#define SCROLL_GAIN_MIN 0.09f
#define SCROLL_GAIN_MAX 0.16f
#define SCROLL_ACCEL_START 2.0f
#define SCROLL_ACCEL_END 18.0f

#if CONFIG_PTP_SIMULATED_MOUSE_MODE

typedef enum {
    SCROLL_AXIS_UNDECIDED = 0,
    SCROLL_AXIS_HORIZONTAL,
    SCROLL_AXIS_VERTICAL,
} scroll_axis_t;

typedef struct {
    float rem_x;
    float rem_y;
    float rem_wheel;
    float rem_pan;
    float pending_move_x;
    float pending_move_y;
    float scroll_origin_x;
    float scroll_origin_y;
    float last_scroll_x;
    float last_scroll_y;
    float tap_start_x;
    float tap_start_y;
    uint16_t last_x;
    uint16_t last_y;
    uint16_t tap_start_time;
    uint16_t last_single_tap_time;
    uint8_t move_contact_index;
    uint8_t scroll_contact_mask;
    uint8_t tap_contact_mask;
    uint8_t tap_max_count;
    scroll_axis_t scroll_axis;
    bool has_move_anchor;
    bool has_scroll_anchor;
    bool scroll_active;
    bool tap_active;
    bool tap_moved;
    bool has_last_single_tap;
    bool double_tap_drag_candidate;
    bool drag_active;
    bool suppress_tap_until_release;
    bool force_click_seen;
    bool click_release_pending;
} simulated_mouse_state_t;

static simulated_mouse_state_t m_state = {0};

static uint16_t scan_time_delta(uint16_t now, uint16_t then) {
    return (uint16_t)(now - then);
}

static float vector_length(float x, float y) {
    return sqrtf((x * x) + (y * y));
}

static float interpolate_gain(float speed,
                              float min_gain,
                              float max_gain,
                              float accel_start,
                              float accel_end) {
    if (speed <= accel_start) {
        return min_gain;
    }
    if (speed >= accel_end) {
        return max_gain;
    }

    float ratio = (speed - accel_start) / (accel_end - accel_start);
    return min_gain + ((max_gain - min_gain) * ratio);
}

static int8_t emit_hid_axis(float *remainder, float delta) {
    *remainder += delta;

    int32_t whole = (int32_t)*remainder;
    if (whole > HID_AXIS_MAX) {
        /* Avoid cursor/wheel motion continuing from a saturated backlog. */
        *remainder = 0.0f;
        return HID_AXIS_MAX;
    }
    if (whole < HID_AXIS_MIN) {
        *remainder = 0.0f;
        return HID_AXIS_MIN;
    }

    *remainder -= (float)whole;
    return (int8_t)whole;
}

static bool is_gesture_contact_active(const tp_finger_t *finger) {
    return finger->tip_switch && finger->confidence;
}

static uint8_t get_active_contact_mask(const tp_multi_msg_t *msg) {
    uint8_t mask = 0;

    for (int i = 0; i < MAX_TOUCH_CONTACTS; i++) {
        if (is_gesture_contact_active(&msg->fingers[i])) {
            mask |= (uint8_t)(1U << i);
        }
    }

    return mask;
}

static int count_active_contacts(uint8_t mask) {
    int count = 0;

    while (mask != 0) {
        count += mask & 1U;
        mask >>= 1;
    }

    return count;
}

static int find_active_finger(const tp_multi_msg_t *msg, int start_index) {
    for (int i = start_index; i < MAX_TOUCH_CONTACTS; i++) {
        if (is_gesture_contact_active(&msg->fingers[i])) {
            return i;
        }
    }
    return -1;
}

static void reset_move_state(void) {
    m_state.has_move_anchor = false;
    m_state.pending_move_x = 0.0f;
    m_state.pending_move_y = 0.0f;
    m_state.rem_x = 0.0f;
    m_state.rem_y = 0.0f;
}

static void reset_scroll_state(void) {
    m_state.has_scroll_anchor = false;
    m_state.scroll_active = false;
    m_state.scroll_axis = SCROLL_AXIS_UNDECIDED;
    m_state.scroll_contact_mask = 0;
    m_state.scroll_origin_x = 0.0f;
    m_state.scroll_origin_y = 0.0f;
    m_state.last_scroll_x = 0.0f;
    m_state.last_scroll_y = 0.0f;
    m_state.rem_wheel = 0.0f;
    m_state.rem_pan = 0.0f;
}

static void get_active_centroid(const tp_multi_msg_t *msg,
                                int active_count,
                                float *x,
                                float *y) {
    float sum_x = 0.0f;
    float sum_y = 0.0f;

    for (int i = 0; i < MAX_TOUCH_CONTACTS; i++) {
        if (is_gesture_contact_active(&msg->fingers[i])) {
            sum_x += (float)msg->fingers[i].x;
            sum_y += (float)msg->fingers[i].y;
        }
    }

    *x = sum_x / (float)active_count;
    *y = sum_y / (float)active_count;
}

static void update_tap_state(const tp_multi_msg_t *msg,
                             int active_count,
                             uint8_t active_mask,
                             float centroid_x,
                             float centroid_y) {
    if (!m_state.tap_active) {
        m_state.tap_active = true;
        m_state.tap_moved = false;
        m_state.tap_max_count = (uint8_t)active_count;
        m_state.tap_contact_mask = active_mask;
        m_state.tap_start_time = msg->scan_time;
        m_state.tap_start_x = centroid_x;
        m_state.tap_start_y = centroid_y;
        m_state.double_tap_drag_candidate =
            active_count == 1 &&
            m_state.has_last_single_tap &&
            scan_time_delta(msg->scan_time, m_state.last_single_tap_time) <=
                DOUBLE_TAP_MAX_INTERVAL;
        return;
    }

    if (active_count > m_state.tap_max_count) {
        m_state.tap_max_count = (uint8_t)active_count;
        m_state.tap_contact_mask = active_mask;
        m_state.tap_start_time = msg->scan_time;
        m_state.tap_start_x = centroid_x;
        m_state.tap_start_y = centroid_y;
        m_state.double_tap_drag_candidate = false;
        return;
    }

    /* A slot replacement is not continuous motion: cancel and re-anchor. */
    if (active_mask != m_state.tap_contact_mask) {
        m_state.tap_contact_mask = active_mask;
        m_state.tap_moved = true;
        m_state.double_tap_drag_candidate = false;
        m_state.tap_start_x = centroid_x;
        m_state.tap_start_y = centroid_y;
        m_state.tap_start_time = msg->scan_time;
        return;
    }

    float dx = centroid_x - m_state.tap_start_x;
    float dy = centroid_y - m_state.tap_start_y;
    float distance = vector_length(dx, dy);
    uint16_t elapsed = scan_time_delta(msg->scan_time, m_state.tap_start_time);

    if (m_state.double_tap_drag_candidate &&
        active_count == 1 &&
        (distance > DOUBLE_TAP_DRAG_MIN_MOVE ||
         elapsed >= DOUBLE_TAP_DRAG_HOLD_TIME)) {
        m_state.drag_active = true;
        m_state.has_last_single_tap = false;
    }

    if (distance > TAP_MAX_MOVE) {
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

static void clear_tap_tracking(void) {
    m_state.tap_active = false;
    m_state.tap_max_count = 0;
    m_state.tap_contact_mask = 0;
    m_state.tap_moved = false;
    m_state.double_tap_drag_candidate = false;
    m_state.force_click_seen = false;
}

static void handle_tap_release(const tp_multi_msg_t *msg,
                               mouse_hid_report_t *out_report) {
    if (!m_state.tap_active) {
        m_state.drag_active = false;
        m_state.force_click_seen = false;
        return;
    }

    uint8_t buttons = tap_button_mask(m_state.tap_max_count);
    uint16_t elapsed = scan_time_delta(msg->scan_time, m_state.tap_start_time);

    if (m_state.drag_active) {
        m_state.drag_active = false;
        m_state.has_last_single_tap = false;
    } else if (m_state.force_click_seen) {
        /* The physical/force click already emitted its own button report. */
        m_state.has_last_single_tap = false;
    } else if (!m_state.tap_moved && elapsed <= TAP_MAX_TIME && buttons != 0) {
        out_report->buttons = buttons;
        m_state.click_release_pending = true;

        if (m_state.tap_max_count == 1) {
            m_state.has_last_single_tap = true;
            m_state.last_single_tap_time = msg->scan_time;
        } else {
            m_state.has_last_single_tap = false;
        }
    } else {
        m_state.has_last_single_tap = false;
    }

    clear_tap_tracking();
}

static void handle_single_finger_move(const tp_multi_msg_t *msg,
                                      int finger_index,
                                      mouse_hid_report_t *out_report) {
    uint16_t curr_x = msg->fingers[finger_index].x;
    uint16_t curr_y = msg->fingers[finger_index].y;

    if (!m_state.has_move_anchor ||
        m_state.move_contact_index != (uint8_t)finger_index) {
        reset_move_state();
        m_state.last_x = curr_x;
        m_state.last_y = curr_y;
        m_state.move_contact_index = (uint8_t)finger_index;
        m_state.has_move_anchor = true;
        return;
    }

    float raw_dx = (float)curr_x - (float)m_state.last_x;
    float raw_dy = (float)curr_y - (float)m_state.last_y;
    m_state.last_x = curr_x;
    m_state.last_y = curr_y;

    if (fabsf(raw_dx) > POINTER_JUMP_THRESHOLD ||
        fabsf(raw_dy) > POINTER_JUMP_THRESHOLD) {
        m_state.pending_move_x = 0.0f;
        m_state.pending_move_y = 0.0f;
        return;
    }

    m_state.pending_move_x += raw_dx;
    m_state.pending_move_y += raw_dy;

    float pending_length =
        vector_length(m_state.pending_move_x, m_state.pending_move_y);
    if (pending_length < POINTER_MICRO_MOTION_THRESHOLD) {
        return;
    }

    float gain = interpolate_gain(pending_length,
                                  POINTER_GAIN_MIN,
                                  SENSITIVITY,
                                  POINTER_ACCEL_START,
                                  POINTER_ACCEL_END);
    float move_x = m_state.pending_move_x * gain;
    float move_y = m_state.pending_move_y * gain;
    m_state.pending_move_x = 0.0f;
    m_state.pending_move_y = 0.0f;

    out_report->x = emit_hid_axis(&m_state.rem_x, move_x);
    out_report->y = emit_hid_axis(&m_state.rem_y, move_y);
}

static void handle_dual_finger_scroll(const tp_multi_msg_t *msg,
                                      int first_index,
                                      int second_index,
                                      uint8_t active_mask,
                                      mouse_hid_report_t *out_report) {
    float avg_x =
        ((float)msg->fingers[first_index].x +
         (float)msg->fingers[second_index].x) /
        2.0f;
    float avg_y =
        ((float)msg->fingers[first_index].y +
         (float)msg->fingers[second_index].y) /
        2.0f;

    if (!m_state.has_scroll_anchor ||
        m_state.scroll_contact_mask != active_mask) {
        reset_scroll_state();
        m_state.scroll_origin_x = avg_x;
        m_state.scroll_origin_y = avg_y;
        m_state.last_scroll_x = avg_x;
        m_state.last_scroll_y = avg_y;
        m_state.scroll_contact_mask = active_mask;
        m_state.has_scroll_anchor = true;
        return;
    }

    float step_x = avg_x - m_state.last_scroll_x;
    float step_y = avg_y - m_state.last_scroll_y;
    m_state.last_scroll_x = avg_x;
    m_state.last_scroll_y = avg_y;

    if (fabsf(step_x) > SCROLL_JUMP_THRESHOLD ||
        fabsf(step_y) > SCROLL_JUMP_THRESHOLD) {
        if (!m_state.scroll_active) {
            m_state.scroll_origin_x = avg_x;
            m_state.scroll_origin_y = avg_y;
        }
        return;
    }

    if (!m_state.scroll_active) {
        float total_x = avg_x - m_state.scroll_origin_x;
        float total_y = avg_y - m_state.scroll_origin_y;

        if (vector_length(total_x, total_y) < SCROLL_START_THRESHOLD) {
            return;
        }

        m_state.scroll_active = true;
        m_state.scroll_axis =
            fabsf(total_x) > fabsf(total_y) ?
                SCROLL_AXIS_HORIZONTAL :
                SCROLL_AXIS_VERTICAL;
        m_state.tap_moved = true;

        /* Discard activation travel to avoid an initial wheel/pan burst. */
        m_state.rem_pan = 0.0f;
        m_state.rem_wheel = 0.0f;
        return;
    }

    float speed = vector_length(step_x, step_y);
    float gain = interpolate_gain(speed,
                                  SCROLL_GAIN_MIN,
                                  SCROLL_GAIN_MAX,
                                  SCROLL_ACCEL_START,
                                  SCROLL_ACCEL_END);

    if (m_state.scroll_axis == SCROLL_AXIS_HORIZONTAL) {
        out_report->pan = emit_hid_axis(&m_state.rem_pan, step_x * gain);
    } else {
        out_report->wheel = emit_hid_axis(&m_state.rem_wheel, step_y * gain);
    }
}

bool ptp_simulated_mouse_click_needs_release(void) {
    bool needs_release = m_state.click_release_pending;
    m_state.click_release_pending = false;
    return needs_release;
}

void parse_ptp_simulated_mouse_report(const tp_multi_msg_t *msg,
                                      mouse_hid_report_t *out_report) {
    out_report->buttons = msg->button_mask & 0x07;
    out_report->x = 0;
    out_report->y = 0;
    out_report->wheel = 0;
    out_report->pan = 0;

    uint8_t active_mask = get_active_contact_mask(msg);
    int active_count = count_active_contacts(active_mask);
    int first_active = find_active_finger(msg, 0);
    int second_active =
        first_active >= 0 ? find_active_finger(msg, first_active + 1) : -1;

    if (out_report->buttons != 0) {
        m_state.force_click_seen = true;
    }

    if (m_state.suppress_tap_until_release) {
        reset_move_state();
        reset_scroll_state();
        if (active_count == 0) {
            m_state.suppress_tap_until_release = false;
            clear_tap_tracking();
        }
        return;
    }

    /* Finish a multi-finger tap as soon as its first finger is lifted. */
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
        update_tap_state(msg,
                         active_count,
                         active_mask,
                         centroid_x,
                         centroid_y);
    }

    if (active_count == 1) {
        reset_scroll_state();
        handle_single_finger_move(msg, first_active, out_report);
        if (m_state.drag_active && out_report->buttons == 0) {
            out_report->buttons = 0x01;
        }
    } else if (active_count == 2) {
        reset_move_state();
        handle_dual_finger_scroll(msg,
                                  first_active,
                                  second_active,
                                  active_mask,
                                  out_report);
    } else if (active_count >= 3) {
        /* Reserve three fingers for the middle-click tap. */
        reset_move_state();
        reset_scroll_state();
    } else {
        reset_move_state();
        reset_scroll_state();
        handle_tap_release(msg, out_report);
    }
}

#endif
