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
static uint32_t source_generation;
static bool source_wait_up, output_wait_up;
static const char *source_reason = "startup", *output_reason = "startup";

/* All reset bookkeeping and haptic admission are serialized by lock. */
static void source_reset_locked(const char *reason)
{
    ++source_generation;
    source_wait_up = true;
    source_reason = reason;
    cs40l25_surface_cancel_click();
}

static void output_reset_locked(const char *reason)
{
    output_reason = reason;
    output_wait_up = true;
    if (current_mode != _2_4_MODE) source_reset_locked(reason);
}

static bool output_ready_locked(uint32_t generation)
{
    return generation == reports.generation && !mode_pending && !reports.recovering &&
        (current_mode != _2_4_MODE || !output_wait_up) && (ready_mask & (1U << reports.mode));
}

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static void notify(TaskHandle_t task) { if (task) xTaskNotifyGive(task); }

void input_pipeline_init(void)
{
    tp_data_queue = xQueueCreate(16, sizeof(input_frame_t));
    ESP_ERROR_CHECK(tp_data_queue ? ESP_OK : ESP_ERR_NO_MEM);
    reports.mode = current_tp_mode;
    report_buffer_reset(&reports, current_tp_mode);
    source_generation = 1;
    source_wait_up = output_wait_up = true;
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
    output_reset_locked("send/recovery");
    TaskHandle_t p = parser, s = sender;
    taskEXIT_CRITICAL(&lock);
    notify(p); notify(s);
}

void input_source_recover(const char *reason)
{
    taskENTER_CRITICAL(&lock);
    report_buffer_reset(&reports, reports.mode);
    output_wait_up = true;
    output_reason = reason;
    source_reset_locked(reason);
    TaskHandle_t p = parser, s = sender;
    taskEXIT_CRITICAL(&lock);
    notify(p); notify(s);
}

uint32_t input_source_generation(void)
{
    taskENTER_CRITICAL(&lock); uint32_t g = source_generation; taskEXIT_CRITICAL(&lock); return g;
}

bool input_source_observe(uint32_t generation, bool all_up)
{
    taskENTER_CRITICAL(&lock);
    bool current = generation == source_generation && !mode_pending;
    bool admitted = current && !source_wait_up;
    if (current && source_wait_up && all_up) {
        source_wait_up = false;
        /* Cancel retains the previous button level until a real lift is observed. */
        cs40l25_surface_button_update(false, 0);
    }
    taskEXIT_CRITICAL(&lock);
    return admitted;
}

void input_source_button(uint32_t generation, bool down)
{
    uint8_t setting = ptp_haptic_click_intensity_get();
    taskENTER_CRITICAL(&lock);
    if (generation == source_generation && !source_wait_up && !mode_pending)
        cs40l25_surface_button_update(down, setting);
    taskEXIT_CRITICAL(&lock);
}

bool input_output_ready(uint32_t generation)
{
    taskENTER_CRITICAL(&lock); bool ready = output_ready_locked(generation); taskEXIT_CRITICAL(&lock);
    return ready;
}

