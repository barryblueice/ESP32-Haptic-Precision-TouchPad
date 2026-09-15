#include "wireless/receiver_settings.h"
#include "wireless/receiver_extension.h"
#include "wireless/wireless.h"
#include "freertos/FreeRTOS.h"
#include "esp_random.h"
#include "esp_log.h"

static portMUX_TYPE receiver_settings_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t actual_intensity = 63, actual_level = 2;
static uint8_t wanted_intensity, wanted_level, dirty;
static uint32_t revision[2], transaction_revision[2];
static wire_settings_t transaction;
static uint8_t target_mac[6];
static uint32_t target_session, client_nonce, next_sequence, attempted_at, queried_at;
static bool transaction_pending, settings_synced;

uint8_t receiver_settings_get(uint8_t field)
{
    taskENTER_CRITICAL(&receiver_settings_lock);
    uint8_t value = field == WIRE_SETTING_INTENSITY ? actual_intensity : actual_level;
    taskEXIT_CRITICAL(&receiver_settings_lock); return value;
}
void receiver_settings_set(uint8_t field, uint8_t value)
{
    if ((field != WIRE_SETTING_INTENSITY && field != WIRE_SETTING_LEVEL) ||
        (field == WIRE_SETTING_INTENSITY ? value > 100 : value < 1 || value > 3)) return;
    taskENTER_CRITICAL(&receiver_settings_lock);
    if (field == WIRE_SETTING_INTENSITY) { wanted_intensity=value; ++revision[0]; }
    else { wanted_level=value; ++revision[1]; }
    dirty |= field;
    taskEXIT_CRITICAL(&receiver_settings_lock);
    wireless_wake_worker();
}
bool receiver_settings_next(uint8_t packet[38], uint8_t mac[6], uint32_t now)
{
    uint8_t target[6]; uint32_t session;
    bool live = receiver_ext_target(target,&session,now);
    taskENTER_CRITICAL(&receiver_settings_lock);
    if (!live) {
        settings_synced = transaction_pending = false;
        taskEXIT_CRITICAL(&receiver_settings_lock); return false;
    }
    if (target_session != session || memcmp(target_mac,target,6)) {
        memcpy(target_mac,target,6); target_session=session;
        settings_synced = transaction_pending = false;
    }
    bool send = false;
    if (transaction_pending) send = now-attempted_at >= 250U;
    else if (!settings_synced || dirty || now-queried_at >= 1000U) {
        if (!client_nonce) { client_nonce=esp_random(); if (!client_nonce) client_nonce=1; }
        if (!++next_sequence) ++next_sequence;
        transaction=(wire_settings_t){.session=session,.client=client_nonce,.sequence=next_sequence,
            .mask=settings_synced ? dirty : 0};
        if (transaction.mask & 1) transaction.intensity=wanted_intensity;
        if (transaction.mask & 2) transaction.level=wanted_level;
        memcpy(transaction_revision,revision,sizeof(revision));
        transaction_pending=send=true;
    }
    if (send) {
        wire_settings_encode(packet,WIRE_SETTINGS,&transaction); memcpy(mac,target_mac,6);
        attempted_at=now;
    }
    taskEXIT_CRITICAL(&receiver_settings_lock); return send;
}
void receiver_settings_receive(const uint8_t mac[6], const uint8_t packet[38], uint32_t now)
{
    wire_settings_t result;
    uint8_t target[6]; uint32_t session;
    if (!wire_settings_decode(packet,38,WIRE_SETTINGS_ACK,&result) ||
        !receiver_ext_target(target,&session,now) || memcmp(mac,target,6)) return;
    taskENTER_CRITICAL(&receiver_settings_lock);
    bool accept = transaction_pending && session == target_session && result.session == transaction.session &&
        !memcmp(mac,target_mac,6) && result.client == transaction.client &&
        result.sequence == transaction.sequence && result.mask == transaction.mask;
    if (accept) {
        actual_intensity=result.intensity; actual_level=result.level;
        if (result.status != WIRE_SETTINGS_BUSY) {
            for (unsigned i=0;i<2;++i)
                if ((transaction.mask & (1U<<i)) && revision[i] == transaction_revision[i]) dirty &= ~(1U<<i);
            transaction_pending=false; settings_synced=true; queried_at=now;
        }
    }
    taskEXIT_CRITICAL(&receiver_settings_lock);
    if (accept && result.status && result.status != WIRE_SETTINGS_BUSY)
        ESP_LOGW("SETTINGS","Setting not applied: status=%u strength=%u level=%u",
                 result.status,result.intensity,result.level);
    if (accept) wireless_wake_worker();
}
