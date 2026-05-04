#include "I2C/TP/i2c_hid.h"

#include <inttypes.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

#include "GPIO/GPIO_handle.h"
#include "I2C/SUB_DEV/cs40l25_surface.h"
#include "I2C/SUB_DEV/sub_dev.h"
#include "sdkconfig.h"

#define TAG "TP_SLEEP"
#define TP_SLEEP_HAPTIC_POWER_SETTLE_MS 20

#ifdef CONFIG_TP_ENABLE_SLEEP_MODE

static esp_timer_handle_t s_modern_sleep_timer = NULL;
static portMUX_TYPE s_modern_sleep_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_modern_sleep_active = false;
static bool s_restore_vref_pending = false;

static uint64_t tp_modern_sleep_timeout_us(void)
{
    if (CONFIG_TP_SLEEP_MODE_TIME_MS <= 0) {
        return 0;
    }

    return (uint64_t)CONFIG_TP_SLEEP_MODE_TIME_MS * 1000ULL;
}

static void tp_modern_sleep_enter_cb(void *arg)
{
    bool should_enter = false;

    (void)arg;

    taskENTER_CRITICAL(&s_modern_sleep_lock);
    if (!s_modern_sleep_active) {
        s_modern_sleep_active = true;
        s_restore_vref_pending = false;
        should_enter = true;
    }
    taskEXIT_CRITICAL(&s_modern_sleep_lock);

    if (should_enter) {
        cs40l25_surface_set_modern_sleep(true);
        gpio_set_level(GPIO_HAPTIC_BUCK_BOOST_EN, EN_OFF);
        ESP_LOGI(TAG,
                 "modern sleep enter: idle timeout=%" PRIu32 " ms, haptic buck/boost off",
                 (uint32_t)CONFIG_TP_SLEEP_MODE_TIME_MS);
    }
}

static void tp_modern_sleep_restart_timer(void)
{
    uint64_t timeout_us = tp_modern_sleep_timeout_us();

    if ((s_modern_sleep_timer == NULL) || (timeout_us == 0)) {
        return;
    }

    (void)esp_timer_stop(s_modern_sleep_timer);

    esp_err_t err = esp_timer_start_once(s_modern_sleep_timer, timeout_us);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to restart modern sleep timer: 0x%x", err);
    }
}

static bool tp_modern_sleep_take_restore_needed(void)
{
    bool restore_needed = false;

    taskENTER_CRITICAL(&s_modern_sleep_lock);
    if (s_modern_sleep_active) {
        s_modern_sleep_active = false;
        restore_needed = true;
    }

    if (s_restore_vref_pending) {
        s_restore_vref_pending = false;
        restore_needed = true;
    }
    taskEXIT_CRITICAL(&s_modern_sleep_lock);

    return restore_needed;
}

void tp_modern_sleep_init(void)
{
    uint64_t timeout_us = tp_modern_sleep_timeout_us();

    if (timeout_us == 0) {
        ESP_LOGW(TAG, "modern sleep disabled: invalid timeout=%d ms", CONFIG_TP_SLEEP_MODE_TIME_MS);
        return;
    }

    if (s_modern_sleep_timer != NULL) {
        return;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = tp_modern_sleep_enter_cb,
        .name = "tp_modern_sleep",
    };

    esp_err_t err = esp_timer_create(&timer_args, &s_modern_sleep_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to create modern sleep timer: 0x%x", err);
        return;
    }

    gpio_set_level(GPIO_HAPTIC_BUCK_BOOST_EN, EN_ON);
    tp_modern_sleep_restart_timer();

    ESP_LOGI(TAG,
             "modern sleep enabled: timeout=%" PRIu32 " ms, USB/2.4G/BLE tasks stay running",
             (uint32_t)CONFIG_TP_SLEEP_MODE_TIME_MS);
}

void tp_modern_sleep_record_activity(void)
{
    bool restore_needed = tp_modern_sleep_take_restore_needed();

    if (restore_needed) {
        gpio_set_level(GPIO_HAPTIC_BUCK_BOOST_EN, EN_ON);
        vTaskDelay(pdMS_TO_TICKS(TP_SLEEP_HAPTIC_POWER_SETTLE_MS));
        esp_err_t err = mp28167_set_vref_mv(840);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "modern sleep exit: failed to restore VREF: 0x%x", err);
        }
        cs40l25_surface_set_modern_sleep(false);
        ESP_LOGI(TAG, "modern sleep exit: TP activity, haptic buck/boost on");
    }

    tp_modern_sleep_restart_timer();
}

void tp_modern_sleep_signal_activity_from_isr(void)
{
    taskENTER_CRITICAL_ISR(&s_modern_sleep_lock);
    if (s_modern_sleep_active) {
        s_modern_sleep_active = false;
        s_restore_vref_pending = true;
    }
    taskEXIT_CRITICAL_ISR(&s_modern_sleep_lock);
}

bool tp_modern_sleep_is_active(void)
{
    bool is_active;

    taskENTER_CRITICAL(&s_modern_sleep_lock);
    is_active = s_modern_sleep_active;
    taskEXIT_CRITICAL(&s_modern_sleep_lock);

    return is_active;
}

#else

void tp_modern_sleep_init(void)
{
}

void tp_modern_sleep_record_activity(void)
{
}

void tp_modern_sleep_signal_activity_from_isr(void)
{
}

bool tp_modern_sleep_is_active(void)
{
    return false;
}

#endif
