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
#include "I2C/TP/i2c_hid.h"
#include "WIFI/wireless_wifi.h"

#include "sdkconfig.h"

#define TAG "WIFI_INIT"

#define ESPNOW_CHANNEL 1

wireless_msg_t pkt = {0};

uint8_t receiver_mac[6];

void parse_mac_from_config() {
    const char* mac_str = CONFIG_RECEIVER_MAC_ADDR;
    int values[6];

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

TaskHandle_t xHeartbeatTaskHandle = NULL;

static SemaphoreHandle_t vbus_sem = NULL;

extern bool stop_heartbeat;

void wireless_wifi_init(void) {

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_now_init());

    esp_now_peer_info_t peer = {};

    parse_mac_from_config();

    memcpy(peer.peer_addr, receiver_mac, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    vbus_sem = xSemaphoreCreateBinary();
    
    
    esp_now_send(receiver_mac, (uint8_t *)&pkt, sizeof(pkt));

    xTaskCreatePinnedToCore(alive_heartbeat_task, "alive_heartbeat_task", 2048, NULL, 5, NULL, 0);
    stop_heartbeat = false;

    wireless_espnow_init();

}

void wifi_send_task(void *arg) {
    tp_multi_msg_t tp_msg;
    mouse_msg_t mouse_msg;

    while (1) {
        QueueSetMemberHandle_t xActivatedMember = xQueueSelectFromSet(main_queue_set, portMAX_DELAY);

        if (xActivatedMember == mouse_queue) {
            #if CONFIG_ORI_MOUSE_MODE
                if (xQueueReceive(mouse_queue, &mouse_msg, 0)) {
                    mouse_hid_report_t report = {0};
                    
                    parse_mouse_report(&mouse_msg, &report);

                    pkt.type = MOUSE_MODE;
                    pkt.payload.mouse = report;
                    esp_now_send(receiver_mac, (uint8_t*)&pkt, sizeof(pkt));
                    
                }
            #endif
        } 
        else if (xActivatedMember == tp_queue) {
            if (xQueueReceive(tp_queue, &tp_msg, 0)) {
                ptp_report_t report = {0};
                
                parse_ptp_report(&tp_msg, &report);

                pkt.type = PTP_MODE;
                pkt.payload.ptp = report;
                esp_now_send(receiver_mac, (uint8_t*)&pkt, sizeof(pkt));
                
            }
        }
    }
}
