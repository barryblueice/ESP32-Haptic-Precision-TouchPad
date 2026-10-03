#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h"

#include "SYS/hid_msg.h"
#include "I2C/TP/i2c_hid.h"

#define MAX_TOUCH_CONTACTS 5
#define HID_AXIS_MIN (-127)
#define HID_AXIS_MAX 127

/* Single-finger pointer. The parser receives the jump-guarded raw
 * coordinates (before the PTP position low-pass), so all pointer
 * smoothing and acceleration live here. scan_time uses 100 us units. */

/* One-euro adaptive low-pass: the cutoff follows speed, so a resting finger is
 * smoothed hard (jitter rejected without a positional dead zone, hence no
 * release jump) while a moving finger stays responsive. It self-tunes to the
 * user's motion, needs no training data, and costs a few multiply-adds. */
#define POINTER_EURO_MIN_CUTOFF 1.4f
#define POINTER_EURO_BETA 0.004f
#define POINTER_EURO_D_CUTOFF 1.0f

#define POINTER_JUMP_THRESHOLD 300.0f

/* Acceleration is driven by filtered speed (raw units/s) instead of the
 * accumulated distance, so the curve tracks real hand motion. The range keeps
 * the previous feel: ~2 raw units per sample at the start, ~24 at the end. */
#define POINTER_ACCEL_START 200.0f
#define POINTER_ACCEL_END 2400.0f
#define POINTER_GAIN_MIN 0.70f
#define POINTER_GAIN_MAX SENSITIVITY

/* A bounded sub-count remainder is carried so fast flicks lose no motion, yet
 * a saturated burst cannot leave the pointer coasting after the finger stops. */
#define POINTER_REMAINDER_LIMIT 96.0f

/* Stationary jitter is learned while the estimated speed is low: the residual
 * (raw - filtered) is low-passed into an estimate that sets the emit floor, so
 * a resting finger never drifts and deliberate slow motion still moves. */
#define POINTER_JITTER_ALPHA 0.02f
#define POINTER_JITTER_MIN 0.35f
#define POINTER_JITTER_MAX 1.5f
#define POINTER_STILL_SPEED 25.0f
#define POINTER_EMIT_FLOOR 0.35f

/* Anchor key for the two-finger middle-button drag; finger slots are 0..4. */
#define MOVE_ANCHOR_CENTROID 0xFFU

/* Tap / click. */
#define TAP_MAX_MOVE 60.0f
#define TAP_MAX_TIME 3000U
/* The active contact count/mask must stay changed for this many frames before
 * it is adopted. A one-frame ghost finger then neither promotes a left tap to a
 * right click nor tears down an in-flight scroll. */
#define CONTACT_COUNT_CONFIRM_FRAMES 2U
#define DOUBLE_TAP_MAX_INTERVAL 3800U
#define DOUBLE_TAP_DRAG_MIN_MOVE 30.0f
#define DOUBLE_TAP_DRAG_HOLD_TIME 1800U

/* Two-finger scroll. */
#define SCROLL_EURO_MIN_CUTOFF 1.2f
#define SCROLL_EURO_BETA 0.003f
#define SCROLL_START_THRESHOLD 12.0f
/* Raw-to-raw centroid step (not raw-vs-filtered, which would trip on the
 * filter's own lag) that can only be a contact jump. */
#define SCROLL_JUMP_THRESHOLD 300.0f
#define SCROLL_AXIS_MARGIN 1.25f
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
    float tap_start_x[MAX_TOUCH_CONTACTS];
    float tap_start_y[MAX_TOUCH_CONTACTS];
    float tap_max_move;
    uint16_t tap_start_time;
    uint16_t last_single_tap_time;
    uint8_t move_contact_index;
    uint8_t scroll_contact_mask;
    uint8_t tap_contact_mask;
    uint8_t tap_max_count;
    /* Debounced contact count: changes are adopted only after they hold. */
    uint8_t stable_count;
    uint8_t pending_count;
    uint8_t count_frames;
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

typedef struct {
    bool initialized;
    float x;
    float y;
    float dx;
    float dy;
    uint16_t time;
} axis_filter_t;

