#include "wireless.h"
#include "driver/gpio.h"

static portMUX_TYPE heartbeat_lock = portMUX_INITIALIZER_UNLOCKED;
static TickType_t last_seen_timestamp;
static bool heartbeat_online, connection_led_ready;

/* Hold heartbeat_lock through the pin write so receive and timeout cannot race. */
static void connection_led_update(void)
{
    if (heartbeat_online &&
        (TickType_t)(xTaskGetTickCount() - last_seen_timestamp) > pdMS_TO_TICKS(5000)) {
        heartbeat_online = false;
    }
    if (connection_led_ready) gpio_set_level(GPIO_NUM_9, !heartbeat_online);
}

void wireless_heartbeat_seen(TickType_t tick)
{
    taskENTER_CRITICAL(&heartbeat_lock);
    last_seen_timestamp = tick;
    heartbeat_online = true;
    connection_led_update();
    taskEXIT_CRITICAL(&heartbeat_lock);
}

void wireless_led_init(void)
{
    /* Match 1a05877's hardware setup: open drain, configured after USB. */
    gpio_config_t gpio = {.intr_type = GPIO_INTR_DISABLE, .mode = GPIO_MODE_OUTPUT_OD,
        .pin_bit_mask = 1ULL << GPIO_NUM_9, .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&gpio));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_9, 1));
    taskENTER_CRITICAL(&heartbeat_lock);
    connection_led_ready = true;
    connection_led_update();
    taskEXIT_CRITICAL(&heartbeat_lock);
}

void monitor_link_task(void *arg)
{
    (void)arg;
    while (true) {
        /* Restore the historical periodic drive, independent of HID processing. */
        taskENTER_CRITICAL(&heartbeat_lock);
        connection_led_update();
        taskEXIT_CRITICAL(&heartbeat_lock);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
