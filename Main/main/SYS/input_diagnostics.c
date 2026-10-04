#include "input_diagnostics.h"
#if CONFIG_INPUT_LATENCY_DIAGNOSTICS
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <string.h>

/* Fixed buckets: bounded overhead, no allocation or per-frame logging. */
#define BINS 11
#define REASONS 24
static const uint32_t bounds[BINS] = {100,250,500,1000,2000,5000,10000,20000,50000,100000,UINT32_MAX};
static const char *names[INPUT_DIAG_COUNT] = {"read", "raw_wait", "parse", "force", "read_to_submit", "read_to_complete", "usb_flight", "capture_interval"};
typedef struct { uint32_t n, max, bins[BINS]; uint64_t sum; } metric_t;
typedef struct { const char *name; uint32_t n; } reason_t;
static metric_t metrics[INPUT_DIAG_COUNT];
static reason_t reasons[REASONS];
typedef struct {
    uint32_t zero, two, ffff, ptp, other, invalid, errors;
    uint8_t last_ptp[4], last_other[4];
} raw_results_t;
static raw_results_t raw_results;
static unsigned raw_peak, report_peak;
static portMUX_TYPE diag_lock = portMUX_INITIALIZER_UNLOCKED;
uint32_t input_diag_now(void) { return (uint32_t)esp_timer_get_time(); }
void input_diag_sample(input_diag_stage_t stage, uint32_t us)
{
    if (stage >= INPUT_DIAG_COUNT) return;
    unsigned bin = 0;
    while (bin + 1 < BINS && us > bounds[bin]) ++bin;
    taskENTER_CRITICAL(&diag_lock);
    metric_t *m = &metrics[stage];
    ++m->n; m->sum += us; ++m->bins[bin];
    if (us > m->max) m->max = us;
    taskEXIT_CRITICAL(&diag_lock);
}
void input_diag_read_result(const uint8_t *bytes, bool success)
{
    taskENTER_CRITICAL(&diag_lock);
    if (!success) ++raw_results.errors;
    else {
        uint16_t length = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
        if (length == 0) ++raw_results.zero;
        else if (length == 2) ++raw_results.two;
        else if (length == UINT16_MAX) ++raw_results.ffff;
        else if (length == 64) {
            ++raw_results.ptp;
            memcpy(raw_results.last_ptp, bytes, 4);
        } else if (length >= 6 && length < 64) {
            ++raw_results.other;
            memcpy(raw_results.last_other, bytes, 4);
        } else ++raw_results.invalid;
    }
    taskEXIT_CRITICAL(&diag_lock);
}
void input_diag_capture(uint32_t generation, bool active, uint32_t read_done_us)
{
    /* Reader-owned history. Exclude idle and source-reset gaps; unsigned
     * subtraction also handles the 32-bit microsecond clock wrapping. */
    static bool previous_active;
    static uint32_t previous_generation, previous_us;
    if (active && previous_active && generation == previous_generation)
        input_diag_sample(INPUT_DIAG_CAPTURE_INTERVAL, read_done_us - previous_us);
    previous_active = active;
    previous_generation = generation;
    previous_us = read_done_us;
}
void input_diag_depth(unsigned raw, unsigned reports)
{
    taskENTER_CRITICAL(&diag_lock);
    if (raw > raw_peak) raw_peak = raw;
    if (reports > report_peak) report_peak = reports;
    taskEXIT_CRITICAL(&diag_lock);
}
void input_diag_recovery(const char *reason)
{
    taskENTER_CRITICAL(&diag_lock);
    for (unsigned i = 0; i < REASONS; ++i) {
        if (!reasons[i].name || !strcmp(reasons[i].name, reason)) {
            reasons[i].name = reason; ++reasons[i].n; break;
        }
    }
    taskEXIT_CRITICAL(&diag_lock);
}
static void input_diag_log(void)
{
    static uint32_t last;
    uint32_t now = input_diag_now();
    if (now - last < 5000000U) return;
    last = now;
    metric_t snapshot[INPUT_DIAG_COUNT]; reason_t rs[REASONS];
    raw_results_t reads;
    taskENTER_CRITICAL(&diag_lock);
    memcpy(snapshot, metrics, sizeof(snapshot)); memset(metrics, 0, sizeof(metrics));
    memcpy(rs, reasons, sizeof(rs)); memset(reasons, 0, sizeof(reasons));
    reads = raw_results; memset(&raw_results, 0, sizeof(raw_results));
    unsigned raw = raw_peak, reports = report_peak;
    raw_peak = report_peak = 0;
    taskEXIT_CRITICAL(&diag_lock);
    ESP_LOGI("LATENCY", "5s peaks raw=%u reports=%u", raw, reports);
    ESP_LOGI("LATENCY", "raw_result zero=%lu two=%lu ffff=%lu len64=%lu other=%lu invalid=%lu errors=%lu",
        (unsigned long)reads.zero, (unsigned long)reads.two, (unsigned long)reads.ffff,
        (unsigned long)reads.ptp, (unsigned long)reads.other,
        (unsigned long)reads.invalid, (unsigned long)reads.errors);
    if (reads.ptp || reads.other)
        ESP_LOGI("LATENCY", "last_headers len64=%02X:%02X:%02X:%02X other=%02X:%02X:%02X:%02X",
            reads.last_ptp[0], reads.last_ptp[1], reads.last_ptp[2], reads.last_ptp[3],
            reads.last_other[0], reads.last_other[1], reads.last_other[2], reads.last_other[3]);
    for (unsigned i = 0; i < INPUT_DIAG_COUNT; ++i) {
        metric_t *m = &snapshot[i];
        if (!m->n) continue;
        uint32_t target = m->n - m->n / 20U, count = 0, p95 = m->max;
        for (unsigned j = 0; j < BINS; ++j) {
            count += m->bins[j];
            if (count >= target) { p95 = bounds[j] < m->max ? bounds[j] : m->max; break; }
        }
        ESP_LOGI("LATENCY", "%s n=%" PRIu32 " avg_us=%" PRIu32 " p95_le_us=%" PRIu32 " max_us=%" PRIu32,
            names[i], m->n, (uint32_t)(m->sum / m->n), p95, m->max);
        ESP_LOGI("LATENCY", "bins_us<=100/250/500/1k/2k/5k/10k/20k/50k/100k/over: %lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
            (unsigned long)m->bins[0], (unsigned long)m->bins[1], (unsigned long)m->bins[2],
            (unsigned long)m->bins[3], (unsigned long)m->bins[4], (unsigned long)m->bins[5],
            (unsigned long)m->bins[6], (unsigned long)m->bins[7], (unsigned long)m->bins[8],
            (unsigned long)m->bins[9], (unsigned long)m->bins[10]);
    }
    for (unsigned i = 0; i < REASONS; ++i)
        if (rs[i].n) ESP_LOGI("LATENCY", "recover[%s]=%" PRIu32, rs[i].name, rs[i].n);
}
static void diagnostic_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        input_diag_log();
    }
}
void input_diag_init(void)
{
    /* UART can take >100 ms to print a histogram. Never print while holding
     * connection/input locks or from the reader, parser or transport tasks. */
    if (xTaskCreatePinnedToCore(diagnostic_task, "input_diag", 4096, NULL, 1, NULL, 1) != pdPASS)
        ESP_LOGW("LATENCY", "Could not start diagnostic logger");
}
#endif
