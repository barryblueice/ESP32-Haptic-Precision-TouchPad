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

#define TAG "WIFI_INIT"

#define ESPNOW_CHANNEL 1


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
    wireless_espnow_init();
    input_set_link(3);
    input_request_mode(PTP_MODE);
}

void wifi_send_task(void *arg)
{
    (void)arg;
    input_register_sender();
    input_report_t pending = {0};
    wireless_msg_t packet = {0};
    bool have_pending = false, in_flight = false, heartbeat = false;
    uint32_t heartbeat_at = 0;
    while (true) {
        input_log_stats();
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        taskENTER_CRITICAL(&send_lock);
        bool done = send_done;
        esp_now_send_status_t status = send_status;
        send_done = false;
        taskEXIT_CRITICAL(&send_lock);
        if (in_flight && done) {
            in_flight = false;
            if (status == ESP_NOW_SEND_SUCCESS) {
                if (!heartbeat) input_report_ack(&pending);
            } else {
                input_submit_failed();
                if (heartbeat || input_report_current(&pending)) input_recover();
            }
            if (!heartbeat) have_pending = false;
        }
        if (have_pending && !in_flight && !input_report_current(&pending)) have_pending = false;
        if (!in_flight) {
            heartbeat = (uint32_t)(now - heartbeat_at) >= 1000U;
            if (!have_pending) have_pending = input_take_report(&pending);
            if (heartbeat || have_pending) {
                packet = (wireless_msg_t){0};
                if (heartbeat) wireless_make_heartbeat(&packet);
                else if (pending.mode == PTP_MODE) {
                    packet.type = WIRELESS_HAPTIC_PTP_MODE;
                    packet.payload.ptp = pending.data.ptp;
                } else {
                    packet.type = MOUSE_MODE;
                    packet.payload.mouse = pending.data.mouse;
                }
                esp_err_t err = esp_now_send(receiver_mac, (uint8_t *)&packet, sizeof(packet));
                if (err == ESP_OK) {
                    in_flight = true;
                    if (heartbeat) heartbeat_at = now;
                } else {
                    input_submit_failed();
                    vTaskDelay(1);
                }
            }
        }
        /* The SDK callback retires each packet before this buffer is reused. */
        ulTaskNotifyTake(pdTRUE, 1);
    }
}