void input_set_link(uint8_t mask)
{
    taskENTER_CRITICAL(&lock);
    bool changed = ready_mask != mask;
    ready_mask = mask;
    if (changed) {
        report_buffer_reset(&reports, reports.mode);
        output_reset_locked("link");
        /* A new connection has no state from the previous logical device. */
        reports.release_mask = 1U << reports.mode;
    }
    TaskHandle_t p = parser, s = sender;
    taskEXIT_CRITICAL(&lock);
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

void input_capture(const uint8_t *bytes, bool success, uint32_t generation,
                   uint32_t output_generation, uint32_t time_ms)
{
    input_frame_t frame = {.generation = generation, .output_generation = output_generation, .time_ms = time_ms};
    if (success) memcpy(frame.bytes, bytes, sizeof(frame.bytes));
    bool overflow = success && xQueueSend(tp_data_queue, &frame, 0) != pdPASS;
    if (!success || overflow) {
        taskENTER_CRITICAL(&lock);
        if (overflow) ++reports.stats.raw_overflows; else ++reports.stats.read_failures;
        taskEXIT_CRITICAL(&lock);
        input_source_recover(overflow ? "raw_full" : "read_fail");
    }
    taskENTER_CRITICAL(&lock); TaskHandle_t p = parser; taskEXIT_CRITICAL(&lock);
    notify(p);
}

bool input_next_frame(input_frame_t *frame)
{
    while (xQueueReceive(tp_data_queue, frame, 0) == pdPASS)
        if (frame->generation == input_source_generation()) return true;
    return false;
}

bool input_observe(uint32_t generation, bool all_up)
{
    taskENTER_CRITICAL(&lock);
    bool waiting = output_wait_up;
    bool admitted = generation == reports.generation && !mode_pending &&
        report_buffer_observe(&reports, all_up) && (ready_mask & (1U << reports.mode));
    if (current_mode == _2_4_MODE && waiting) {
        if (admitted && all_up) output_wait_up = false;
        admitted = false; /* The recovery lift must never become an offline tap. */
    }
    taskEXIT_CRITICAL(&lock);
    return admitted;
}

bool input_publish_pair(uint32_t generation, input_report_t *down, input_report_t *up)
{
    taskENTER_CRITICAL(&lock);
    uint32_t before = reports.generation;
    bool ok = output_ready_locked(generation);
    if (ok) {
        if (reports.count <= REPORT_BUFFER_CAPACITY - 2)
            ok = report_buffer_push(&reports, down, false) && report_buffer_push(&reports, up, false);
        else { report_buffer_reset(&reports, reports.mode); ok = false; }
    }
    bool reset = before != reports.generation;
    if (reset) output_reset_locked("pair_full");
    taskEXIT_CRITICAL(&lock);
    if (reset) input_wake_parser();
    input_wake_sender(); return ok;
}
bool input_publish(uint32_t generation, input_report_t *report, bool tap)
{
    taskENTER_CRITICAL(&lock);
    uint32_t before = reports.generation;
    bool ok = output_ready_locked(generation) &&
        report_buffer_push(&reports, report, tap);
    bool reset = before != reports.generation;
    if (reset) output_reset_locked("report_full");
    TaskHandle_t s = sender, p = parser;
    taskEXIT_CRITICAL(&lock);
    if (reset) notify(p);
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
    if (reset) output_reset_locked("report_age");
    TaskHandle_t p = parser;
    taskEXIT_CRITICAL(&lock);
    if (reset) notify(p);
    return ok;
}

bool input_report_current(const input_report_t *report)
{
    taskENTER_CRITICAL(&lock);
    bool ok = report_buffer_current(&reports, report) && (ready_mask & (1U << report->mode));
    uint32_t age = now_ms() - report->time_ms;
    if (ok && !report->release && age > reports.stats.longest_wait_ms) reports.stats.longest_wait_ms = age;
    bool stale = ok && !report->release && age > REPORT_MAX_AGE_MS;
    if (stale) {
        report_buffer_reset(&reports, reports.mode);
        output_reset_locked("report_age");
    }
    TaskHandle_t p = parser, s = sender;
    taskEXIT_CRITICAL(&lock);
    if (stale) { notify(p); notify(s); return false; }
    return ok;
}
void input_report_ack(const input_report_t *report)
{
    taskENTER_CRITICAL(&lock);
    report_buffer_ack(&reports, report);
    if (report_buffer_current(&reports, report) && !reports.recovering && reports.all_up &&
        !mode_pending && (ready_mask & (1U << reports.mode))) output_wait_up = false;
    taskEXIT_CRITICAL(&lock);
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
    static uint32_t last_log, last_errors, last_source, last_gate = UINT32_MAX;
    uint32_t now = now_ms();
    if (now - last_log < 5000U) return;
    last_log = now;
    taskENTER_CRITICAL(&lock);
    input_stats_t stats = reports.stats;
    uint8_t link = ready_mask, mode = reports.mode, releases = reports.release_mask;
    bool recovering = reports.recovering, all_up = reports.all_up, changing = mode_pending;
    uint32_t source = source_generation;
    const char *source_why = source_reason, *output_why = output_reason;
    taskEXIT_CRITICAL(&lock);
    uint32_t gate = link | ((uint32_t)mode << 8) | ((uint32_t)releases << 16) |
        ((uint32_t)recovering << 24) | ((uint32_t)all_up << 25) | ((uint32_t)changing << 26);
    uint32_t errors = stats.raw_overflows + stats.read_failures + stats.submit_failures + stats.recoveries;
    if (errors == last_errors && gate == last_gate && source == last_source) return;
    last_source = source;
    last_errors = errors;
    last_gate = gate;
    ESP_LOGW("INPUT", "recover=%" PRIu32 " raw_full=%" PRIu32 " read_fail=%" PRIu32
        " send_fail=%" PRIu32 " merged=%" PRIu32 " peak=%" PRIu32 " wait_ms=%" PRIu32
        " link=%u mode=%u recovering=%u all_up=%u releases=%u mode_pending=%u"
        " source_gen=%" PRIu32 " source_reason=%s output_reason=%s haptic_state=%u",
        stats.recoveries, stats.raw_overflows, stats.read_failures, stats.submit_failures,
        stats.merged, stats.peak, stats.longest_wait_ms,
        (unsigned)link, (unsigned)mode, (unsigned)recovering, (unsigned)all_up,
        (unsigned)releases, (unsigned)changing, source, source_why, output_why,
        (unsigned)cs40l25_surface_get_state());
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
    output_wait_up = true;
    output_reason = "mode";
    source_reset_locked("mode");
    TaskHandle_t p = parser, s = sender;
    taskEXIT_CRITICAL(&lock);
    notify(p); notify(s);
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
    output_wait_up = true;
    source_reset_locked("mode_applied");
    if (serial == request_serial) mode_pending = false;
    taskEXIT_CRITICAL(&lock);
    if (err != ESP_OK) ESP_LOGW("INPUT", "Mode %u failed: %s", mode, esp_err_to_name(err));
    input_wake_sender();
    return true;
}
