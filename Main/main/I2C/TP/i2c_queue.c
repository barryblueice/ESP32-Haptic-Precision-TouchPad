#include "I2C/TP/i2c_hid.h"
#include "I2C/TP/tp_coordinates.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "SYS/rtos_queue.h"
#include "SYS/input_pipeline.h"
#include "SYS/device_config.h"
#include "SYS/edge_gesture.h"
#include "SYS/point_gesture.h"
#include "USB/usb_aux.h"
#include "SYS/hid_msg.h"

#include "I2C/I2C_handle.h"
#include "GPIO/GPIO_handle.h"

#define TAG "I2C_QUEUE"

#define HISTORY_LEN 3

#define TAP_DEADZONE 30
#define FILTER_ALPHA 0.5f

#define PALM_MAJOR_LIMIT  0x10
#define PALM_MINOR_LIMIT  0x10
#define PALM_RATIO_LIMIT  2.0f

#define SCAN_INTERVAL_PER_FINGER 80
#define FORCE_CLICK_PRESS_STABLE_FRAMES 2
#define FORCE_CLICK_RELEASE_STABLE_FRAMES 2
#define FORCE_CLICK_MOVE_DEADZONE 45

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
static uint16_t last_raw_x[5] = {0};
static uint16_t last_raw_y[5] = {0};
static uint16_t origin_x[5] = {0};
static uint16_t origin_y[5] = {0};

static int64_t last_frame_time = 0;
static uint32_t simulated_scan_time = 0;

static uint16_t get_median(uint16_t n1, uint16_t n2, uint16_t n3) {
    if ((n1 > n2) ^ (n1 > n3)) return n1;
    else if ((n2 > n1) ^ (n2 > n3)) return n2;
    else return n3;
}

static int32_t slot_filter_x[5] = {0};
static int32_t slot_filter_y[5] = {0};
static uint16_t history_x[5] = {0};
static uint16_t history_y[5] = {0};
static uint16_t last_confidence[5] = {0};

static uint8_t consecutive_errors[5] = {0};

static bool slot_active[5] = {false};
static edge_gesture_t edge_state;
static point_gesture_t point_state;
static esp_timer_handle_t point_timer;
static void point_timer_wake(void *arg) { (void)arg; input_wake_parser(); }
static void point_schedule(void)
{
    esp_timer_stop(point_timer);
    if (point_gesture_repeating(&point_state)) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        int32_t delay = (int32_t)(point_state.repeat_at - now);
        esp_timer_start_once(point_timer, (uint64_t)(delay > 0 ? delay : 1) * 1000);
    }
}

typedef struct {
    bool tracking_contact;
    bool button_down;
    bool click_anchor_valid;
    bool click_drag_unlocked;
    bool last_position_valid;
    uint8_t tracked_contact_id;
    uint8_t filtered_z;
    uint16_t click_anchor_x;
    uint16_t click_anchor_y;
    uint16_t last_position_x;
    uint16_t last_position_y;
    uint8_t press_stable_frames;
    uint8_t release_stable_frames;
} ptp_force_click_state_t;

static ptp_force_click_state_t ptp_force_click_state = {0};

/* Owned by the parser; supply selection is independent of the host transport. */
static bool pressure_vbus_high, pressure_vbus_candidate;
static uint32_t pressure_vbus_sample_at, pressure_vbus_candidate_at;

static uint8_t ptp_map_button_press_threshold(uint8_t threshold_level) {
    if (!pressure_vbus_high) {
        device_config_t config; device_config_get(&config);
        unsigned index = threshold_level == 1 ? 0 : threshold_level == 3 ? 2 : 1;
        return config.bytes[CFG_WIRELESS_LIGHT + index];
    }
    switch (threshold_level) {
        case 1:
            return click_light_weight_threshold;

        case 3:
            return click_strong_weight_threshold;

        case 2:
        default:
            return click_midium_weight_threshold;
    }
}

