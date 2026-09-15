#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "I2C/TP/i2c_hid.h"
#include "I2C/SUB_DEV/cs40l25_surface.h"

#include "SYS/hid_msg.h"
#include "SYS/input_pipeline.h"

#define TAG "WATCHDOG"
#define TOUCH_TIMEOUT_US (100 * 1000)

esp_timer_handle_t timeout_watchdog_timer;

uint16_t watchdog_x = 0;
uint16_t watchdog_y = 0;
uint16_t watchdog_id = 0;
uint16_t watchdog_tip_switch = 0;

void watchdog_timeout_callback(void* arg) {
    (void)arg;
    if (current_tp_mode == PTP_MODE && global_watchdog_start) {
        global_watchdog_start = false;
        input_source_recover("watchdog");
    }
}
