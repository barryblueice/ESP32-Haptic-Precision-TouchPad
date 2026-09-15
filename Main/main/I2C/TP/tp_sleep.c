#include "I2C/TP/i2c_hid.h"
#include "I2C/SUB_DEV/cs40l25_surface.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include "SYS/device_config.h"

static esp_timer_handle_t timer;
static bool enabled;
static uint32_t timeout_ms;
static portMUX_TYPE sleep_lock = portMUX_INITIALIZER_UNLOCKED;
static bool active, wake_pending;
static int64_t last_activity;

static void sleep_cb(void *arg)
{
    (void)arg;
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&sleep_lock);
    if (enabled && !active && now - last_activity >= (int64_t)timeout_ms * 1000) {
        active = true;
        wake_pending = false;
        cs40l25_surface_set_modern_sleep(true);
    }
    taskEXIT_CRITICAL(&sleep_lock);
}

static void restart_timer(void)
{
    if (timer == NULL || !enabled) return;
    (void)esp_timer_stop(timer);
    esp_err_t err = esp_timer_start_once(timer, (uint64_t)timeout_ms * 1000);
    if (err != ESP_OK) ESP_LOGW("TP_SLEEP", "Could not restart idle timer: %s", esp_err_to_name(err));
}

void tp_modern_sleep_init(void)
{
    if (timer != NULL) return;
    device_config_t config; device_config_get(&config);
    enabled = device_config_sleep(&config);
    timeout_ms = rstp_u32(config.bytes + CFG_TIMEOUT);
    const esp_timer_create_args_t args = {.callback = sleep_cb, .name = "tp_modern_sleep"};
    if (esp_timer_create(&args, &timer) != ESP_OK) {
        ESP_LOGW("TP_SLEEP", "Idle sleep disabled: timer allocation failed");
        device_config_disable(1U << 4);
        return;
    }
    last_activity = esp_timer_get_time();
    restart_timer();
}

void tp_modern_sleep_record_activity(void)
{
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&sleep_lock);
    last_activity = now;
    if (active || wake_pending) {
        active = wake_pending = false;
        cs40l25_surface_set_modern_sleep(false);
    }
    taskEXIT_CRITICAL(&sleep_lock);
    restart_timer();
}

void tp_modern_sleep_signal_activity_from_isr(void)
{
    taskENTER_CRITICAL_ISR(&sleep_lock);
    if (active) {
        active = false;
        wake_pending = true;
    }
    taskEXIT_CRITICAL_ISR(&sleep_lock);
}

bool tp_modern_sleep_is_active(void)
{
    taskENTER_CRITICAL(&sleep_lock);
    bool result = active;
    taskEXIT_CRITICAL(&sleep_lock);
    return result;
}