static void pressure_vbus_log(void)
{
    ESP_LOGI(TAG, "VBUS=%u pressure_group=%s level=%u threshold=%u",
             pressure_vbus_high, pressure_vbus_high ? "powered" : "battery",
             ptp_button_press_threshold, ptp_map_button_press_threshold(ptp_button_press_threshold));
}

static void pressure_vbus_init(void)
{
    pressure_vbus_high = pressure_vbus_candidate = gpio_get_level(VBUS_DET_GPIO) != 0;
    pressure_vbus_sample_at = pressure_vbus_candidate_at = (uint32_t)(esp_timer_get_time() / 1000);
    pressure_vbus_log();
}

static void pressure_vbus_poll(void)
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if ((uint32_t)(now - pressure_vbus_sample_at) < 10) return;
    pressure_vbus_sample_at = now;
    bool high = gpio_get_level(VBUS_DET_GPIO) != 0;
    if (high != pressure_vbus_candidate) {
        pressure_vbus_candidate = high;
        pressure_vbus_candidate_at = now;
    }
    if (high != pressure_vbus_high && (uint32_t)(now - pressure_vbus_candidate_at) >= 30) {
        pressure_vbus_high = high;
        input_source_recover("vbus");
        pressure_vbus_log();
    }
}

static void ptp_reset_force_click(tp_multi_msg_t *msg) {
    ptp_force_click_state.tracking_contact = false;
    ptp_force_click_state.button_down = false;
    ptp_force_click_state.click_anchor_valid = false;
    ptp_force_click_state.click_drag_unlocked = false;
    ptp_force_click_state.last_position_valid = false;
    ptp_force_click_state.tracked_contact_id = 0;
    ptp_force_click_state.filtered_z = 0;
    ptp_force_click_state.click_anchor_x = 0;
    ptp_force_click_state.click_anchor_y = 0;
    ptp_force_click_state.last_position_x = 0;
    ptp_force_click_state.last_position_y = 0;
    ptp_force_click_state.press_stable_frames = 0;
    ptp_force_click_state.release_stable_frames = 0;
    msg->button_mask = 0;
}

static void ptp_apply_force_click_deadzone(tp_multi_msg_t *msg, int tracked_index) {
    uint16_t current_x = msg->fingers[tracked_index].x;
    uint16_t current_y = msg->fingers[tracked_index].y;

    if (!ptp_force_click_state.click_anchor_valid) {
        ptp_force_click_state.click_anchor_valid = true;
        ptp_force_click_state.click_drag_unlocked = false;
        ptp_force_click_state.click_anchor_x = current_x;
        ptp_force_click_state.click_anchor_y = current_y;
    }

    int dx = abs((int)current_x - (int)ptp_force_click_state.click_anchor_x);
    int dy = abs((int)current_y - (int)ptp_force_click_state.click_anchor_y);

    if (!ptp_force_click_state.click_drag_unlocked &&
        dx <= FORCE_CLICK_MOVE_DEADZONE &&
        dy <= FORCE_CLICK_MOVE_DEADZONE) {
        msg->fingers[tracked_index].x = ptp_force_click_state.click_anchor_x;
        msg->fingers[tracked_index].y = ptp_force_click_state.click_anchor_y;
        return;
    }

    ptp_force_click_state.click_drag_unlocked = true;
}

