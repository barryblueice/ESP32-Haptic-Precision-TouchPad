#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "sdkconfig.h"

#include "wireless/wireless.h"

QueueHandle_t legacy_tp_queue = NULL;
QueueHandle_t haptic_tp_queue = NULL;
QueueHandle_t mouse_queue = NULL;
QueueSetHandle_t main_queue_set = NULL;
volatile uint8_t current_mode = MOUSE_MODE;

static const char *TAG = "WIFI_QUENE";

static void wifi_now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (len < sizeof(wireless_input_mode_t)) return;

    wireless_msg_t *msg = (wireless_msg_t *)data;

    switch (msg->type) {
        case MOUSE_MODE:
            if (len >= sizeof(wireless_input_mode_t) + sizeof(mouse_hid_report_t)) {
                xQueueSend(mouse_queue, &msg->payload.mouse, 0);
            }
            break;

        case LEGACY_PTP_MODE:
            if (len >= sizeof(wireless_input_mode_t) + sizeof(legacy_ptp_report_t)) {
                xQueueSend(legacy_tp_queue, &msg->payload.legacy_ptp, 0);
            }
            break;
        case HAPTIC_PTP_MODE:
            if (len >= sizeof(wireless_input_mode_t) + sizeof(haptic_ptp_report_t)) {
                xQueueSend(haptic_tp_queue, &msg->payload.haptic_ptp, 0);
            }
            break;

        case VBUS_STATUS:

            gpio_set_level(GPIO_NUM_9, msg->payload.vbus.vbus_level);
            // ESP_DRAM_LOGI(TAG, "Remote VBUS Level: %d", msg->payload.vbus.vbus_level);
            break;

        case ALIVE_MODE:

            // ESP_DRAM_LOGI(TAG,"LAST_SEEN_TIMESTAMP before: %u", last_seen_timestamp);
            last_seen_timestamp = xTaskGetTickCount();
            gpio_set_level(GPIO_NUM_9, msg->payload.alive.vbus_level);
            if (msg->payload.alive.vbus_level == 0) {
                ESP_LOGI(TAG, "Device online, sending current mode: %d", current_mode);
                esp_now_send(broadcast_mac, (const uint8_t *)&current_mode, 1);
            }
            break;

        default:
            break;
    }
}

void wifi_recieve_task_init() {

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT_OD,
        .pin_bit_mask = (1ULL << GPIO_NUM_9),
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(GPIO_NUM_9, 1);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(wifi_now_recv_cb));
}