static simulated_mouse_state_t m_state = {0};
static axis_filter_t m_pointer = {0};
static axis_filter_t m_scroll = {0};
static float m_pointer_jitter = POINTER_JITTER_MIN;

void ptp_simulated_mouse_reset(void)
{
    m_state = (simulated_mouse_state_t){0};
    m_pointer = (axis_filter_t){0};
    m_scroll = (axis_filter_t){0};
    m_pointer_jitter = POINTER_JITTER_MIN;
}

static uint16_t scan_time_delta(uint16_t now, uint16_t then)
{
    return (uint16_t)(now - then);
}

static float scan_dt_seconds(uint16_t now, uint16_t then)
{
    float dt = (float)scan_time_delta(now, then) * 0.0001f;
    /* Coalesced reads can share a timestamp; clamp to a sane sample period. */
    if (dt < 0.002f) {
        dt = 0.005f;
    }
    if (dt > 0.05f) {
        dt = 0.05f;
    }
    return dt;
}

static float vector_length(float x, float y)
{
    return sqrtf((x * x) + (y * y));
}

static float interpolate_gain(float speed,
                              float min_gain,
                              float max_gain,
                              float accel_start,
                              float accel_end)
{
    if (speed <= accel_start) {
        return min_gain;
    }
    if (speed >= accel_end) {
        return max_gain;
    }

    float ratio = (speed - accel_start) / (accel_end - accel_start);
    return min_gain + ((max_gain - min_gain) * ratio);
}

static float lowpass_alpha(float cutoff_hz, float dt)
{
    float tau = 1.0f / (2.0f * (float)M_PI * cutoff_hz);
    return 1.0f / (1.0f + (tau / dt));
}

/* One-euro step for one axis: the filtered derivative raises the cutoff with
 * speed, giving adaptive smoothing. */
static float one_euro_axis(float *filtered,
                           float *derivative,
                           float sample,
                           float dt,
                           float min_cutoff,
                           float beta,
                           float d_cutoff)
{
    float a_d = lowpass_alpha(d_cutoff, dt);
    float raw_derivative = (sample - *filtered) / dt;
    *derivative += a_d * (raw_derivative - *derivative);

    float cutoff = min_cutoff + (beta * fabsf(*derivative));
    float a = lowpass_alpha(cutoff, dt);
    *filtered += a * (sample - *filtered);
    return *filtered;
}

static int8_t emit_hid_axis(float *remainder, float delta)
{
    *remainder += delta;

    /* Saturate but keep a bounded backlog so fast motion drains smoothly. */
    if (*remainder > HID_AXIS_MAX) {
        if (*remainder > HID_AXIS_MAX + POINTER_REMAINDER_LIMIT) {
            *remainder = HID_AXIS_MAX + POINTER_REMAINDER_LIMIT;
        }
        *remainder -= HID_AXIS_MAX;
        return HID_AXIS_MAX;
    }
    if (*remainder < HID_AXIS_MIN) {
        if (*remainder < HID_AXIS_MIN - POINTER_REMAINDER_LIMIT) {
            *remainder = HID_AXIS_MIN - POINTER_REMAINDER_LIMIT;
        }
        *remainder -= HID_AXIS_MIN;
        return HID_AXIS_MIN;
    }

    int8_t whole = (int8_t)*remainder;
    *remainder -= (float)whole;
    return whole;
}

static bool is_gesture_contact_active(const tp_finger_t *finger)
{
    return finger->tip_switch && finger->confidence;
}

static uint8_t get_active_contact_mask(const tp_multi_msg_t *msg)
{
    uint8_t mask = 0;

    for (int i = 0; i < MAX_TOUCH_CONTACTS; i++) {
        if (is_gesture_contact_active(&msg->fingers[i])) {
            mask |= (uint8_t)(1U << i);
        }
    }

    return mask;
}

static int count_active_contacts(uint8_t mask)
{
    int count = 0;

    while (mask != 0) {
        count += mask & 1U;
        mask >>= 1;
    }

    return count;
}

static int mask_nth_slot(uint8_t mask, int n)
{
    for (int i = 0; i < MAX_TOUCH_CONTACTS; i++) {
        if (mask & (1U << i)) {
            if (n-- == 0) {
                return i;
            }
        }
    }
    return -1;
}

