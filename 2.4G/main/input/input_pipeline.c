#include "input_pipeline.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <inttypes.h>

static portMUX_TYPE pipeline_lock = portMUX_INITIALIZER_UNLOCKED;
static report_buffer_t reports;
static TaskHandle_t sender;
static bool usb_ready, link_online;
static uint32_t link_seen_at;
static current_input_mode_t mode = TP_MOUSE_MODE;

uint32_t input_now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
void input_init(void) { report_buffer_reset(&reports); }
void input_register_sender(void)
{
    taskENTER_CRITICAL(&pipeline_lock);
    sender = xTaskGetCurrentTaskHandle();
    taskEXIT_CRITICAL(&pipeline_lock);
}
void input_wake_sender(void)
{
    taskENTER_CRITICAL(&pipeline_lock);
    TaskHandle_t task = sender;
    taskEXIT_CRITICAL(&pipeline_lock);
    if (task) xTaskNotifyGive(task);
}
uint32_t input_generation(void)
{
    taskENTER_CRITICAL(&pipeline_lock);
    uint32_t result = reports.generation;
    taskEXIT_CRITICAL(&pipeline_lock);
    return result;
}
void input_recover(void)
{
    taskENTER_CRITICAL(&pipeline_lock);
    report_buffer_reset(&reports);
    taskEXIT_CRITICAL(&pipeline_lock);
    input_wake_sender();
}
void input_invalid_packet(void)
{
    taskENTER_CRITICAL(&pipeline_lock);
    ++reports.stats.invalid_packets;
    taskEXIT_CRITICAL(&pipeline_lock);
}
void input_rx_overflow(void)
{
    taskENTER_CRITICAL(&pipeline_lock);
    ++reports.stats.rx_overflows;
    report_buffer_reset(&reports);
    taskEXIT_CRITICAL(&pipeline_lock);
    input_wake_sender();
}
void input_set_usb(bool ready)
{
    taskENTER_CRITICAL(&pipeline_lock);
    usb_ready = ready;
    report_buffer_reset(&reports);
    taskEXIT_CRITICAL(&pipeline_lock);
    input_wake_sender();
}
void input_set_mode(current_input_mode_t value)
{
    if (value != TP_MOUSE_MODE && value != TP_PTP_MODE) return;
    taskENTER_CRITICAL(&pipeline_lock);
    if (mode != value) {
        mode = value;
        report_buffer_reset(&reports);
        reports.active = REPORT_NONE;
    }
    taskEXIT_CRITICAL(&pipeline_lock);
    input_wake_sender();
}
current_input_mode_t input_mode(void)
{
    taskENTER_CRITICAL(&pipeline_lock);
    current_input_mode_t result = mode;
    taskEXIT_CRITICAL(&pipeline_lock);
    return result;
}
void input_link_seen(uint32_t time_ms)
{
    taskENTER_CRITICAL(&pipeline_lock);
    link_seen_at = time_ms;
    link_online = true;
    taskEXIT_CRITICAL(&pipeline_lock);
}
void input_check_link(uint32_t now)
{
    taskENTER_CRITICAL(&pipeline_lock);
    if (link_online && (uint32_t)(now - link_seen_at) > 5000U) {
        link_online = false;
        report_buffer_reset(&reports);
    }
    taskEXIT_CRITICAL(&pipeline_lock);
}
void input_receive(const wireless_msg_t *packet, uint32_t generation, uint32_t time_ms)
{
    input_report_t report = {.kind = wireless_report_kind(packet->type),
        .generation = generation, .time_ms = time_ms, .data = packet->payload};
    if (!report.kind) return;
    taskENTER_CRITICAL(&pipeline_lock);
    bool matching = (report.kind == REPORT_MOUSE) == (mode == TP_MOUSE_MODE);
    if (generation == reports.generation && matching && usb_ready && link_online) {
        if ((uint32_t)(input_now_ms() - time_ms) > REPORT_MAX_AGE_MS) report_buffer_reset(&reports);
        else report_buffer_push(&reports, &report);
    }
    taskEXIT_CRITICAL(&pipeline_lock);
    input_wake_sender();
}
bool input_take(input_report_t *report)
{
    taskENTER_CRITICAL(&pipeline_lock);
    bool result = usb_ready && report_buffer_take(&reports, input_now_ms(), report);
    taskEXIT_CRITICAL(&pipeline_lock);
    return result;
}
bool input_current(const input_report_t *report)
{
    taskENTER_CRITICAL(&pipeline_lock);
    bool result = usb_ready && report_buffer_current(&reports, report, input_now_ms());
    taskEXIT_CRITICAL(&pipeline_lock);
    return result;
}
void input_submitted(const input_report_t *report)
{
    taskENTER_CRITICAL(&pipeline_lock);
    report_buffer_submitted(&reports, report);
    taskEXIT_CRITICAL(&pipeline_lock);
}
void input_complete(const input_report_t *report, bool success)
{
    taskENTER_CRITICAL(&pipeline_lock);
    if (success) report_buffer_ack(&reports, report);
    else {
        ++reports.stats.usb_failures;
        if (report->generation == reports.generation) report_buffer_reset(&reports);
    }
    taskEXIT_CRITICAL(&pipeline_lock);
    input_wake_sender();
}
void input_submit_failed(void)
{
    taskENTER_CRITICAL(&pipeline_lock);
    ++reports.stats.usb_failures;
    taskEXIT_CRITICAL(&pipeline_lock);
}
void input_get_stats(input_stats_t *out)
{
    taskENTER_CRITICAL(&pipeline_lock);
    *out = reports.stats;
    taskEXIT_CRITICAL(&pipeline_lock);
}
void input_log_stats(void)
{
    static uint32_t logged_at, last_errors;
    uint32_t now = input_now_ms();
    if ((uint32_t)(now - logged_at) < 5000U) return;
    logged_at = now;
    input_stats_t s;
    input_get_stats(&s);
    uint32_t errors = s.invalid_packets + s.rx_overflows + s.recoveries + s.usb_failures;
    if (errors == last_errors) return;
    last_errors = errors;
    ESP_LOGW("INPUT", "invalid=%" PRIu32 " rx_full=%" PRIu32 " recover=%" PRIu32
        " usb_fail=%" PRIu32 " merged=%" PRIu32 " peak=%" PRIu32 " wait_ms=%" PRIu32,
        s.invalid_packets, s.rx_overflows, s.recoveries, s.usb_failures,
        s.merged, s.peak, s.longest_wait_ms);
}
