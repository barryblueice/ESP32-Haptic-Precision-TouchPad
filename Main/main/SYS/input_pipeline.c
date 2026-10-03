#include "input_pipeline.h"
#include "device_config.h"
#include "rtos_queue.h"
#include "I2C/TP/i2c_hid.h"
#include "I2C/TP/tp_raw_trace.h"
#include "I2C/SUB_DEV/cs40l25_surface.h"
#include "esp_log.h"
#include <string.h>
#include <inttypes.h>
#include "sdkconfig.h"
#ifdef ESP_PLATFORM
#include "I2C/TP/force_forward.h"
#endif

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static report_buffer_t reports;
static TaskHandle_t parser, sender;
static TaskHandle_t transport_senders[3];
static bool transport_paused;
static uint8_t ready_mask;
static bool mode_pending;
static uint8_t requested_mode;
static uint32_t request_serial;
static bool mode_applied;
static uint32_t source_generation;
static bool source_wait_up, output_wait_up;
static bool usb_session, physical_active, source_uncertain;
static bool startup_pending, first_frame_seen;
static uint8_t host_active_mask;
static bool physical_mode_valid, physical_ptp, mode_retry;
static uint32_t mode_retry_at;
static const char *source_reason = "startup", *output_reason = "startup";

/* All reset bookkeeping and haptic admission are serialized by lock. */
static void source_reset_locked(const char *reason)
{
    ++source_generation;
#ifdef ESP_PLATFORM
    force_forward_invalidate();
#endif
    source_wait_up = true;
    source_reason = reason;
    cs40l25_surface_cancel_click();
}

static void output_reset_locked(const char *reason)
{
    startup_pending = false;
    output_reason = reason;
    output_wait_up = true;
    if (current_mode != _2_4_MODE) {
        source_reset_locked(reason);
        source_uncertain = true;
    }
    if (usb_session) {
        source_uncertain = true;
        reports.release_mask = host_active_mask;
    }
}

/* Cold startup rebases the first gesture without an activation lift. After
 * actual host input (or a fault), transitions retain physical-lift protection. */
static void transition_locked(const char *reason, bool reset_source)
{
    bool wait_up = source_uncertain || (!startup_pending && (!usb_session || physical_active));
    if (reset_source) {
        source_reset_locked(reason);
        source_wait_up = wait_up;
    }
    output_wait_up = wait_up;
    output_reason = reason;
    if (usb_session) reports.release_mask = host_active_mask;
    else if (startup_pending) reports.release_mask = 1U << reports.mode;
    if (ready_mask) reports.release_mask &= ready_mask;
    reports.recovery_ready = !wait_up;
    reports.recovering = wait_up || reports.release_mask;
}

static bool output_ready_locked(uint32_t generation)
{
    return !transport_paused && generation == reports.generation && !mode_pending && !reports.recovering &&
        (current_mode != _2_4_MODE || !output_wait_up) && (ready_mask & (1U << reports.mode));
}

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static void notify(TaskHandle_t task) { if (task) xTaskNotifyGive(task); }

void input_pipeline_init(void)
{
#ifdef ESP_PLATFORM
    /* Serial instrumentation is excluded from native algorithm harnesses. */
    tp_raw_trace_init();
#endif
    tp_data_queue = xQueueCreate(16, sizeof(input_frame_t));
    ESP_ERROR_CHECK(tp_data_queue ? ESP_OK : ESP_ERR_NO_MEM);
    reports.mode = current_tp_mode;
    report_buffer_reset(&reports, current_tp_mode);
    source_generation = 1;
    /* There is no old gesture to recover at cold boot. */
    source_wait_up = output_wait_up = false;
    reports.recovery_ready = true;
    startup_pending = true;
    first_frame_seen = false;
    usb_session = physical_active = source_uncertain = false;
    host_active_mask = 0;
    physical_mode_valid = mode_retry = false;
    transport_paused = false;
}