static void reset_move_state(void)
{
    m_state.has_move_anchor = false;
    m_state.pending_move_x = 0.0f;
    m_state.pending_move_y = 0.0f;
    m_state.rem_x = 0.0f;
    m_state.rem_y = 0.0f;
    m_pointer = (axis_filter_t){0};
}

static void reset_scroll_state(void)
{
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
    m_scroll = (axis_filter_t){0};
}

static void tap_anchor_contacts(const tp_multi_msg_t *msg, uint8_t mask)
{
    m_state.tap_contact_mask = mask;
    for (int i = 0; i < MAX_TOUCH_CONTACTS; i++) {
        if (mask & (1U << i)) {
            m_state.tap_start_x[i] = (float)msg->fingers[i].x;
            m_state.tap_start_y[i] = (float)msg->fingers[i].y;
        }
    }
}

static float tap_contact_travel(const tp_multi_msg_t *msg, uint8_t mask)
{
    float max_move = 0.0f;

    for (int i = 0; i < MAX_TOUCH_CONTACTS; i++) {
        if (!(mask & (1U << i))) {
            continue;
        }
        float dx = (float)msg->fingers[i].x - m_state.tap_start_x[i];
        float dy = (float)msg->fingers[i].y - m_state.tap_start_y[i];
        float distance = vector_length(dx, dy);
        if (distance > max_move) {
            max_move = distance;
        }
    }

    return max_move;
}

static void update_tap_state(const tp_multi_msg_t *msg,
                             int active_count,
                             uint8_t active_mask)
{
    if (!m_state.tap_active) {
        m_state.tap_active = true;
        m_state.tap_moved = false;
        m_state.tap_max_move = 0.0f;
        m_state.tap_max_count = (uint8_t)active_count;
        m_state.tap_start_time = msg->scan_time;
        m_state.double_tap_drag_candidate =
            active_count == 1 &&
            m_state.has_last_single_tap &&
            scan_time_delta(msg->scan_time, m_state.last_single_tap_time) <=
                DOUBLE_TAP_MAX_INTERVAL;
        tap_anchor_contacts(msg, active_mask);
        return;
    }

    if (active_count > m_state.tap_max_count) {
        /* The caller only passes a debounced contact count, so a one-frame
         * ghost finger cannot promote the tap to a higher button. */
        m_state.tap_max_count = (uint8_t)active_count;
        m_state.tap_start_time = msg->scan_time;
        m_state.double_tap_drag_candidate = false;
        tap_anchor_contacts(msg, active_mask);
        return;
    }

    /* A slot replacement is not continuous motion: cancel and re-anchor. */
    if (active_mask != m_state.tap_contact_mask) {
        m_state.tap_moved = true;
        m_state.double_tap_drag_candidate = false;
        m_state.tap_start_time = msg->scan_time;
        tap_anchor_contacts(msg, active_mask);
        return;
    }

    float max_move = tap_contact_travel(msg, active_mask);
    if (max_move > m_state.tap_max_move) {
        m_state.tap_max_move = max_move;
    }
    uint16_t elapsed = scan_time_delta(msg->scan_time, m_state.tap_start_time);

    if (m_state.double_tap_drag_candidate &&
        active_count == 1 &&
        (max_move > DOUBLE_TAP_DRAG_MIN_MOVE ||
         elapsed >= DOUBLE_TAP_DRAG_HOLD_TIME)) {
        m_state.drag_active = true;
        m_state.has_last_single_tap = false;
    }
}

