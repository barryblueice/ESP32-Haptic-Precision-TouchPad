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
    bool submit = false;
    taskENTER_CRITICAL(&control_lock);
    if (control_in_flight && control_done) {
        control_done = false;
        control_in_flight = false;
        if (flight_settings) {
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
    bool ack = !control_in_flight && receiver_ext_ack(ack_packet,ack_mac);
    bool settings = !control_in_flight && !control_pending && !ack &&
        (!retry_wait || (uint32_t)(now-retry_at) >= 20U) && receiver_settings_next(ack_packet,ack_mac,now);
    if (!control_in_flight && (control_pending || ack || settings) && (!retry_wait || (uint32_t)(now - retry_at) >= 20U)) {
        flight_serial = requested_serial;
        flight_ack = ack; flight_settings = settings;
        control_in_flight = true;
        control_done = false;
        retry_wait = false;
        submit = true;
    }
    taskEXIT_CRITICAL(&control_lock);
    if (submit) {
        flight_mode = (uint8_t)input_mode();
        if ((flight_ack || flight_settings) && !esp_now_is_peer_exist(ack_mac)) {
            esp_now_peer_info_t peer = {.channel = ESPNOW_CHANNEL, .ifidx = WIFI_IF_STA};
            memcpy(peer.peer_addr,ack_mac,6);
            (void)esp_now_add_peer(&peer);
        }
        if (esp_now_send((flight_ack || flight_settings) ? ack_mac : broadcast_mac,
                         (flight_ack || flight_settings) ? ack_packet : &flight_mode, (flight_ack || flight_settings) ? 38 : 1) != ESP_OK) {
            taskENTER_CRITICAL(&control_lock);
            control_in_flight = false;
            ++control_failures;
            retry_at = now;
            retry_wait = true;
            taskEXIT_CRITICAL(&control_lock);
        }
    }
    static uint32_t logged_at, logged_failures;
    if ((uint32_t)(now - logged_at) >= 5000U && control_failures != logged_failures) {
        ESP_LOGW("WIRELESS", "Mode command failures: %lu", (unsigned long)control_failures);
        logged_at = now;
        logged_failures = control_failures;
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
