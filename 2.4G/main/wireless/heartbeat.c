#include "wireless.h"
#include "driver/gpio.h"

static portMUX_TYPE heartbeat_lock = portMUX_INITIALIZER_UNLOCKED;
static TickType_t last_seen_timestamp;

void wireless_heartbeat_seen(TickType_t tick)
{
    taskENTER_CRITICAL(&heartbeat_lock);
    last_seen_timestamp = tick;
    taskEXIT_CRITICAL(&heartbeat_lock);
}
void monitor_link_task(void *arg)
{
    (void)arg;
    while (true) {
        taskENTER_CRITICAL(&heartbeat_lock);
        TickType_t last = last_seen_timestamp;
        taskEXIT_CRITICAL(&heartbeat_lock);
        /* Deliberately preserve the original GPIO9 startup/heartbeat behavior. */
        gpio_set_level(GPIO_NUM_9, (TickType_t)(xTaskGetTickCount() - last) > pdMS_TO_TICKS(5000));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