bool input_starting(void)
{
    taskENTER_CRITICAL(&lock); bool starting = startup_pending; taskEXIT_CRITICAL(&lock);
    return starting;
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
    TaskHandle_t tasks[3];
    taskENTER_CRITICAL(&lock); TaskHandle_t task = sender;
    memcpy(tasks, transport_senders, sizeof(tasks)); taskEXIT_CRITICAL(&lock);
    notify(task);
    for (unsigned i = 0; i < 3; ++i) notify(tasks[i]);
}
void input_register_transport_sender(unsigned transport)
{
    if (transport >= 3) return;
    taskENTER_CRITICAL(&lock);
    transport_senders[transport] = xTaskGetCurrentTaskHandle();
    if ((int)transport == current_mode) sender = transport_senders[transport];
    taskEXIT_CRITICAL(&lock);
}
void input_transport_quiesce(void)
{
    taskENTER_CRITICAL(&lock);
    transport_paused = true;
    report_buffer_reset(&reports, reports.mode);
    reports.release_mask = usb_session ? host_active_mask : (1U << reports.mode);
    source_reset_locked("transport");
    source_uncertain = output_wait_up = true;
    startup_pending = false;
    taskEXIT_CRITICAL(&lock);
    input_wake_parser(); input_wake_sender();
}
bool input_transport_drained(void)
{
    taskENTER_CRITICAL(&lock); bool done = reports.release_mask == 0; taskEXIT_CRITICAL(&lock);
    return done;
}
void input_transport_start(int transport, uint8_t mode, bool ready, bool initial)
{
    taskENTER_CRITICAL(&lock);
    current_mode = transport;
    sender = transport_senders[transport];
    usb_session = transport == WIRED_MODE;
    host_active_mask = 0;
    transport_paused = false;
    ready_mask = ready ? (transport == BLE_MODE ? 1 : 3) : 0;
    requested_mode = mode; mode_pending = true; mode_retry = mode_applied = false; ++request_serial;
    report_buffer_reset(&reports, mode);
    reports.release_mask = 1U << mode;
    source_reset_locked("transport_start");
    source_uncertain = source_wait_up = output_wait_up = !initial;
    startup_pending = initial;
    reports.recovery_ready = initial;
    taskEXIT_CRITICAL(&lock);
    input_wake_parser(); input_wake_sender();
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
    startup_pending = false;
    report_buffer_reset(&reports, reports.mode);
    output_wait_up = true;
    output_reason = reason;
    source_reset_locked(reason);
    source_uncertain = true;
    if (usb_session) reports.release_mask = host_active_mask;
    TaskHandle_t p = parser, s = sender;
    taskEXIT_CRITICAL(&lock);
    notify(p); notify(s);
}

uint32_t input_source_generation(void)
{
    taskENTER_CRITICAL(&lock); uint32_t g = source_generation; taskEXIT_CRITICAL(&lock); return g;
}

bool input_force_forward_ready(uint32_t generation)
{
    taskENTER_CRITICAL(&lock);
    bool ready = generation == source_generation && physical_mode_valid && mode_applied &&
        !mode_pending && !transport_paused;
    taskEXIT_CRITICAL(&lock);
    return ready;
}

bool input_source_observe(uint32_t generation, bool all_up)
{
    taskENTER_CRITICAL(&lock);
    bool current = generation == source_generation;
    bool admitted = current && !transport_paused && !mode_pending && !source_wait_up;
    if (current && all_up) source_uncertain = false;
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
    if (generation == source_generation && !transport_paused && !source_wait_up && !mode_pending)
        cs40l25_surface_button_update(down, setting);
    taskEXIT_CRITICAL(&lock);
}

void input_source_gesture(uint32_t generation, bool point)
{
    if (!(device_config_value(CFG_FEATURE_FLAGS) & CFG_FLAG_CUSTOM_GESTURE_HAPTICS)) return;
    taskENTER_CRITICAL(&lock);
    if (generation == source_generation && !transport_paused && !source_wait_up && !mode_pending)
        cs40l25_surface_gesture(point);
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
        /* A new connection has no state from the previous logical device. */
        reports.release_mask = 1U << reports.mode;
        transition_locked("link", current_mode != _2_4_MODE);
    }
    TaskHandle_t p = parser, s = sender;
    taskEXIT_CRITICAL(&lock);
    notify(p); notify(s);
}

void input_usb_reset(void)
{
    taskENTER_CRITICAL(&lock);
    usb_session = true;
    ready_mask = host_active_mask = 0;
    /* A bus reset or resume destroys pending host reports, not the Input Mode
     * the host negotiated. Forcing mouse here flipped the controller and
     * stalled input on every wake, so keep the mode that is already applied
     * (or still pending) and only bootstrap mouse when nothing was chosen. */
    if (!mode_pending && !mode_applied) {
        requested_mode = MOUSE_MODE;
        mode_pending = true;
        mode_retry = false;
        ++request_serial;
        report_buffer_reset(&reports, MOUSE_MODE);
    } else {
        report_buffer_reset(&reports, reports.mode);
    }
    transition_locked("usb_reset", true);
    taskEXIT_CRITICAL(&lock);
    input_wake_parser(); input_wake_sender();
}