static void ptp_update_force_click_button(tp_multi_msg_t *msg, int active_finger_count) {
    int tracked_index = -1;
    // uint8_t tracked_confidence = 0;
    static uint32_t debug_counter = 0;
    bool should_log_sample = false;

    msg->button_mask = 0;
    debug_counter++;
    should_log_sample = (debug_counter % 64U) == 0U;

    if ((active_finger_count != 1) ||
        ((current_tp_mode != PTP_MODE)
#if CONFIG_PTP_SIMULATED_MOUSE_MODE
         && (current_tp_mode != MOUSE_MODE)
#endif
        )) {
        if (should_log_sample) {
            // ESP_LOGI(TAG,
            //          "PTP force click bypass: mode=%u active_fingers=%d threshold_level=%u",
            //          current_tp_mode,
            //          active_finger_count,
            //          ptp_button_press_threshold);
        }
        ptp_reset_force_click(msg);
        return;
    }

    for (int id = 0; id < 5; id++) {
        if (msg->fingers[id].tip_switch != 0) {
            tracked_index = id;
            // tracked_confidence = msg->fingers[id].confidence;
            break;
        }
    }

    if (tracked_index < 0) {
        // if (should_log_sample) {
        //     ESP_LOGI(TAG, "PTP force click bypass: no active tip slot");
        // }
        ptp_reset_force_click(msg);
        return;
    }

    uint8_t raw_z = msg->fingers[tracked_index].pressure_z;
    uint8_t press_threshold = ptp_map_button_press_threshold(ptp_button_press_threshold);
    uint8_t release_threshold = (press_threshold > 12) ? (press_threshold - 12) : press_threshold;
    uint8_t effective_z;
    uint16_t current_x = msg->fingers[tracked_index].x;
    uint16_t current_y = msg->fingers[tracked_index].y;

    if (!ptp_force_click_state.tracking_contact ||
        (ptp_force_click_state.tracked_contact_id != (uint8_t)tracked_index)) {
        ptp_force_click_state.tracking_contact = true;
        ptp_force_click_state.button_down = false;
        ptp_force_click_state.tracked_contact_id = (uint8_t)tracked_index;
        ptp_force_click_state.filtered_z = raw_z;
        ptp_force_click_state.click_anchor_valid = false;
        ptp_force_click_state.click_drag_unlocked = false;
        ptp_force_click_state.click_anchor_x = 0;
        ptp_force_click_state.click_anchor_y = 0;
        ptp_force_click_state.last_position_valid = true;
        ptp_force_click_state.last_position_x = current_x;
        ptp_force_click_state.last_position_y = current_y;
        ptp_force_click_state.press_stable_frames = 0;
        ptp_force_click_state.release_stable_frames = 0;
        msg->button_mask = 0;
        // ESP_LOGI(TAG,
        //          "PTP force click tracking: id=%d raw_z=%u press=%u release=%u conf=%u level=%u",
        //          tracked_index,
        //          raw_z,
        //          press_threshold,
        //          release_threshold,
        //          tracked_confidence,
        //          ptp_button_press_threshold);
        return;
    }

    ptp_force_click_state.filtered_z = (uint8_t)(((uint16_t)(ptp_force_click_state.filtered_z * 3U) + raw_z + 2U) / 4U);
    effective_z = (raw_z > ptp_force_click_state.filtered_z) ? raw_z : ptp_force_click_state.filtered_z;

    // if (should_log_sample) {
    //     ESP_LOGI(TAG,
    //              "PTP force click sample: id=%d raw_z=%u filtered_z=%u effective_z=%u press=%u release=%u conf=%u down=%u",
    //              tracked_index,
    //              raw_z,
    //              ptp_force_click_state.filtered_z,
    //              effective_z,
    //              press_threshold,
    //              release_threshold,
    //              tracked_confidence,
    //              ptp_force_click_state.button_down ? 1U : 0U);
    // }

    if (!ptp_force_click_state.button_down) {
        if (effective_z >= press_threshold) {
            if (ptp_force_click_state.press_stable_frames < FORCE_CLICK_PRESS_STABLE_FRAMES) {
                ptp_force_click_state.press_stable_frames++;
            }
            if (ptp_force_click_state.press_stable_frames >= FORCE_CLICK_PRESS_STABLE_FRAMES) {
                ptp_force_click_state.button_down = true;
                ptp_force_click_state.click_anchor_valid = true;
                ptp_force_click_state.click_drag_unlocked = false;
                ptp_force_click_state.click_anchor_x = ptp_force_click_state.last_position_valid ?
                    ptp_force_click_state.last_position_x : current_x;
                ptp_force_click_state.click_anchor_y = ptp_force_click_state.last_position_valid ?
                    ptp_force_click_state.last_position_y : current_y;
                ptp_force_click_state.release_stable_frames = 0;
                // ESP_LOGI(TAG,
                //          "PTP force click down: id=%d raw_z=%u filtered_z=%u press=%u release=%u conf=%u level=%u",
                //          tracked_index,
                //          raw_z,
                //          ptp_force_click_state.filtered_z,
                //          press_threshold,
                //          release_threshold,
                //          tracked_confidence,
                //          ptp_button_press_threshold);
            }
        } else {
            ptp_force_click_state.press_stable_frames = 0;
        }
    } else {
        if (ptp_force_click_state.filtered_z <= release_threshold) {
            if (ptp_force_click_state.release_stable_frames < FORCE_CLICK_RELEASE_STABLE_FRAMES) {
                ptp_force_click_state.release_stable_frames++;
            }
            if (ptp_force_click_state.release_stable_frames >= FORCE_CLICK_RELEASE_STABLE_FRAMES) {
                ptp_force_click_state.button_down = false;
                ptp_force_click_state.click_anchor_valid = false;
                ptp_force_click_state.click_drag_unlocked = false;
                ptp_force_click_state.press_stable_frames = 0;
                // ESP_LOGI(TAG,
                //          "PTP force click up: id=%d raw_z=%u filtered_z=%u press=%u release=%u conf=%u level=%u",
                //          tracked_index,
                //          raw_z,
                //          ptp_force_click_state.filtered_z,
                //          press_threshold,
                //          release_threshold,
                //          tracked_confidence,
                //          ptp_button_press_threshold);
            }
        } else {
            ptp_force_click_state.release_stable_frames = 0;
        }
    }

    if (ptp_force_click_state.button_down) {
        ptp_apply_force_click_deadzone(msg, tracked_index);

        if (current_tp_mode == PTP_MODE) {
            msg->button_mask = 0x01;
        } else {
            msg->button_mask = (msg->fingers[tracked_index].x < device_config_x_max() / 2) ? 0x01 : 0x02;
        }
    } else {
        msg->button_mask = 0x00;
        if (ptp_force_click_state.press_stable_frames == 0) {
            ptp_force_click_state.last_position_valid = true;
            ptp_force_click_state.last_position_x = current_x;
            ptp_force_click_state.last_position_y = current_y;
        }
    }


}

