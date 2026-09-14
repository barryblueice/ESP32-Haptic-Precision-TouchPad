#include "input_pipeline.h"
#include "rtos_queue.h"
#include "I2C/TP/i2c_hid.h"
#include "I2C/SUB_DEV/cs40l25_surface.h"
#include "esp_log.h"
#include <string.h>
#include <inttypes.h>

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static report_buffer_t reports;
static TaskHandle_t parser, sender;
static uint8_t ready_mask;
static bool mode_pending;
static uint8_t requested_mode;
static uint32_t request_serial;
static bool mode_applied;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static void notify(TaskHandle_t task) { if (task) xTaskNotifyGive(task); }

void input_pipeline_init(void)
{
    tp_data_queue = xQueueCreate(16, sizeof(input_frame_t));
    ESP_ERROR_CHECK(tp_data_queue ? ESP_OK : ESP_ERR_NO_MEM);
    reports.mode = current_tp_mode;
    report_buffer_reset(&reports, current_tp_mode);
}

void input_register_parser(void)
{
    taskENTER_CRITICAL(&lock); parser = xTaskGetCurrentTaskHandle(); taskEXIT_CRITICAL(&lock);
}
void input_register_sender(void)
{
    taskENTER_CRITICAL(&lock); sender = xTaskGetCurrentTaskHandle(); taskEXIT_CRITICAL(&lock);
}
void input_wake_sender(void)
{
    taskENTER_CRITICAL(&lock); TaskHandle_t task = sender; taskEXIT_CRITICAL(&lock);
    notify(task);
}
void input_wake_parser(void)
{
    taskENTER_CRITICAL(&lock); TaskHandle_t task = parser; taskEXIT_CRITICAL(&lock);
    notify(task);
}

void input_recover(void)
{
    taskENTER_CRITICAL(&lock);
    report_buffer_reset(&reports, reports.mode);
    TaskHandle_t p = parser, s = sender;
    taskEXIT_CRITICAL(&lock);
    cs40l25_surface_cancel_click();
    notify(p); notify(s);
}

void input_set_link(uint8_t mask)
{
    taskENTER_CRITICAL(&lock);
    bool changed = ready_mask != mask;
    ready_mask = mask;
    if (changed) {
        report_buffer_reset(&reports, reports.mode);
        /* A new connection has no state from the previous logical device. */
        reports.release_mask = 1U << reports.mode;
    }
    TaskHandle_t p = parser, s = sender;
    taskEXIT_CRITICAL(&lock);
    if (changed) cs40l25_surface_cancel_click();
    notify(p); notify(s);
}

uint32_t input_generation(void)
{
    taskENTER_CRITICAL(&lock); uint32_t g = reports.generation; taskEXIT_CRITICAL(&lock); return g;
}
uint8_t input_mode(void)
{
    taskENTER_CRITICAL(&lock); uint8_t m = reports.mode; taskEXIT_CRITICAL(&lock); return m;
}

void input_capture(const uint8_t *bytes, bool success, uint32_t generation, uint32_t time_ms)
{
    input_frame_t frame = {.generation = generation, .time_ms = time_ms};
    if (success) memcpy(frame.bytes, bytes, sizeof(frame.bytes));
    bool overflow = success && xQueueSend(tp_data_queue, &frame, 0) != pdPASS;
    if (!success || overflow) {
        taskENTER_CRITICAL(&lock);
        if (overflow) ++reports.stats.raw_overflows; else ++reports.stats.read_failures;
        taskEXIT_CRITICAL(&lock);
        input_recover();
    }
    taskENTER_CRITICAL(&lock); TaskHandle_t p = parser; taskEXIT_CRITICAL(&lock);
    notify(p);
}

bool input_next_frame(input_frame_t *frame)
{
    while (xQueueReceive(tp_data_queue, frame, 0) == pdPASS)
        if (frame->generation == input_generation()) return true;
    return false;
}

bool input_observe(uint32_t generation, bool all_up)
{
    taskENTER_CRITICAL(&lock);
    bool admitted = generation == reports.generation && !mode_pending &&
        report_buffer_observe(&reports, all_up) && (ready_mask & (1U << reports.mode));
    taskEXIT_CRITICAL(&lock);
    return admitted;
}

