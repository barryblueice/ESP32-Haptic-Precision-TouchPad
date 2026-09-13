#include "wireless.h"
#include <stddef.h>
#include "input/input_pipeline.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

typedef struct {
    wireless_msg_t packet;
    uint32_t generation, time_ms;
    TickType_t tick;
} receive_frame_t;
static QueueHandle_t receive_queue;

static void wifi_now_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    receive_frame_t frame = {.generation = input_generation(), .time_ms = input_now_ms(),
                             .tick = xTaskGetTickCount()};
    if (!info || !wireless_decode(data, len, &frame.packet)) {
        input_invalid_packet();
        return;
    }
    if (xQueueSend(receive_queue, &frame, 0) != pdPASS) input_rx_overflow();
    wireless_wake_worker();
}

static void wireless_receive_step(void)
{
    receive_frame_t frame;
    /* Bounded batch: a busy radio must not starve control retries or timeouts. */
    for (unsigned i = 0; i < RECEIVE_CAPACITY && xQueueReceive(receive_queue, &frame, 0) == pdPASS; ++i) {
        const wireless_msg_t *p = &frame.packet;
        if (p->type == VBUS_STATUS) gpio_set_level(GPIO_NUM_9, p->payload.vbus.vbus_level);
        if (p->type == ALIVE_MODE) {
            wireless_heartbeat_seen(frame.tick);
            gpio_set_level(GPIO_NUM_9, p->payload.alive.vbus_level);
            if (!p->payload.alive.vbus_level) wireless_request_mode();
        }
        if (wireless_report_kind(p->type) || p->type == ALIVE_MODE) input_link_seen(frame.time_ms);
        input_receive(p, frame.generation, frame.time_ms);
    }
    input_check_link(input_now_ms());
    wireless_control_step(input_now_ms());
}

static void wireless_receive_task(void *arg)
{
    (void)arg;
    wireless_register_worker();
    while (true) {
        wireless_receive_step();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
    }
}

void wireless_init(void)
{
    receive_queue = xQueueCreate(RECEIVE_CAPACITY, sizeof(receive_frame_t));
    ESP_ERROR_CHECK(receive_queue ? ESP_OK : ESP_ERR_NO_MEM);
    gpio_config_t gpio = {.intr_type = GPIO_INTR_DISABLE, .mode = GPIO_MODE_OUTPUT_OD,
        .pin_bit_mask = 1ULL << GPIO_NUM_9, .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&gpio));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_9, 1));
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_now_init());
    broadcast_init();
    ESP_ERROR_CHECK(xTaskCreate(wireless_receive_task, "wireless_rx", 4096, NULL, 10, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_now_register_recv_cb(wifi_now_recv_cb));
    ESP_ERROR_CHECK(xTaskCreate(monitor_link_task, "heartbeat", 2048, NULL, 2, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
}
