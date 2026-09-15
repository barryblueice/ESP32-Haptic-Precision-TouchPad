#include "wireless_settings.h"
#include "SYS/device_config.h"
#include "SYS/input_pipeline.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>

static portMUX_TYPE settings_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t settings_worker;
static uint32_t settings_session, peer_seen;
static uint8_t settings_peer[6];
static bool peer_valid, request_pending, completed_valid, reply_pending, reply_attempted;
static wire_settings_t newest, completed_request, completed_reply, reply;
static uint32_t reply_serial, reply_flight, reply_at;
static uint32_t settings_now(void) { return (uint32_t)(esp_timer_get_time()/1000); }

void wireless_settings_peer(const uint8_t mac[6])
{
    taskENTER_CRITICAL(&settings_lock);
    if (peer_valid && memcmp(settings_peer,mac,6)) {
        request_pending = completed_valid = reply_pending = false;
        newest = (wire_settings_t){0};
    }
    memcpy(settings_peer,mac,6); peer_valid = true; peer_seen = settings_now();
    taskEXIT_CRITICAL(&settings_lock);
}

void wireless_settings_receive(const uint8_t *mac, const uint8_t *packet, unsigned size)
{
    wire_settings_t request;
    if (!mac || !wire_settings_decode(packet,size,WIRE_SETTINGS,&request)) return;
    taskENTER_CRITICAL(&settings_lock);
    bool accept = settings_worker && peer_valid && !memcmp(mac,settings_peer,6) &&
        settings_now()-peer_seen < 2500U && request.session == settings_session;
    /* A new receiver boot establishes its nonce with a read before any write. */
    if (accept && request.client != newest.client && request.mask) accept = false;
    if (accept && request.client == newest.client && newest.sequence) {
        int32_t order = (int32_t)(request.sequence-newest.sequence);
        if (order < 0 || (order == 0 && (request.mask != newest.mask ||
            request.intensity != newest.intensity || request.level != newest.level))) accept = false;
    }
    if (accept) { newest = request; request_pending = true; }
    TaskHandle_t task = settings_worker;
    taskEXIT_CRITICAL(&settings_lock);
    if (accept) xTaskNotifyGive(task);
}

/* Storage waits for the input parser; never run this in the Wi-Fi callback/sender. */
static void wireless_settings_process(void)
{
    taskENTER_CRITICAL(&settings_lock);
    bool pending = request_pending && peer_valid && settings_now()-peer_seen < 2500U;
    wire_settings_t request = newest;
    uint8_t peer[6]; memcpy(peer,settings_peer,6);
    request_pending = false;
    bool duplicate = completed_valid && request.client == completed_request.client &&
        request.sequence == completed_request.sequence && completed_reply.status != WIRE_SETTINGS_BUSY;
    wire_settings_t result = duplicate ? completed_reply : request;
    taskEXIT_CRITICAL(&settings_lock);
    if (!pending) return;
    if (!duplicate) {
        result.status = WIRE_SETTINGS_OK;
        device_config_t config; device_config_get(&config);
        uint8_t changed = request.mask;
        if (request.intensity == config.bytes[CFG_INTENSITY]) changed &= ~1U;
        if (request.level == config.bytes[CFG_LEVEL]) changed &= ~2U;
        if (changed) {
            if ((device_config_capabilities() & changed) != changed) result.status = WIRE_SETTINGS_UNSUPPORTED;
            else {
                esp_err_t err = device_config_set_controls(changed,request.intensity,request.level,true);
                if (err != ESP_OK) result.status = err == ESP_ERR_INVALID_STATE ? WIRE_SETTINGS_BUSY : WIRE_SETTINGS_STORAGE;
            }
        }
        device_config_get(&config);
        result.intensity = config.bytes[CFG_INTENSITY]; result.level = config.bytes[CFG_LEVEL];
        if (request.mask) ESP_LOGI("WIFI_SETTINGS", "seq=%lu mask=%u strength=%u level=%u status=%u",
            (unsigned long)request.sequence,request.mask,result.intensity,result.level,result.status);
    }
    taskENTER_CRITICAL(&settings_lock);
    if (peer_valid && !memcmp(peer,settings_peer,6) && request.client == newest.client) {
        completed_request = request; completed_reply = result; completed_valid = true;
        reply = result; reply_pending = true; ++reply_serial;
    }
    taskEXIT_CRITICAL(&settings_lock);
    input_wake_sender();
}

static void settings_task(void *arg)
{
    (void)arg;
    while (true) { ulTaskNotifyTake(pdTRUE,portMAX_DELAY); wireless_settings_process(); }
}

esp_err_t wireless_settings_init(uint32_t session)
{
    settings_session = session;
    return xTaskCreate(settings_task,"wifi_settings",4096,NULL,5,&settings_worker) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

bool wireless_settings_reply(uint8_t packet[38], uint8_t mac[6])
{
    taskENTER_CRITICAL(&settings_lock);
    uint32_t now = settings_now();
    bool available = reply_pending && peer_valid && now-peer_seen < 2500U &&
        (!reply_attempted || now-reply_at >= 20U);
    if (available) {
        wire_settings_encode(packet,WIRE_SETTINGS_ACK,&reply); memcpy(mac,settings_peer,6);
        reply_flight = reply_serial; reply_at = now; reply_attempted = true;
    }
    taskEXIT_CRITICAL(&settings_lock);
    return available;
}

void wireless_settings_reply_complete(bool success)
{
    taskENTER_CRITICAL(&settings_lock);
    if (success && reply_flight == reply_serial) reply_pending = false;
    taskEXIT_CRITICAL(&settings_lock);
}
