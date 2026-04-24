#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "I2C/TP/i2c_hid.h"

#include "SYS/hid_msg.h"

#define TAG "WATCHDOG"
#define TOUCH_TIMEOUT_US (100 * 1000)

esp_timer_handle_t timeout_watchdog_timer;

uint16_t watchdog_x = 0;
uint16_t watchdog_y = 0;
uint16_t watchdog_id = 0;
uint16_t watchdog_tip_switch = 0;

void watchdog_refresh(bool touch_active) {
    global_watchdog_start = touch_active && (current_tp_mode == PTP_MODE);

    if (timeout_watchdog_timer == NULL) {
        return;
    }

    if (esp_timer_is_active(timeout_watchdog_timer)) {
        esp_err_t err = esp_timer_stop(timeout_watchdog_timer);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop watchdog timer: %s", esp_err_to_name(err));
        }
    }

    if (global_watchdog_start) {
        esp_err_t err = esp_timer_start_once(timeout_watchdog_timer, TOUCH_TIMEOUT_US);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to start watchdog timer: %s", esp_err_to_name(err));
        }
    }
}

void watchdog_timeout_callback(void* arg) {

    if (current_tp_mode == PTP_MODE && global_watchdog_start) {

        ESP_LOGI(TAG, "Watchdog Triggered!");

        global_watchdog_start = false;
        watchdog_tip_switch = 0;

        tp_multi_msg_t release_msg = {0};

        global_scan_time += 100;
        release_msg.scan_time = global_scan_time;

        release_msg.fingers[watchdog_id].contact_id = 0;
        release_msg.fingers[watchdog_id].tip_switch = 0;
        release_msg.fingers[watchdog_id].confidence = 1;
        release_msg.fingers[watchdog_id].pressure_z = 0;

        release_msg.fingers[watchdog_id].x = watchdog_x;
        release_msg.fingers[watchdog_id].y = watchdog_y;

        release_msg.actual_count = 1;
        release_msg.button_mask = 0;

        if (tp_queue != NULL) {
            xQueueOverwrite(tp_queue, &release_msg);
        }
    }

}
