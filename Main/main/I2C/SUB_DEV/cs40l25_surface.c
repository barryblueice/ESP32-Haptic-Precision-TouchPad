#include "cs40l25_surface.h"
#include "surface_haptic_hw.h"
#include "surface_haptic_settings.h"
#include "mcu-drivers/cs40l25/bsp/bsp_dut.h"
#include <inttypes.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "SURFACE_HAPTIC"
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static surface_haptic_runtime_t runtime;
static bool started, sleep_requested;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

surface_haptic_state_t cs40l25_surface_get_state(void)
{
    taskENTER_CRITICAL(&lock);
    surface_haptic_state_t state = runtime.state;
    taskEXIT_CRITICAL(&lock);
    return state;
}
bool cs40l25_surface_is_ready(void) { return cs40l25_surface_get_state() == SURFACE_READY; }

bool cs40l25_surface_is_modern_sleep(void)
{
    taskENTER_CRITICAL(&lock);
    bool sleeping = sleep_requested;
    taskEXIT_CRITICAL(&lock);
    return sleeping;
}

void cs40l25_surface_button_update(bool down, uint8_t setting)
{
    uint32_t now = now_ms();
    taskENTER_CRITICAL(&lock);
    surface_runtime_button(&runtime, down, setting, now);
    taskEXIT_CRITICAL(&lock);
}

void cs40l25_surface_cancel_click(void)
{
    taskENTER_CRITICAL(&lock);
    surface_runtime_cancel(&runtime);
    taskEXIT_CRITICAL(&lock);
}

void cs40l25_surface_set_modern_sleep(bool sleep_active)
{
    taskENTER_CRITICAL(&lock);
    if (runtime.state != SURFACE_FAULT && sleep_requested != sleep_active) {
        sleep_requested = sleep_active;
        // Stop admission immediately; only the worker changes the power pin.
        surface_runtime_state(&runtime, sleep_active ? SURFACE_SLEEPING : SURFACE_WAKING);
    }
    taskEXIT_CRITICAL(&lock);
}

static void fault(const char *stage, uint8_t waveform)
{
    taskENTER_CRITICAL(&lock);
    surface_runtime_state(&runtime, SURFACE_FAULT);
    taskEXIT_CRITICAL(&lock);
    ESP_LOGE(TAG, "FAULT at %s; haptics disabled until reset, touch reporting continues", stage);
    surface_haptic_hw_diagnostics(waveform);
    (void)surface_haptic_hw_power_off();
}

static void worker(void *arg)
{
    (void)arg;
    if (!surface_haptic_hw_initialize()) {
        fault("initialization", 0);
        vTaskDelete(NULL);
        return;
    }
    taskENTER_CRITICAL(&lock);
    surface_runtime_state(&runtime, sleep_requested ? SURFACE_SLEEPING : SURFACE_READY);
    taskEXIT_CRITICAL(&lock);
    bool powered = true, heartbeat_pending = false;
    uint32_t heartbeat_start = 0, drops_seen = 0;
    uint8_t last_waveform = 0;
    ESP_LOGI(TAG, "Initialized: Surface settings 0..100, MBOX1 PRESS/RELEASE");
    while (true) {
        taskENTER_CRITICAL(&lock);
        bool want_sleep = sleep_requested;
        taskEXIT_CRITICAL(&lock);
        if (want_sleep) {
            if (powered) {
                if (!surface_haptic_hw_power_off()) { fault("sleep power off", last_waveform); break; }
                powered = false;
                heartbeat_pending = false;
                ESP_LOGI(TAG, "SLEEP: boost off");
            }
        } else {
            if (!powered) {
                if (!surface_haptic_hw_wake()) { fault("wake", last_waveform); break; }
                powered = true;
                ESP_LOGI(TAG, "WAKE: power and DSP checked");
            }
            uint32_t now = now_ms();
            taskENTER_CRITICAL(&lock);
            // A sleep request arriving during wake wins over READY.
            if (!sleep_requested) surface_runtime_state(&runtime, SURFACE_READY);
            surface_haptic_event_t event;
            bool have_event = surface_runtime_pop(&runtime, now, &event);
            uint32_t drops = runtime.dropped;
            taskEXIT_CRITICAL(&lock);
            if (drops != drops_seen) {
                ESP_LOGW(TAG, "Dropped stale/full haptic queue; cancelled click (count=%" PRIu32 ")", drops);
                drops_seen = drops;
            }
            if (have_event) {
                if (!heartbeat_pending) {
                    bool changed;
                    if (bsp_dut_has_processed(&changed) != BSP_STATUS_OK) {
                        fault("heartbeat baseline", last_waveform); break;
                    }
                }
                uint8_t live_setting = ptp_haptic_click_intensity_get();
                uint32_t dispatch_time = now_ms();
                taskENTER_CRITICAL(&lock);
                if (live_setting == 0 || (uint32_t)(dispatch_time - event.time_ms) > SURFACE_EVENT_MAX_AGE_MS) {
                    surface_runtime_cancel(&runtime);
                }
                bool current = surface_runtime_current(&runtime, &event);
                taskEXIT_CRITICAL(&lock);
                if (current) {
                    last_waveform = event.release ? event.pair.release_index : event.pair.press_index;
                    if (!heartbeat_pending) heartbeat_start = now_ms();
                    uint32_t status = surface_haptic_play_event(&event.pair, event.release);
                    ESP_LOGD(TAG, "setting=%u %s index=%u result=%" PRIu32,
                             event.setting, event.release ? "RELEASE" : "PRESS", last_waveform, status);
                    if (status != BSP_STATUS_OK) { fault("playback", last_waveform); break; }
                    heartbeat_pending = true;
                }
            }
            if (surface_haptic_hw_process() != BSP_STATUS_OK) { fault("driver processing", last_waveform); break; }
            if (heartbeat_pending) {
                bool changed;
                if (bsp_dut_has_processed(&changed) != BSP_STATUS_OK) { fault("heartbeat read", last_waveform); break; }
                if (changed) heartbeat_pending = false;
                else if ((uint32_t)(now_ms() - heartbeat_start) >= 2000U) {
                    fault("heartbeat timeout", last_waveform); break;
                }
            }
        }
        TickType_t ticks = pdMS_TO_TICKS(10);
        vTaskDelay(ticks ? ticks : 1);
    }
    vTaskDelete(NULL);
}

void cs40l25_surface_init(void)
{
    taskENTER_CRITICAL(&lock);
    bool create = !started;
    started = true;
    taskEXIT_CRITICAL(&lock);
    if (create && xTaskCreatePinnedToCore(worker, "surface_haptic", 8192, NULL, 8, NULL, 1) != pdPASS) {
        fault("task allocation", 0);
    }
}
