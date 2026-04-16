#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "I2C/i2c_hid.h"

#include "SYS/hid_msg.h"

#define TAG "WATCHDOG"

esp_timer_handle_t timeout_watchdog_timer;

uint16_t watchdog_x = 0;
uint16_t watchdog_y = 0;

void watchdog_timeout_callback(void* arg) {

    if (current_tp_mode == PTP_MODE && global_watchdog_start) {

        tp_multi_msg_t release_msg = {0};

        global_scan_time += 100;
        release_msg.scan_time = global_scan_time;

        release_msg.fingers[0].contact_id = 0;
        release_msg.fingers[0].tip_switch = 0;
        release_msg.fingers[0].confidence = 1;
        
        release_msg.fingers[0].x = watchdog_x;
        release_msg.fingers[0].y = watchdog_y;

        release_msg.actual_count = 1;
        release_msg.button_mask = 0;

        if (tp_queue != NULL) {
            xQueueOverwrite(tp_queue, &release_msg);
        }
    }

}