static uint8_t tap_button_mask(uint8_t finger_count)
{
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

static void clear_tap_tracking(void)
{
    m_state.tap_active = false;
    m_state.tap_max_count = 0;
    m_state.tap_contact_mask = 0;
    m_state.tap_moved = false;
    m_state.tap_max_move = 0.0f;
    m_state.double_tap_drag_candidate = false;
    m_state.force_click_seen = false;
}

static void handle_tap_release(const tp_multi_msg_t *msg,
                               mouse_hid_report_t *out_report)
{
    if (!m_state.tap_active) {
        m_state.drag_active = false;
        m_state.force_click_seen = false;
        return;
    }

    uint8_t buttons = tap_button_mask(m_state.tap_max_count);
    uint16_t elapsed = scan_time_delta(msg->scan_time, m_state.tap_start_time);
    bool clean = !m_state.tap_moved &&
                 m_state.tap_max_move <= TAP_MAX_MOVE &&
                 elapsed <= TAP_MAX_TIME &&
                 buttons != 0;

    if (m_state.drag_active) {
        m_state.drag_active = false;
        m_state.has_last_single_tap = false;
    } else if (m_state.force_click_seen) {
        /* The physical/force click already emitted its own button report. */
        m_state.has_last_single_tap = false;
    } else if (clean) {
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

/* Shared pointer path: one finger, or the centroid of the two fingers that
 * hold the middle button during a drag. */
static void handle_pointer_move(float curr_x,
                                float curr_y,
                                uint8_t anchor_key,
                                uint16_t scan_time,
                                mouse_hid_report_t *out_report)
{
    if (!m_state.has_move_anchor ||
        m_state.move_contact_index != anchor_key ||
        !m_pointer.initialized) {
        reset_move_state();
        m_pointer = (axis_filter_t){.initialized = true,
                                    .x = curr_x,
                                    .y = curr_y,
                                    .time = scan_time};
        m_state.move_contact_index = anchor_key;
        m_state.has_move_anchor = true;
        return;
    }

    float dt = scan_dt_seconds(scan_time, m_pointer.time);
    m_pointer.time = scan_time;

    float prev_x = m_pointer.x;
    float prev_y = m_pointer.y;

    /* A step this large cannot be real motion mid-touch: re-anchor instead of
     * letting a spike through the filter. */
    if (vector_length(curr_x - prev_x, curr_y - prev_y) > POINTER_JUMP_THRESHOLD) {
        m_pointer.x = curr_x;
        m_pointer.y = curr_y;
        m_pointer.dx = 0.0f;
        m_pointer.dy = 0.0f;
        m_state.pending_move_x = 0.0f;
        m_state.pending_move_y = 0.0f;
        return;
    }

    float filtered_x = one_euro_axis(&m_pointer.x,
                                     &m_pointer.dx,
                                     curr_x,
                                     dt,
                                     POINTER_EURO_MIN_CUTOFF,
                                     POINTER_EURO_BETA,
                                     POINTER_EURO_D_CUTOFF);
    float filtered_y = one_euro_axis(&m_pointer.y,
                                     &m_pointer.dy,
                                     curr_y,
                                     dt,
                                     POINTER_EURO_MIN_CUTOFF,
                                     POINTER_EURO_BETA,
                                     POINTER_EURO_D_CUTOFF);

    float speed = vector_length(m_pointer.dx, m_pointer.dy);

    /* Learn the resting jitter floor; it only ever raises the emit gate, so it
     * suppresses drift without discarding deliberate motion. */
    if (speed < POINTER_STILL_SPEED) {
        float residual = vector_length(curr_x - filtered_x, curr_y - filtered_y);
        m_pointer_jitter += POINTER_JITTER_ALPHA * (residual - m_pointer_jitter);
        if (m_pointer_jitter < POINTER_JITTER_MIN) {
            m_pointer_jitter = POINTER_JITTER_MIN;
        } else if (m_pointer_jitter > POINTER_JITTER_MAX) {
            m_pointer_jitter = POINTER_JITTER_MAX;
        }
    }

    m_state.pending_move_x += filtered_x - prev_x;
    m_state.pending_move_y += filtered_y - prev_y;

    float pending = vector_length(m_state.pending_move_x, m_state.pending_move_y);
    float emit_floor =
        m_pointer_jitter > POINTER_EMIT_FLOOR ? m_pointer_jitter : POINTER_EMIT_FLOOR;
    if (pending < emit_floor) {
        return;
    }

    float gain = interpolate_gain(speed,
                                  POINTER_GAIN_MIN,
                                  POINTER_GAIN_MAX,
                                  POINTER_ACCEL_START,
                                  POINTER_ACCEL_END);
    float move_x = m_state.pending_move_x * gain;
    float move_y = m_state.pending_move_y * gain;
    m_state.pending_move_x = 0.0f;
    m_state.pending_move_y = 0.0f;

    out_report->x = emit_hid_axis(&m_state.rem_x, move_x);
    out_report->y = emit_hid_axis(&m_state.rem_y, move_y);
}

static void handle_single_finger_move(const tp_multi_msg_t *msg,
                                      int finger_index,
                                      mouse_hid_report_t *out_report)
{
    handle_pointer_move((float)msg->fingers[finger_index].x,
                        (float)msg->fingers[finger_index].y,
                        (uint8_t)finger_index,
                        msg->scan_time,
                        out_report);
}

/* Both the scroll and the middle drag reference the two-finger centroid, so
 * switching between them cannot produce a position jump. */
static void contact_centroid(const tp_multi_msg_t *msg,
                             int first_index,
                             int second_index,
                             float *x,
                             float *y)
{
    *x = ((float)msg->fingers[first_index].x +
          (float)msg->fingers[second_index].x) / 2.0f;
    *y = ((float)msg->fingers[first_index].y +
          (float)msg->fingers[second_index].y) / 2.0f;
}

static void handle_dual_finger_scroll(const tp_multi_msg_t *msg,
                                      int first_index,
                                      int second_index,
                                      uint8_t active_mask,
                                      mouse_hid_report_t *out_report)
{
    float avg_x, avg_y;
    contact_centroid(msg, first_index, second_index, &avg_x, &avg_y);

    if (!m_state.has_scroll_anchor ||
        m_state.scroll_contact_mask != active_mask ||
        !m_scroll.initialized) {
        reset_scroll_state();
        m_scroll = (axis_filter_t){.initialized = true,
                                   .x = avg_x,
                                   .y = avg_y,
                                   .time = msg->scan_time};
        m_state.scroll_origin_x = avg_x;
        m_state.scroll_origin_y = avg_y;
        m_state.last_scroll_x = avg_x;
        m_state.last_scroll_y = avg_y;
        m_state.scroll_contact_mask = active_mask;
        m_state.has_scroll_anchor = true;
        return;
    }

    float dt = scan_dt_seconds(msg->scan_time, m_scroll.time);
    m_scroll.time = msg->scan_time;

    /* Compare raw sample to raw sample. Comparing raw to the filtered position
     * would trip on the filter's own lag and stall accelerating scrolls. On a
     * real jump, re-anchor instead of skipping so the wheel never freezes. */
    float raw_step = vector_length(avg_x - m_state.last_scroll_x,
                                   avg_y - m_state.last_scroll_y);
    m_state.last_scroll_x = avg_x;
    m_state.last_scroll_y = avg_y;
    if (raw_step > SCROLL_JUMP_THRESHOLD) {
        m_scroll.x = avg_x;
        m_scroll.y = avg_y;
        m_scroll.dx = 0.0f;
        m_scroll.dy = 0.0f;
        if (!m_state.scroll_active) {
            m_state.scroll_origin_x = avg_x;
            m_state.scroll_origin_y = avg_y;
        }
        return;
    }

    float prev_x = m_scroll.x;
    float prev_y = m_scroll.y;
    float filtered_x = one_euro_axis(&m_scroll.x,
                                     &m_scroll.dx,
                                     avg_x,
                                     dt,
                                     SCROLL_EURO_MIN_CUTOFF,
                                     SCROLL_EURO_BETA,
                                     POINTER_EURO_D_CUTOFF);
    float filtered_y = one_euro_axis(&m_scroll.y,
                                     &m_scroll.dy,
                                     avg_y,
                                     dt,
                                     SCROLL_EURO_MIN_CUTOFF,
                                     SCROLL_EURO_BETA,
                                     POINTER_EURO_D_CUTOFF);

    if (!m_state.scroll_active) {
        float total_x = filtered_x - m_state.scroll_origin_x;
        float total_y = filtered_y - m_state.scroll_origin_y;

        if (vector_length(total_x, total_y) < SCROLL_START_THRESHOLD) {
            return;
        }

        float abs_x = fabsf(total_x);
        float abs_y = fabsf(total_y);
        /* Require a clearly dominant axis; an ambiguous diagonal keeps waiting
         * instead of locking the wrong direction. */
        if (abs_x >= abs_y * SCROLL_AXIS_MARGIN) {
            m_state.scroll_axis = SCROLL_AXIS_HORIZONTAL;
        } else if (abs_y >= abs_x * SCROLL_AXIS_MARGIN) {
            m_state.scroll_axis = SCROLL_AXIS_VERTICAL;
        } else {
            return;
        }

        m_state.scroll_active = true;
        m_state.tap_moved = true;

        /* Discard activation travel to avoid an initial wheel/pan burst. */
        m_state.rem_pan = 0.0f;
        m_state.rem_wheel = 0.0f;
        return;
    }

    float step_x = filtered_x - prev_x;
    float step_y = filtered_y - prev_y;
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

bool ptp_simulated_mouse_click_needs_release(void)
{
    bool needs_release = m_state.click_release_pending;
    m_state.click_release_pending = false;
    return needs_release;
}

static void parse_simulated_mouse_buttons(const tp_multi_msg_t *msg,
                                          mouse_hid_report_t *out_report)
{
    out_report->buttons = msg->button_mask & 0x07;
    out_report->x = 0;
    out_report->y = 0;
    out_report->wheel = 0;
    out_report->pan = 0;

    uint8_t raw_mask = get_active_contact_mask(msg);
    int raw_count = count_active_contacts(raw_mask);

    if (out_report->buttons != 0) {
        m_state.force_click_seen = true;
    }

    if (m_state.suppress_tap_until_release) {
        reset_move_state();
        reset_scroll_state();
        if (raw_count == 0) {
            m_state.suppress_tap_until_release = false;
            clear_tap_tracking();
        }
        return;
    }

    /* Debounce the contact count so a one-frame confidence drop or a ghost
     * finger neither tears down an in-flight scroll nor changes the tap's
     * button. A real lift to zero is always immediate. */
    if ((uint8_t)raw_count != m_state.stable_count) {
        if (raw_count == 0) {
            m_state.stable_count = 0;
            m_state.count_frames = 0;
        } else {
            if ((uint8_t)raw_count == m_state.pending_count) {
                if (m_state.count_frames < CONTACT_COUNT_CONFIRM_FRAMES) {
                    m_state.count_frames++;
                }
            } else {
                m_state.pending_count = (uint8_t)raw_count;
                m_state.count_frames = 1;
            }
            if (m_state.count_frames >= CONTACT_COUNT_CONFIRM_FRAMES) {
                m_state.stable_count = (uint8_t)raw_count;
                m_state.count_frames = 0;
            } else {
                /* Transient change: hold every gesture state, resume next frame. */
                return;
            }
        }
    } else {
        m_state.count_frames = 0;
    }

    uint8_t active_mask = raw_mask;
    int active_count = raw_count;
    int first_active = mask_nth_slot(active_mask, 0);
    int second_active = mask_nth_slot(active_mask, 1);

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
        update_tap_state(msg, active_count, active_mask);
    }

    if (active_count == 1) {
        reset_scroll_state();
        handle_single_finger_move(msg, first_active, out_report);
        if (m_state.drag_active && out_report->buttons == 0) {
            out_report->buttons = 0x01;
        }
    } else if (active_count == 2) {
        if (out_report->buttons != 0) {
            /* A held two-finger force press is a middle-button drag: keep
             * emitting motion from the centroid instead of scrolling. */
            reset_scroll_state();
            float centroid_x, centroid_y;
            contact_centroid(msg, first_active, second_active, &centroid_x, &centroid_y);
            handle_pointer_move(centroid_x,
                                centroid_y,
                                MOVE_ANCHOR_CENTROID,
                                msg->scan_time,
                                out_report);
        } else {
            reset_move_state();
            handle_dual_finger_scroll(msg,
                                      first_active,
                                      second_active,
                                      active_mask,
                                      out_report);
        }
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

void parse_ptp_simulated_mouse_report(const tp_multi_msg_t *msg, mouse_hid_report_t *out_report)
{
    parse_simulated_mouse_buttons(msg, out_report);
    /* The parser admits haptics against the raw frame's source generation. */
}

#endif
