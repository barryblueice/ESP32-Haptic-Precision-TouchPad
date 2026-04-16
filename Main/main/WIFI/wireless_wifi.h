#ifndef WIRELESS_WIFI_H
#define WIRELESS_WIFI_H

extern uint8_t receiver_mac[6];

void wireless_wifi_init(void);
void alive_heartbeat_task(void *pvParameters);
void wireless_espnow_init(void);
void wifi_send_task(void *arg);

#endif