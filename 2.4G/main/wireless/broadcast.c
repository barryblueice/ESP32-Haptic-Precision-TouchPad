#include "wireless/receiver_extension.h"
#include "wireless/receiver_settings.h"
#include "wireless.h"
#include "input/input_pipeline.h"
#include "esp_now.h"
#include "esp_log.h"
#include <string.h>

const uint8_t broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static portMUX_TYPE control_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t worker;
static uint32_t requested_serial, flight_serial, retry_at, control_failures;
static bool control_pending, control_in_flight, control_done, control_success, retry_wait;
static uint8_t flight_mode;
static bool flight_ack, flight_settings;
static uint8_t ack_packet[38], ack_mac[6];
enum { PROBE_REPLY_CAPACITY = 8 };
typedef struct {
    uint8_t mac[6];
    wire_probe_t token;
    uint32_t received_at;
} probe_reply_t;
static probe_reply_t probe_replies[PROBE_REPLY_CAPACITY];
static unsigned probe_head, probe_count;
static bool flight_probe, probe_peer_temporary, prefer_probe = true;
static uint32_t probe_failures;

bool wireless_probe_enqueue(const uint8_t mac[6], const wire_probe_t *token, uint32_t now)
{
    taskENTER_CRITICAL(&control_lock);
    bool queued = probe_count < PROBE_REPLY_CAPACITY;
    if (queued) {
        probe_reply_t *reply = &probe_replies[(probe_head + probe_count++) % PROBE_REPLY_CAPACITY];
        memcpy(reply->mac, mac, 6);
        reply->token = *token;
        reply->received_at = now;
    }
    taskEXIT_CRITICAL(&control_lock);
    if (queued) wireless_wake_worker();
    return queued;
}

/* Only the worker owns radio peers; never call the SDK inside control_lock. */
static void probe_peer_release(void)
{
    if (probe_peer_temporary) {
        if (esp_now_del_peer(ack_mac) != ESP_OK) ++probe_failures;
        probe_peer_temporary = false;
    }
}

void wireless_register_worker(void)
{
    taskENTER_CRITICAL(&control_lock);
    worker = xTaskGetCurrentTaskHandle();
    taskEXIT_CRITICAL(&control_lock);
}
void wireless_wake_worker(void)
{
    taskENTER_CRITICAL(&control_lock);
    TaskHandle_t task = worker;
    taskEXIT_CRITICAL(&control_lock);
    if (task) xTaskNotifyGive(task);
}
void wireless_request_mode(void)
{
    taskENTER_CRITICAL(&control_lock);
    ++requested_serial;
    control_pending = true;
    taskEXIT_CRITICAL(&control_lock);
    wireless_wake_worker();
}
static void mode_send_complete(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    (void)info;
    taskENTER_CRITICAL(&control_lock);
    control_done = true;
    control_success = status == ESP_NOW_SEND_SUCCESS;
    taskEXIT_CRITICAL(&control_lock);
    wireless_wake_worker();
}
void wireless_control_step(uint32_t now)
{
    /* Only the wireless worker submits; the byte remains stable until callback. */
    bool submit = false, probe_completed = false;
    taskENTER_CRITICAL(&control_lock);
    if (control_in_flight && control_done) {
        control_done = false;
        control_in_flight = false;
        if (flight_probe) {
            probe_completed = true;
            if (!control_success) ++probe_failures;
        } else if (flight_settings) {
            if (!control_success) ++control_failures;
        } else if (flight_ack) {
            receiver_ext_ack_complete(control_success);
            if (!control_success) { ++control_failures; retry_at = now; retry_wait = true; }
        } else if (control_success) {
            if (flight_serial == requested_serial) control_pending = false;
        } else {
            ++control_failures;
            retry_at = now;
            retry_wait = true;
        }
    }
    taskEXIT_CRITICAL(&control_lock);
    if (probe_completed) probe_peer_release();

    taskENTER_CRITICAL(&control_lock);
    if (!control_in_flight) {
        /* A callback can enqueue after the caller sampled now. Treat that
         * small negative age as fresh, including across the clock wrap. */
        while (probe_count && (int32_t)(now - probe_replies[probe_head].received_at) >= PROBE_WINDOW_MS) {
            probe_head = (probe_head + 1) % PROBE_REPLY_CAPACITY;
            --probe_count;
        }
        bool probe = probe_count && prefer_probe;
        bool ack = false, settings = false, ordinary = false;
        if (!probe && (!retry_wait || (uint32_t)(now - retry_at) >= 20U)) {
            ack = receiver_ext_ack(ack_packet, ack_mac);
            settings = !control_pending && !ack && receiver_settings_next(ack_packet, ack_mac, now);
            ordinary = control_pending || ack || settings;
        }
        if (!ordinary && probe_count) probe = true;
        if (probe || ordinary) {
            if (probe) {
                const probe_reply_t *reply = &probe_replies[probe_head];
                memcpy(ack_mac, reply->mac, 6);
                wire_probe_encode(ack_packet, WIRE_PROBE_ACK, &reply->token);
                probe_head = (probe_head + 1) % PROBE_REPLY_CAPACITY;
                --probe_count;
            } else {
                retry_wait = false;
            }
            flight_serial = requested_serial;
            flight_ack = ack; flight_settings = settings; flight_probe = probe;
            prefer_probe = !probe;
            control_in_flight = true;
            control_done = false;
            submit = true;
        }
    }
    taskEXIT_CRITICAL(&control_lock);
    if (submit) {
        flight_mode = (uint8_t)input_mode();
        bool unicast = flight_ack || flight_settings || flight_probe;
        esp_err_t result = ESP_OK;
        if (unicast && !esp_now_is_peer_exist(ack_mac)) {
            esp_now_peer_info_t peer = {.channel = ESPNOW_CHANNEL, .ifidx = WIFI_IF_STA};
            memcpy(peer.peer_addr,ack_mac,6);
            esp_err_t added = esp_now_add_peer(&peer);
            if (flight_probe) {
                probe_peer_temporary = added == ESP_OK;
                result = added;
            }
        }
        if (result == ESP_OK) {
            result = esp_now_send(unicast ? ack_mac : broadcast_mac,
                                  unicast ? ack_packet : &flight_mode, unicast ? 38 : 1);
        }
        if (result != ESP_OK) {
            taskENTER_CRITICAL(&control_lock);
            control_in_flight = false;
            if (flight_probe) {
                ++probe_failures;
            } else {
                ++control_failures;
                retry_at = now;
                retry_wait = true;
            }
            taskEXIT_CRITICAL(&control_lock);
            if (flight_probe) probe_peer_release();
        }
    }
    static uint32_t logged_at, logged_failures;
    if ((uint32_t)(now - logged_at) >= 5000U && control_failures != logged_failures) {
        ESP_LOGW("WIRELESS", "Mode command failures: %lu", (unsigned long)control_failures);
        logged_at = now;
        logged_failures = control_failures;
    }
    static uint32_t probe_logged_at, probe_logged_failures;
    if ((uint32_t)(now - probe_logged_at) >= 5000U && probe_failures != probe_logged_failures) {
        ESP_LOGW("WIRELESS", "Probe reply failures: %lu", (unsigned long)probe_failures);
        probe_logged_at = now;
        probe_logged_failures = probe_failures;
    }
}
void broadcast_init(void)
{
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, broadcast_mac, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    if (!esp_now_is_peer_exist(broadcast_mac)) ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    ESP_ERROR_CHECK(esp_now_register_send_cb(mode_send_complete));
}
