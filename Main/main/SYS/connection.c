#include "connection.h"
#include "connection_policy.h"
#include "device_config.h"
#include "input_pipeline.h"
#include "aux_output.h"
#include "wireless_probe.h"
#include "GPIO/GPIO_handle.h"
#include "NVS/nvs_handle.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"

static SemaphoreHandle_t route_mutex;
static connection_policy_t policy;
static int route = -1;
static bool links[3], retiring;
static unsigned flights[3];
static uint32_t route_epoch = 1, sampled_at;
static uint8_t usb_mode = MOUSE_MODE;
static portMUX_TYPE event_lock = portMUX_INITIALIZER_UNLOCKED;
static bool config_wait, wake_event;
static wireless_probe_t probe;
static bool probe_allowed, probe_waiting, boot_wait_up, restarted;
static uint32_t retiring_at;
static int retiring_target;
static RTC_NOINIT_ATTR struct {
    uint32_t magic, inverse;
    int mode;
    uint32_t high, reason, wait_up;
} handoff;
#define HANDOFF_MAGIC 0x52535031U
enum { HANDOFF_MANUAL = 1, HANDOFF_AUTO = 2 };

static void connection_handoff(int mode, unsigned reason, bool wait_up)
{
    handoff.mode = mode;
    handoff.high = policy.valid ? policy.stable : gpio_get_level(VBUS_DET_GPIO) != 0;
    handoff.reason = reason; handoff.wait_up = wait_up;
    handoff.inverse = ~HANDOFF_MAGIC; handoff.magic = HANDOFF_MAGIC;
}

void connection_lock(void) { if (route_mutex) xSemaphoreTake(route_mutex, portMAX_DELAY); }
void connection_unlock(void) { if (route_mutex) xSemaphoreGive(route_mutex); }
bool connection_selected(int transport) { return route_mutex ? route == transport : current_mode == transport; }
bool connection_can_send(int transport)
{
    return connection_selected(transport) && (!route_mutex || (links[transport] &&
        (transport != WIRED_MODE || gpio_get_level(VBUS_DET_GPIO))));
}
uint32_t connection_epoch(void) { return route_epoch; }
int connection_mode(void)
{
    connection_lock();
    int selected = policy.target >= 0 ? policy.target : current_mode;
    connection_unlock(); return selected;
}
void connection_flight(int transport, uint32_t epoch, bool submitted)
{
    if (epoch != route_epoch || !connection_selected(transport)) return;
    if (submitted) ++flights[transport];
    else if (flights[transport]) --flights[transport];
}
void connection_init(int boot)
{
    route_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(route_mutex ? ESP_OK : ESP_ERR_NO_MEM);
    bool enabled = (device_config_value(CFG_FEATURE_FLAGS) & CFG_FLAG_AUTO_SWITCH) != 0;
    bool resume = esp_reset_reason() == ESP_RST_SW && handoff.magic == HANDOFF_MAGIC &&
        handoff.inverse == ~HANDOFF_MAGIC && handoff.mode >= WIRED_MODE && handoff.mode <= BLE_MODE &&
        handoff.high <= 1 && handoff.wait_up <= 1 &&
        (handoff.reason == HANDOFF_AUTO || (handoff.reason == HANDOFF_MANUAL && handoff.mode == boot));
    bool manual = resume && handoff.reason == HANDOFF_MANUAL;
    bool high = handoff.high == 1;
    boot_wait_up = resume && handoff.wait_up;
    if (resume) boot = handoff.mode;
    else if (enabled) boot = WIRED_MODE; /* Discovery before starting BLE on a cold boot. */
    handoff.magic = handoff.inverse = 0;
    current_mode = boot;
    connection_policy_init(&policy, boot, enabled, manual || (!enabled && boot == BLE_MODE), high);
    if (resume && !manual) { policy.target = boot; policy.choose = false; }
    route = -1; retiring = restarted = probe_allowed = probe_waiting = false;
    probe = (wireless_probe_t){0};
    sampled_at = (uint32_t)(esp_timer_get_time() / 1000) - 10U;
}