void input_usb_link(bool ready)
{
    taskENTER_CRITICAL(&lock);
    if (ready_mask != (ready ? 3 : 0)) {
        report_buffer_reset(&reports, reports.mode);
        /* Cold startup includes an already resting finger; runtime reconnection
         * still requires the previous physical contact to end. */
        transition_locked(ready ? "usb_ready" : "usb_suspend", true);
    }
    ready_mask = ready ? 3 : 0;
    taskEXIT_CRITICAL(&lock);
    input_wake_parser(); input_wake_sender();
}

static bool report_active(const input_report_t *report)
{
    if (report->release) return false;
    if (report->mode == MOUSE_MODE) return report->data.mouse.buttons != 0;
    if (report->data.ptp.buttons) return true;
    for (unsigned i = 0; i < report->data.ptp.contact_count && i < 5; ++i)
        if (report->data.ptp.fingers[i].tip_conf_id & 2U) return true;
    return false;
}

void input_report_submitted(const input_report_t *report)
{
    taskENTER_CRITICAL(&lock);
    if (usb_session && report_active(report)) {
        host_active_mask |= 1U << report->mode;
        /* A parser recovery can race with a successful endpoint submission. */
        if (!report_buffer_current(&reports, report)) {
            reports.release_mask |= 1U << report->mode;
            reports.recovering = true;
        }
    }
    taskEXIT_CRITICAL(&lock);
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
    if (success) {
        uint16_t length = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
        /* Zero-length HID-I2C RESET completion is not a mouse sample. Its
         * unread/stale tail must never become contact or lift evidence. */
        if (length == 0 || length == 2 || length == UINT16_MAX) return;
        if (length < 6 || length > 64) {
            taskENTER_CRITICAL(&lock); ++reports.stats.read_failures; taskEXIT_CRITICAL(&lock);
            input_source_recover("raw_length");
            return;
        }
    }
#ifdef ESP_PLATFORM
    if (success) tp_raw_trace_capture(bytes, time_ms, generation, output_generation);
#endif
    input_frame_t frame = {.generation = generation, .output_generation = output_generation, .time_ms = time_ms};
    if (success) memcpy(frame.bytes, bytes, sizeof(frame.bytes));
    if (success) {
        bool active = (bytes[3] & 7U) != 0;
        if (bytes[0] == 0x40) {
            active = false;
            for (unsigned i = 0; i < 5; ++i) active |= (bytes[4 + i * 8] & 1U) != 0;
        }
        taskENTER_CRITICAL(&lock);
        /* Track capture, even before the parser runs or while mode is pending. */
        if (generation == source_generation && now_ms() - time_ms <= REPORT_MAX_AGE_MS) {
            physical_active = active;
            /* Retain real lift evidence even if a logical mode/session change
             * invalidates this queued frame before the parser gets to it. */
            if (!active) source_uncertain = false;
        }
        taskEXIT_CRITICAL(&lock);
    }
    bool overflow = success && xQueueSend(tp_data_queue, &frame, 0) != pdPASS;
    if (!success || overflow) {
        taskENTER_CRITICAL(&lock);
        if (overflow) ++reports.stats.raw_overflows; else ++reports.stats.read_failures;
        taskEXIT_CRITICAL(&lock);
        input_source_recover(overflow ? "raw_full" : "read_fail");
    }
    taskENTER_CRITICAL(&lock);
    bool first = success && !overflow && generation == source_generation && !first_frame_seen;
    if (first) first_frame_seen = true;
    TaskHandle_t p = parser;
    taskEXIT_CRITICAL(&lock);
    if (first) ESP_LOGI("INPUT", "First controller input at %" PRIu32 " ms, length=%u", now_ms(), bytes[0]);
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
    bool observed = generation == reports.generation && report_buffer_observe(&reports, all_up);
    bool admitted = observed && !transport_paused && !mode_pending && (ready_mask & (1U << reports.mode));
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
    if (reset) {
        output_reset_locked("report_age");
        /* Reset may have removed release bits for untouched USB interfaces. */
        if (usb_session) ok = ready_mask && report_buffer_take(&reports, now_ms(), report);
    }
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
    bool first = startup_pending && !report->release && report_buffer_current(&reports, report) && ready_mask;
    if (first) startup_pending = false;
    if (usb_session && report_buffer_current(&reports, report) && !report_active(report))
        host_active_mask &= ~(1U << report->mode);
    report_buffer_ack(&reports, report);
    if (report_buffer_current(&reports, report) && !reports.recovering && reports.recovery_ready &&
        !mode_pending && (ready_mask & (1U << reports.mode))) output_wait_up = false;
    taskEXIT_CRITICAL(&lock);
    if (first) ESP_LOGI("INPUT", "First host input acknowledged at %" PRIu32 " ms, transport=%" PRId32, now_ms(), current_mode);
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
    bool source_gated = source_wait_up, recovery_ready = reports.recovery_ready;
    uint8_t host_active = host_active_mask;
    uint32_t source = source_generation;
    const char *source_why = source_reason, *output_why = output_reason;
    taskEXIT_CRITICAL(&lock);
    uint32_t gate = link | ((uint32_t)mode << 8) | ((uint32_t)releases << 16) |
        ((uint32_t)recovering << 24) | ((uint32_t)all_up << 25) | ((uint32_t)changing << 26) |
        ((uint32_t)source_gated << 27) | ((uint32_t)recovery_ready << 28) | ((uint32_t)host_active << 29);
    uint32_t errors = stats.raw_overflows + stats.read_failures + stats.submit_failures + stats.recoveries;
    if (errors == last_errors && gate == last_gate && source == last_source) return;
    last_source = source;
    last_errors = errors;
    last_gate = gate;
    ESP_LOGW("INPUT", "recover=%" PRIu32 " raw_full=%" PRIu32 " read_fail=%" PRIu32
        " send_fail=%" PRIu32 " merged=%" PRIu32 " peak=%" PRIu32 " wait_ms=%" PRIu32
        " link=%u mode=%u recovering=%u all_up=%u releases=%u mode_pending=%u"
        " source_wait_up=%u recovery_ready=%u host_active=%u"
        " source_gen=%" PRIu32 " source_reason=%s output_reason=%s haptic_state=%u",
        stats.recoveries, stats.raw_overflows, stats.read_failures, stats.submit_failures,
        stats.merged, stats.peak, stats.longest_wait_ms,
        (unsigned)link, (unsigned)mode, (unsigned)recovering, (unsigned)all_up,
        (unsigned)releases, (unsigned)changing,
        (unsigned)source_gated, (unsigned)recovery_ready, (unsigned)host_active,
        source, source_why, output_why,
        (unsigned)cs40l25_surface_get_state());
}