void update_simulated_scan_time(tp_multi_msg_t *msg) {
    int64_t now = esp_timer_get_time();

    if (last_frame_time != 0) {
        uint32_t delta = (uint32_t)((now - last_frame_time) / 100);

        simulated_scan_time += delta;
    }

    msg->scan_time = (uint16_t)(simulated_scan_time & 0xFFFF);

    last_frame_time = now;
}

static void reset_input_state(void)
{
    edge_gesture_reset(&edge_state);
    point_gesture_reset(&point_state);
    if (point_timer) esp_timer_stop(point_timer);
    usb_aux_cancel();
    ptp_report_reset();
    ptp_force_click_state = (ptp_force_click_state_t){0};
    memset(touch_state, 0, sizeof(touch_state));
    memset(tap_frozen, 0, sizeof(tap_frozen));
    memset(raw_x_history, 0, sizeof(raw_x_history));
    memset(raw_y_history, 0, sizeof(raw_y_history));
    memset(last_raw_x, 0, sizeof(last_raw_x));
    memset(last_raw_y, 0, sizeof(last_raw_y));
    memset(origin_x, 0, sizeof(origin_x));
    memset(origin_y, 0, sizeof(origin_y));
    memset(slot_filter_x, 0, sizeof(slot_filter_x));
    memset(slot_filter_y, 0, sizeof(slot_filter_y));
    memset(history_x, 0, sizeof(history_x));
    memset(history_y, 0, sizeof(history_y));
    memset(last_confidence, 0, sizeof(last_confidence));
    memset(consecutive_errors, 0, sizeof(consecutive_errors));
    memset(slot_active, 0, sizeof(slot_active));
    last_frame_time = 0;
#if CONFIG_PTP_SIMULATED_MOUSE_MODE
    ptp_simulated_mouse_reset();
#endif
}

