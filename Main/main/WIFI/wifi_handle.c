#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_now.h"

#include "freertos/semphr.h"

#include "SYS/hid_msg.h"
#include "SYS/input_pipeline.h"
#include "I2C/TP/i2c_hid.h"
#include "WIFI/wireless_wifi.h"

#include "sdkconfig.h"
#include "SYS/aux_output.h"
#include "SYS/device_config.h"
#include "SYS/connection.h"
#include "SYS/wireless_extension.h"
#include "esp_random.h"
#include "wireless_settings.h"

#define TAG "WIFI_INIT"

#define ESPNOW_CHANNEL 1
_Static_assert(sizeof(wireless_msg_t) == 38, "ESP-NOW envelope ABI");


uint8_t receiver_mac[6];

void parse_mac_from_config() {
    const char* mac_str = CONFIG_RECEIVER_MAC_ADDR;
    unsigned int values[6];

    if (sscanf(mac_str, "%x:%x:%x:%x:%x:%x",
               &values[0], &values[1], &values[2],
               &values[3], &values[4], &values[5]) == 6) {
        for (int i = 0; i < 6; ++i) {
            receiver_mac[i] = (uint8_t)values[i];
        }
    } else {
        ESP_LOGE("CONFIG", "Invalid MAC address format in Kconfig!");
        uint8_t default_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        memcpy(receiver_mac, default_mac, 6);
    }
}

static portMUX_TYPE send_lock = portMUX_INITIALIZER_UNLOCKED;
static bool send_done;
static wire_surface_t surface;
static uint32_t acknowledged_at;
static bool acknowledged;
static uint8_t acknowledged_peer[6];
void wireless_surface_ack(const uint8_t *mac, const uint8_t *data, unsigned size)
{
    wire_surface_t ack;
    if (!mac || !wire_surface_decode(data, size, WIRE_SURFACE_ACK, &ack)) return;
    const uint8_t broadcast[6] = {255,255,255,255,255,255};
    if (memcmp(receiver_mac, broadcast, 6) && memcmp(receiver_mac, mac, 6)) return;
    taskENTER_CRITICAL(&send_lock);
    bool accept = ack.session == surface.session && ack.rotation == surface.rotation &&
        (!acknowledged || (uint32_t)(esp_timer_get_time()/1000)-acknowledged_at >= 2500U ||
         !memcmp(acknowledged_peer,mac,6));
    if (accept) {
        memcpy(acknowledged_peer,mac,6);
        acknowledged = true; acknowledged_at = (uint32_t)(esp_timer_get_time() / 1000);
    }
    taskEXIT_CRITICAL(&send_lock);
    if (accept) wireless_settings_peer(mac);
    input_wake_sender();
}
static esp_now_send_status_t send_status;

static void send_callback(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    (void)info;
    taskENTER_CRITICAL(&send_lock);
    send_status = status; send_done = true;
    taskEXIT_CRITICAL(&send_lock);
    input_wake_sender();
}

void wireless_wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_now_init());
    esp_now_peer_info_t peer = {0};
    parse_mac_from_config();
    memcpy(peer.peer_addr, receiver_mac, sizeof(receiver_mac));
    peer.channel = ESPNOW_CHANNEL;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    ESP_ERROR_CHECK(esp_now_register_send_cb(send_callback));
    surface = (wire_surface_t){.version = WIRE_VERSION, .rotation = device_config_rotation(), .session = esp_random()};
    if (!surface.session) surface.session = 1;
    ESP_ERROR_CHECK(wireless_settings_init(surface.session));
    wireless_espnow_init();

}