void connection_probe_ready(uint32_t session)
{
    taskENTER_CRITICAL(&event_lock);
    probe = (wireless_probe_t){.token = {.session = session ? session : 1}};
    taskEXIT_CRITICAL(&event_lock);
    input_wake_parser();
}
bool connection_probe_packet(uint8_t packet[38])
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    taskENTER_CRITICAL(&event_lock);
    bool send = probe_allowed && wireless_probe_packet(&probe, now, packet);
    taskEXIT_CRITICAL(&event_lock);
    return send;
}
void connection_probe_receive(const uint8_t *data, unsigned size)
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    taskENTER_CRITICAL(&event_lock);
    bool accepted = probe_allowed && wireless_probe_ack(&probe, data, size, now);
    taskEXIT_CRITICAL(&event_lock);
    if (accepted) input_wake_parser();
}
void connection_link(int transport, bool ready)
{
    /* Called with connection_lock held by the transport. */
    if (route_mutex && transport == WIRED_MODE && !gpio_get_level(VBUS_DET_GPIO)) ready = false;
    links[transport] = ready;
    if (connection_selected(transport)) {
        if (transport == WIRED_MODE) input_usb_link(ready);
        else input_set_link(ready ? (transport == BLE_MODE ? 1 : 3) : 0);
    }
    input_wake_parser();
}
void connection_usb_reset(void)
{
    links[WIRED_MODE] = false; usb_mode = MOUSE_MODE;
    flights[WIRED_MODE] = 0;
    if (connection_selected(WIRED_MODE)) { input_usb_reset(); aux_output_reset(false); }
    input_wake_parser();
}
void connection_usb_mode(uint8_t mode)
{
    usb_mode = mode;
    if (connection_selected(WIRED_MODE) && !retiring) input_request_mode(mode);
}
void connection_config_pending(void)
{ taskENTER_CRITICAL(&event_lock); config_wait = true; taskEXIT_CRITICAL(&event_lock); }
void connection_config_complete(void)
{
    taskENTER_CRITICAL(&event_lock); config_wait = false; taskEXIT_CRITICAL(&event_lock);
    input_wake_parser();
}
void connection_wake(void)
{
    taskENTER_CRITICAL(&event_lock); wake_event = true; taskEXIT_CRITICAL(&event_lock);
    input_wake_parser();
}
void connection_poll(void)
{
    if (!route_mutex || restarted) return;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if ((uint32_t)(now - sampled_at) < 10U) return;
    sampled_at = now;
    connection_lock();
    taskENTER_CRITICAL(&event_lock);
    bool wait = config_wait, wake = wake_event; wake_event = false;
    taskEXIT_CRITICAL(&event_lock);
    if (wake) connection_policy_wake(&policy);
    if (!wait) {
        bool enabled = (device_config_value(CFG_FEATURE_FLAGS) & CFG_FLAG_AUTO_SWITCH) != 0;
        if (!enabled && policy.enabled) policy.target = route >= 0 ? route : current_mode;
        connection_policy_enable(&policy, enabled);
    }
    bool high = gpio_get_level(VBUS_DET_GPIO) != 0;
    if (!high && links[WIRED_MODE]) connection_link(WIRED_MODE, false);
    int target = connection_policy_sample(&policy, high, now);
    /* Probe state is independent of the input/session ACK, and callbacks only
     * touch it under event_lock. Never wait for radio completion here. */
    taskENTER_CRITICAL(&event_lock);
    bool discover = !wait && policy.enabled && target == CONNECTION_PROBE;
    probe_allowed = !wait && policy.enabled && !policy.ble && !retiring &&
        (discover || route == WIRED_MODE);
    if (!probe_allowed) {
        probe.started = probe.seen = false; probe_waiting = false;
    } else if (discover) {
        if (wireless_probe_fresh(&probe, now)) {
            target = policy.target = _2_4_MODE; probe_waiting = false;
        } else if (probe.token.session) {
            if (!probe_waiting) { wireless_probe_begin(&probe, now); probe_waiting = true; }
            if ((uint32_t)(now - probe.started_at) >= PROBE_WINDOW_MS) {
                target = policy.target = BLE_MODE; probe_waiting = false;
            }
        }
    } else {
        probe_waiting = false;
        if (probe.token.session && (!probe.started || (uint32_t)(now - probe.started_at) >= PROBE_PERIOD_MS))
            wireless_probe_begin(&probe, now);
    }
    taskEXIT_CRITICAL(&event_lock);
    /* An auto-switch flag write takes effect only after its response completes. */
    if (wait) { connection_unlock(); return; }
    if (retiring && (target != retiring_target || !policy.enabled)) {
        retiring = false;
        input_transport_start(route, route == WIRED_MODE ? usb_mode : route == BLE_MODE ? MOUSE_MODE : PTP_MODE,
                              links[route], false);
    }
    if (target >= 0 && (route != target || retiring)) {
        if (route < 0 && target == current_mode) {
            ++route_epoch;
            for (unsigned i = 0; i < 3; ++i) flights[i] = 0;
            route = target;
            aux_output_reset(route == WIRED_MODE && links[route]);
            input_transport_start(route, route == WIRED_MODE ? usb_mode : route == BLE_MODE ? MOUSE_MODE : PTP_MODE,
                                  links[route], !boot_wait_up);
        } else {
            if (!retiring) {
                retiring = true; retiring_at = now; retiring_target = target;
                input_transport_quiesce(); aux_output_cancel();
            }
            /* BLE mouse readiness does not cover keyboard/Consumer-only
             * subscriptions: their accepted keys must still be released. */
            bool drained = route < 0 || (!links[route] &&
                (route != BLE_MODE || aux_output_drained(false))) ||
                (!flights[route] && input_transport_drained() && aux_output_drained(route == _2_4_MODE));
            if (drained || (uint32_t)(now - retiring_at) >= 250U) {
                connection_handoff(target, HANDOFF_AUTO, route >= 0 || boot_wait_up);
                restarted = true;
                ESP_LOGI("CONNECTION", "Restart mode=%d -> %d VBUS=%u receiver=%u", current_mode, target,
                         policy.stable, target == _2_4_MODE);
            }
        }
    }
    connection_unlock();
    if (restarted) esp_restart();
}
esp_err_t connection_manual_restart(int mode)
{
    if (mode < WIRED_MODE || mode > BLE_MODE) return ESP_ERR_INVALID_ARG;
    esp_err_t err = nvs_write_int("current_mode", mode);
    if (err != ESP_OK) return err;
    connection_lock();
    connection_handoff(mode, HANDOFF_MANUAL, true);
    connection_unlock();
    return ESP_OK;
}