void i2c_queue_task(void *arg) {

    input_register_parser();
    pressure_vbus_init();
    const TickType_t poll_ticks = pdMS_TO_TICKS(10) ? pdMS_TO_TICKS(10) : 1;
    const esp_timer_create_args_t point_timer_args = {.callback = point_timer_wake, .name = "point_wheel"};
    ESP_ERROR_CHECK(esp_timer_create(&point_timer_args, &point_timer));
    input_frame_t frame;
    uint32_t generation = input_source_generation();
    uint32_t output_generation = input_generation();
    bool last_all_up = true;
    int previous_format = -1;
    uint8_t previous_mode = current_tp_mode;

    while (1) {

        tp_multi_msg_t tp_msg = {0};
        mouse_msg_t mouse_msg = {0};

        if (device_config_parser_boundary()) {
            reset_input_state();
            while (input_next_frame(&frame)) { }
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        pressure_vbus_poll();
        if (input_apply_mode_request()) continue;
        if (generation != input_source_generation()) {
            generation = input_source_generation();
            reset_input_state();
            previous_format = -1;
            previous_mode = current_tp_mode;
        }
        if (output_generation != input_generation()) {
            output_generation = input_generation();
            /* Preserve local contact/force/region ownership across radio recovery. */
            usb_aux_cancel();
            if (last_all_up) {
                ptp_report_reset();
#if CONFIG_PTP_SIMULATED_MOUSE_MODE
                ptp_simulated_mouse_reset();
#endif
            }
        }
        if (!input_next_frame(&frame)) {
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
            point_result_t repeat = point_gesture_tick(&point_state, now);
            if (repeat.steps && input_output_ready(output_generation) &&
                !aux_output_repeat(repeat.action, repeat.steps, output_generation, now)) input_recover();
            point_schedule();
            ulTaskNotifyTake(pdTRUE, poll_ticks);
            continue;
        }
        /* A reset may have raced with dequeuing the next captured frame. */
        if (generation != frame.generation) {
            generation = frame.generation;
            reset_input_state();
            previous_format = -1;
            previous_mode = current_tp_mode;
        }
        if ((uint32_t)(esp_timer_get_time() / 1000) - frame.time_ms > REPORT_MAX_AGE_MS) {
            input_source_recover("raw_age");
            continue;
        }
        uint8_t *tp_packet = frame.bytes;
        bool all_up = (tp_packet[3] & 7U) == 0;
        if (tp_packet[0] == 0x40) {
            all_up = true;
            for (unsigned i = 0; i < 5; ++i) if (tp_packet[4 + i * 8] & 1U) all_up = false;
        }
        uint32_t report_generation = frame.output_generation;
        if (frame.generation != input_source_generation()) continue;
        bool local_ready = input_source_observe(frame.generation, all_up);
        bool publish = input_observe(report_generation, all_up);
        last_all_up = all_up;
        if (!local_ready || (!publish && current_mode != _2_4_MODE)) continue;
        {

            // printf("Raw Data: ");
            // for(int i=0; i<64; i++) printf("%02x ", tp_packet[i]);
            // printf("\n");

            int format = tp_packet[0] == 0x40;
            if ((previous_format != -1 && previous_format != format) || previous_mode != current_tp_mode) {
                input_source_recover("format");
                continue;
            }
            previous_format = format;
            previous_mode = current_tp_mode;
            if (tp_packet[0] == 0x40) {
                int active_finger_count = 0;
                bool published_pair = false;

                update_simulated_scan_time(&tp_msg);

                for (int id = 0; id < 5; id++) {
                    int offset = 4 + (id * 8);
                    uint8_t finger_status = tp_packet[offset];

                    uint16_t rx, ry;
                    uint16_t raw_x = tp_packet[offset + 1] | (tp_packet[offset + 2] << 8);
                    uint16_t raw_y = tp_packet[offset + 3] | (tp_packet[offset + 4] << 8);
                    tp_rotate_coordinates(raw_x, raw_y, &rx, &ry);
                    uint8_t pressure_z = tp_packet[offset + 5];
                    uint8_t major = tp_packet[offset + 6];
                    uint8_t minor = tp_packet[offset + 7];

                    tp_msg.fingers[id].contact_id = id;

                    tp_msg.fingers[id].tip_switch = finger_status & 0x01;

                    if (finger_status & 0x01) {

                        bool is_confident = raw_x <= 2302 && raw_y <= 1532;

                        if (major > PALM_MAJOR_LIMIT || minor > PALM_MINOR_LIMIT) {
                            is_confident = false;
                        }

                        float ratio = (minor > 0) ? (float)major / minor : 0;
                        if (ratio > PALM_RATIO_LIMIT) {
                            is_confident = false;
                        }

                        tp_msg.fingers[id].confidence = is_confident ? 0x01 : 0x00;
                        last_confidence[id] = tp_msg.fingers[id].confidence;

                        for (int h = 0; h < HISTORY_LEN - 1; h++) {
                            raw_x_history[id][h] = raw_x_history[id][h+1];
                            raw_y_history[id][h] = raw_y_history[id][h+1];
                        }
                        raw_x_history[id][HISTORY_LEN-1] = rx;
                        raw_y_history[id][HISTORY_LEN-1] = ry;

                        if (!slot_active[id]) {
                            slot_filter_x[id] = rx << 8;
                            slot_filter_y[id] = ry << 8;
                            origin_x[id] = rx;
                            origin_y[id] = ry;
                            tap_frozen[id] = true;
                            touch_state[id] = TOUCH_TAP_CANDIDATE;
                            slot_active[id] = true;
                            consecutive_errors[id] = 0;

                            for(int h=0; h<HISTORY_LEN; h++) {
                                raw_x_history[id][h] = rx; raw_y_history[id][h] = ry;
                            }
                        }

                        uint16_t mx = get_median(raw_x_history[id][HISTORY_LEN-3],
                                                raw_x_history[id][HISTORY_LEN-2],
                                                raw_x_history[id][HISTORY_LEN-1]);
                        uint16_t my = get_median(raw_y_history[id][HISTORY_LEN-3],
                                                raw_y_history[id][HISTORY_LEN-2],
                                                raw_y_history[id][HISTORY_LEN-1]);

                        int dx_jump = mx - (slot_filter_x[id] >> 8);
                        int dy_jump = my - (slot_filter_y[id] >> 8);
                        if ((dx_jump*dx_jump + dy_jump*dy_jump) > (300*300)) {
                            if (consecutive_errors[id] < 2) {
                                mx = (uint16_t)(slot_filter_x[id] >> 8);
                                my = (uint16_t)(slot_filter_y[id] >> 8);
                                consecutive_errors[id]++;
                            } else {
                                consecutive_errors[id] = 0;
                            }
                        } else {
                            consecutive_errors[id] = 0;
                        }

                        int alpha_speed = abs(rx - (int)last_raw_x[id]) + abs(ry - (int)last_raw_y[id]);
                        uint32_t dynamic_alpha = (alpha_speed < 3) ? 64 : (alpha_speed < 12 ? 115 : 218);

                        slot_filter_x[id] = (dynamic_alpha * (mx << 8) + (256 - dynamic_alpha) * slot_filter_x[id]) >> 8;
                        slot_filter_y[id] = (dynamic_alpha * (my << 8) + (256 - dynamic_alpha) * slot_filter_y[id]) >> 8;

                        uint16_t fx = (uint16_t)(slot_filter_x[id] >> 8);
                        uint16_t fy = (uint16_t)(slot_filter_y[id] >> 8);

                        int dx_from_origin = abs((int)rx - (int)origin_x[id]);
                        int dy_from_origin = abs((int)ry - (int)origin_y[id]);

                        int active_dz = (active_finger_count > 0) ? (TAP_DEADZONE / 2) : TAP_DEADZONE;

                        if (dx_from_origin > active_dz || dy_from_origin > active_dz) {
                            tap_frozen[id] = false;
                            if (touch_state[id] == TOUCH_TAP_CANDIDATE) touch_state[id] = TOUCH_DRAG;
                        }

                        if (tap_frozen[id]) {
                            tp_msg.fingers[id].x = origin_x[id];
                            tp_msg.fingers[id].y = origin_y[id];
                        } else {
                            tp_msg.fingers[id].x = fx;
                            tp_msg.fingers[id].y = fy;
                        }

                        history_x[id] = tp_msg.fingers[id].x;
                        history_y[id] = tp_msg.fingers[id].y;
                        last_raw_x[id] = rx;
                        last_raw_y[id] = ry;

                        tp_msg.fingers[id].pressure_z = pressure_z;

                        active_finger_count++;

                    } else {
                        tp_msg.fingers[id].x = history_x[id];
                        tp_msg.fingers[id].y = history_y[id];
                        tp_msg.fingers[id].confidence = last_confidence[id];

                        slot_active[id] = false;
                        last_raw_x[id] = 0; last_raw_y[id] = 0;
                        origin_x[id] = 0; origin_y[id] = 0;
                        slot_filter_x[id] = 0; slot_filter_y[id] = 0;
                        touch_state[id] = TOUCH_NONE;
                        tap_frozen[id] = false;
                        consecutive_errors[id] = 0;
                        for(int h=0; h<HISTORY_LEN; h++) {
                            raw_x_history[id][h] = 0; raw_y_history[id][h] = 0;
                        }
                    }

                    // watchdog_id = 0;
                    // watchdog_x = tp_msg.fingers[watchdog_id].x;
                    // watchdog_y = tp_msg.fingers[watchdog_id].y;
                    // watchdog_tip_switch = tp_msg.fingers[watchdog_id].tip_switch;
                    // global_scan_time = tp_msg.scan_time;

                }

                // if (watchdog_tip_switch == 0x01) {
                //     esp_timer_start_once(timeout_watchdog_timer, WATCHDOG_TIMEOUT_US);
                // }

                if (current_tp_mode == PTP_MODE || current_mode == WIRED_MODE) {
                    device_config_t config; device_config_get(&config);
                    tp_multi_msg_t logical = tp_msg;
                    for (unsigned id = 0; id < 5; ++id) if (logical.fingers[id].tip_switch) {
                        logical.fingers[id].x = last_raw_x[id]; logical.fingers[id].y = last_raw_y[id];
                    }
                    point_result_t point = {0};
                    bool owned = point_state.owned;
                    if (current_tp_mode == PTP_MODE) {
                        bool portrait = device_config_rotation() & 1;
                        point = point_gesture_update(&point_state, &config, &logical,
                            device_config_x_max(), device_config_y_max(),
                            portrait ? 766 : 1149, portrait ? 1149 : 766, frame.time_ms);
                        owned |= point_state.owned;
                        if (point.cancel) aux_output_cancel_gesture();
                        if (point.handoff) edge_gesture_reset(&edge_state);
                        if (publish && point.steps && !(point.hold ?
                            aux_output_hold(point.action, point.steps, report_generation, frame.time_ms) : point.initial ?
                            aux_output_once(point.action, point.steps, report_generation, frame.time_ms) :
                            aux_output_repeat(point.action, point.steps, report_generation, frame.time_ms))) {
                            input_recover(); publish = false;
                        }
                        point_schedule();
                    }
                    edge_result_t edge = {0};
                    if (!point.suppress) edge = edge_gesture_update(&edge_state, &config, &logical,
                        device_config_x_max(), device_config_y_max());
                    edge.suppress |= point.suppress;
                    if (owned) edge.tap = false;
                    if (edge.cancelled) aux_output_cancel_gesture();
                    if (publish && edge.steps && !usb_aux_steps(edge.action, edge.steps, report_generation, frame.time_ms)) {
                        input_recover(); publish = false;
                    }
                    if (edge.tap) {
                        input_report_t down = {.mode = input_mode(), .time_ms = frame.time_ms};
                        input_report_t up = down;
                        if (down.mode == PTP_MODE) {
                            if (publish) {
                                parse_ptp_report(&edge.tap_down, &down.data.ptp);
                                parse_ptp_report(&tp_msg, &up.data.ptp);
                                input_publish_pair(report_generation, &down, &up);
                                published_pair = true;
                            }
                        } else {
#if CONFIG_PTP_SIMULATED_MOUSE_MODE
                            edge.tap_down.actual_count = 1;
                            parse_ptp_simulated_mouse_report(&edge.tap_down, &down.data.mouse);
                            input_source_button(frame.generation, down.data.mouse.buttons != 0);
#endif
                        }
                    }
                    if (edge.suppress) {
                        ptp_reset_force_click(&tp_msg);
                        memset(tp_msg.fingers, 0, sizeof(tp_msg.fingers)); active_finger_count = 0;
                    }
                }
                if (point_state.owned) ptp_reset_force_click(&tp_msg);
                else ptp_update_force_click_button(&tp_msg, active_finger_count);
                tp_msg.actual_count = active_finger_count > 0 ? active_finger_count : 1;
                input_report_t report = {.mode = input_mode(), .time_ms = frame.time_ms};
                bool tap = false;
                if (report.mode == MOUSE_MODE) {
#if CONFIG_PTP_SIMULATED_MOUSE_MODE
                    parse_ptp_simulated_mouse_report(&tp_msg, &report.data.mouse);
                    input_source_button(frame.generation, report.data.mouse.buttons != 0);
                    tap = ptp_simulated_mouse_click_needs_release();
                    if (tap) input_source_button(frame.generation, false);
#else
                    continue;
#endif
                } else {
                    input_source_button(frame.generation, tp_msg.button_mask != 0);
                    parse_ptp_report(&tp_msg, &report.data.ptp);
                }
                if (publish && !published_pair) input_publish(report_generation, &report, tap);
                if (!publish && all_up) {
                    /* Offline/recovery taps cannot seed the next host double-tap drag. */
                    ptp_report_reset();
#if CONFIG_PTP_SIMULATED_MOUSE_MODE
                    ptp_simulated_mouse_reset();
#endif
                }

            } else {
                int dx = (int8_t)tp_packet[4], dy = (int8_t)tp_packet[5], mx, my;
                switch (device_config_rotation()) {
                case 1: mx = dy; my = -dx; break;
                case 2: mx = -dx; my = -dy; break;
                case 3: mx = -dy; my = dx; break;
                default: mx = dx; my = dy; break;
                }
                mouse_msg.x = mx < -127 ? -127 : mx > 127 ? 127 : mx;
                mouse_msg.y = my < -127 ? -127 : my > 127 ? 127 : my;
                mouse_msg.buttons = tp_packet[3];
                input_source_button(frame.generation, (mouse_msg.buttons & 0x03U) != 0);

                input_report_t report = {.mode = MOUSE_MODE, .time_ms = frame.time_ms};
                parse_mouse_report(&mouse_msg, &report.data.mouse);
                if (publish) input_publish(report_generation, &report, false);
            }
        }
    }
}