void wifi_send_task(void *arg)
{
    (void)arg; input_register_transport_sender(_2_4_MODE);
    input_report_t pending = {0};
    aux_output_event_t event = {0};
    uint8_t packet[38] = {0};
    uint8_t settings_destination[6];
    bool have_pending = false, in_flight = false, link_ready = false, prefer_aux = true;
    uint32_t input_epoch = 0;
    uint32_t surface_epoch = connection_epoch();
    unsigned kind = 0; /* 0 pointer, 1 heartbeat, 2 surface, 3 action, 4 settings ACK */
    uint32_t heartbeat_at = 0, surface_at = 0, sequence = 0;
    bool first_surface = true;
    while (true) {
        connection_lock();
        if (connection_selected(_2_4_MODE)) input_log_stats();
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        taskENTER_CRITICAL(&send_lock);
        bool done = send_done; esp_now_send_status_t status = send_status; send_done = false;
        bool ready = acknowledged && now - acknowledged_at < 2500;
        taskEXIT_CRITICAL(&send_lock);
        if (ready != link_ready) {
            link_ready = ready; connection_link(_2_4_MODE, ready);
            if (connection_selected(_2_4_MODE)) aux_output_cancel();
        }
        if (in_flight && done) {
            in_flight = false;
            bool ok = status == ESP_NOW_SEND_SUCCESS;
            bool owner = connection_selected(_2_4_MODE) && input_epoch == connection_epoch();
            if (kind == 0 || kind == 3) connection_flight(_2_4_MODE, input_epoch, false);
            if ((kind == 0 || kind == 3) && !owner) { have_pending = false; kind = 1; }
            if (kind == 3) aux_output_event_complete(ok);
            else if (kind == 4) wireless_settings_reply_complete(ok);
            else if (kind == 0) { if (ok) input_report_ack(&pending); have_pending = false; }
            if (!ok) {
                if (connection_selected(_2_4_MODE)) input_submit_failed();
                /* A failed heartbeat/surface announcement has no HID state to release. */
                if (kind == 0 || kind == 3) input_recover();
            }
        }
        if (have_pending && !in_flight && (!connection_can_send(_2_4_MODE) || !input_report_current(&pending))) have_pending = false;
        if (!in_flight) {
            if (surface_epoch != connection_epoch()) {
                /* Retire local ACK/settings state now. The new session is
                 * announced only when 2.4G is selected again. */
                surface_epoch = connection_epoch();
                taskENTER_CRITICAL(&send_lock);
                if (!++surface.session) ++surface.session;
                acknowledged = false;
                taskEXIT_CRITICAL(&send_lock);
                wireless_settings_session(surface.session);
                link_ready = false; connection_link(_2_4_MODE, false);
                first_surface = true; have_pending = false; sequence = 0;
            }
            if (!connection_selected(_2_4_MODE)) {
                /* USB/startup waiting must not emit even control packets.
                 * Route changes wake this task; first_surface starts discovery
                 * immediately on the next 2.4G selection. */
                connection_unlock();
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
                continue;
            }
            bool send = true;
            if (first_surface || now - surface_at >= 1000) {
                kind = 2; wire_surface_encode(packet, WIRE_SURFACE, &surface);
            } else if (now - heartbeat_at >= 1000) {
                kind = 1; wireless_msg_t heartbeat; wireless_make_heartbeat(&heartbeat); memcpy(packet, &heartbeat, 38);
            } else if (wireless_settings_reply(packet,settings_destination)) {
                kind = 4;
            } else {
                bool selected = connection_can_send(_2_4_MODE);
                if (!have_pending && selected) have_pending = input_take_report(&pending);
                bool auxiliary = selected && link_ready && (prefer_aux || !have_pending) && aux_output_take_event(&event, input_generation(), now);
                if (auxiliary) {
                    kind = 3;
                    if (!++sequence) ++sequence;
                    wire_action_t a = {surface.session, sequence, event.action, event.steps, event.hold};
                    wire_action_encode(packet, &a);
                } else if (selected && have_pending && link_ready) {
                    kind = 0; wireless_msg_t data = {0};
                    if (pending.mode == PTP_MODE) { data.type = WIRELESS_HAPTIC_PTP_MODE; data.payload.ptp = pending.data.ptp; }
                    else { data.type = MOUSE_MODE; data.payload.mouse = pending.data.mouse; }
                    memcpy(packet, &data, 38);
                } else send = false;
            }
            if (send) {
                bool current = kind == 0 ? input_report_current(&pending) : kind == 3 ? aux_output_event_current(&event) : true;
                const uint8_t *destination = kind == 4 ? settings_destination : receiver_mac;
                if (kind == 4 && !esp_now_is_peer_exist(destination)) {
                    esp_now_peer_info_t peer = {.channel = ESPNOW_CHANNEL, .ifidx = WIFI_IF_STA};
                    memcpy(peer.peer_addr,destination,6);
                    (void)esp_now_add_peer(&peer);
                }
                esp_err_t err = current ? esp_now_send(destination, packet, sizeof(packet)) : ESP_FAIL;
                if (err == ESP_OK) {
                    in_flight = true;
                    if (kind == 1) heartbeat_at = now;
                    if (kind == 2) { surface_at = now; first_surface = false; }
                    if (kind == 0 || kind == 3) {
                        prefer_aux = kind == 0;
                        input_epoch = connection_epoch();
                        connection_flight(_2_4_MODE, input_epoch, true);
                    }
                } else {
                    if (kind == 3) aux_output_unsubmitted();
                    if (kind == 4) wireless_settings_reply_complete(false);
                    if (current && (kind == 0 || kind == 3)) input_recover();
                    /* Rate-limit rejected control submissions as well as completed ones. */
                    if (kind == 1) heartbeat_at = now;
                    if (kind == 2) { surface_at = now; first_surface = false; }
                    if (connection_selected(_2_4_MODE)) input_submit_failed();
                }
            }
        }
        connection_unlock();
        ulTaskNotifyTake(pdTRUE, 1);
    }
}
