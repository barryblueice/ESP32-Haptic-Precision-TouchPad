#include "WIFI/wireless_wifi.h"
#include "I2C/SUB_DEV/sub_dev.h"
#include "esp_timer.h"

void wireless_make_heartbeat(wireless_msg_t *packet)
{
    *packet = (wireless_msg_t){0};
    packet->type = ALIVE_MODE;
    packet->payload.alive.battery_level = battery_percentage;
    packet->payload.alive.uptime = (uint32_t)(esp_timer_get_time() / 1000000);
}
