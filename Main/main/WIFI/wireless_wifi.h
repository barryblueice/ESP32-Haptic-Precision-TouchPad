#ifndef WIRELESS_WIFI_H
#define WIRELESS_WIFI_H
#include "SYS/hid_msg.h"

extern uint8_t receiver_mac[6];

void wireless_wifi_init(void);
void wireless_make_heartbeat(wireless_msg_t *packet);
void wireless_espnow_init(void);
void wifi_send_task(void *arg);

#endif