void input_request_mode(uint8_t mode)
{
    if (mode != MOUSE_MODE && mode != PTP_MODE) return;
    taskENTER_CRITICAL(&lock);
    if ((mode_pending && requested_mode == mode) || (!mode_pending && mode_applied && mode == reports.mode)) {
        taskEXIT_CRITICAL(&lock);
        return;
    }
    /* Radio/BLE choose their one boot mode before mode_applied. A later mode
     * change is a runtime operation even if no host has acknowledged input. */
    if (!usb_session && mode_applied && mode != reports.mode) startup_pending = false;
    requested_mode = mode; mode_pending = true; ++request_serial;
    mode_retry = false;
    report_buffer_reset(&reports, reports.mode);
    output_wait_up = true;
    output_reason = "mode";
    transition_locked("mode", true);
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
    bool retry_wait = mode_retry && (int32_t)(now_ms() - mode_retry_at) < 0;
    bool ptp = mode == PTP_MODE;
#if CONFIG_PTP_SIMULATED_MOUSE_MODE
    ptp = true;
#endif
    bool write = !physical_mode_valid || physical_ptp != ptp;
    taskEXIT_CRITICAL(&lock);
    if (!pending || retry_wait) return false;
    esp_err_t err = write ? touchpad_mode_set(ptp) : ESP_OK;
    taskENTER_CRITICAL(&lock);
    if (err == ESP_OK) { physical_mode_valid = true; physical_ptp = ptp; }
    else physical_mode_valid = false;
    if (serial == request_serial) {
        if (err == ESP_OK) {
            current_tp_mode = mode;
            report_buffer_reset(&reports, mode);
            mode_applied = true;
            mode_pending = mode_retry = false;
            {
                /* Preserve a lift already observed while applying/retrying the
                 * request, even if a subsequent fresh contact has arrived. */
                bool wait_up = (source_wait_up && (!usb_session || physical_active)) || source_uncertain;
                transition_locked("mode_applied", true);
                source_wait_up = output_wait_up = wait_up;
                reports.recovery_ready = !wait_up;
                reports.recovering = wait_up || reports.release_mask;
            }
        } else {
            mode_applied = false;
            mode_retry = true;
            mode_retry_at = now_ms() + 100U;
        }
    }
    taskEXIT_CRITICAL(&lock);
    if (err != ESP_OK) ESP_LOGW("INPUT", "Mode %u failed: %s", mode, esp_err_to_name(err));
    input_wake_sender();
    return true;
}