bool input_publish_pair(uint32_t generation, input_report_t *down, input_report_t *up)
{
    taskENTER_CRITICAL(&lock);
    bool ok = generation == reports.generation && !mode_pending && !reports.recovering;
    if (ok && reports.count <= REPORT_BUFFER_CAPACITY - 2) {
        ok = report_buffer_push(&reports, down, false) && report_buffer_push(&reports, up, false);
    } else ok = false;
    if (!ok && generation == reports.generation) report_buffer_reset(&reports, reports.mode);
    taskEXIT_CRITICAL(&lock);
    if (!ok) { cs40l25_surface_cancel_click(); input_wake_parser(); }
    input_wake_sender(); return ok;
}
bool input_publish(uint32_t generation, input_report_t *report, bool tap)
{
    taskENTER_CRITICAL(&lock);
    bool ok = generation == reports.generation && !mode_pending &&
        report_buffer_push(&reports, report, tap);
    bool reset = generation != reports.generation;
    TaskHandle_t s = sender, p = parser;
    taskEXIT_CRITICAL(&lock);
    if (reset) { cs40l25_surface_cancel_click(); notify(p); }
    notify(s);
    return ok;
}

bool input_take_report(input_report_t *report)
{
    taskENTER_CRITICAL(&lock);
    uint32_t before = reports.generation;
    bool ok = ready_mask && report_buffer_take(&reports, now_ms(), report);
    if (ok && !(ready_mask & (1U << report->mode))) ok = false;
    bool reset = before != reports.generation;
    TaskHandle_t p = parser;
    taskEXIT_CRITICAL(&lock);
    if (reset) { cs40l25_surface_cancel_click(); notify(p); }
    return ok;
}

bool input_report_current(const input_report_t *report)
{
    taskENTER_CRITICAL(&lock);
    bool ok = report_buffer_current(&reports, report) && (ready_mask & (1U << report->mode));
    uint32_t age = now_ms() - report->time_ms;
    if (ok && !report->release && age > reports.stats.longest_wait_ms) reports.stats.longest_wait_ms = age;
    bool stale = ok && !report->release && age > REPORT_MAX_AGE_MS;
    taskEXIT_CRITICAL(&lock);
    if (stale) { input_recover(); return false; }
    return ok;
}
void input_report_ack(const input_report_t *report)
{
    taskENTER_CRITICAL(&lock); report_buffer_ack(&reports, report); taskEXIT_CRITICAL(&lock);
}
void input_submit_failed(void)
{
    taskENTER_CRITICAL(&lock); ++reports.stats.submit_failures; taskEXIT_CRITICAL(&lock);
}
void input_get_stats(input_stats_t *stats)
{
    taskENTER_CRITICAL(&lock); *stats = reports.stats; taskEXIT_CRITICAL(&lock);
}

void input_log_stats(void)
{
    /* Called only by the selected sender. No per-frame logging. */
    static uint32_t last_log, last_errors;
    uint32_t now = now_ms();
    if (now - last_log < 5000U) return;
    last_log = now;
    input_stats_t stats;
    input_get_stats(&stats);
    uint32_t errors = stats.raw_overflows + stats.read_failures + stats.submit_failures + stats.recoveries;
    if (errors == last_errors) return;
    last_errors = errors;
    ESP_LOGW("INPUT", "recover=%" PRIu32 " raw_full=%" PRIu32 " read_fail=%" PRIu32
        " send_fail=%" PRIu32 " merged=%" PRIu32 " peak=%" PRIu32 " wait_ms=%" PRIu32,
        stats.recoveries, stats.raw_overflows, stats.read_failures, stats.submit_failures,
        stats.merged, stats.peak, stats.longest_wait_ms);
}

void input_request_mode(uint8_t mode)
{
    if (mode != MOUSE_MODE && mode != PTP_MODE) return;
    taskENTER_CRITICAL(&lock);
    if (!mode_pending && mode_applied && mode == reports.mode) {
        taskEXIT_CRITICAL(&lock);
        return;
    }
    requested_mode = mode; mode_pending = true; ++request_serial;
    report_buffer_reset(&reports, reports.mode);
    TaskHandle_t p = parser, s = sender;
    taskEXIT_CRITICAL(&lock);
    cs40l25_surface_cancel_click(); notify(p); notify(s);
}

bool input_apply_mode_request(void)
{
    taskENTER_CRITICAL(&lock);
    bool pending = mode_pending;
    uint8_t mode = requested_mode;
    uint32_t serial = request_serial;
    taskEXIT_CRITICAL(&lock);
    if (!pending) return false;
    esp_err_t err = touchpad_mode_set(mode == PTP_MODE);
    taskENTER_CRITICAL(&lock);
    if (err == ESP_OK) { current_tp_mode = mode; report_buffer_reset(&reports, mode); mode_applied = true; }
    else { report_buffer_reset(&reports, reports.mode); mode_applied = false; }
    if (serial == request_serial) mode_pending = false;
    taskEXIT_CRITICAL(&lock);
    if (err != ESP_OK) ESP_LOGW("INPUT", "Mode %u failed: %s", mode, esp_err_to_name(err));
    cs40l25_surface_cancel_click(); input_wake_sender();
    return true;
}
