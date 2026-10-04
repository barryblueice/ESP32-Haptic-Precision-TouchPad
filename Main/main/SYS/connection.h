#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Consumes RTC handoff and updates current_mode before app_main starts a stack. */
void connection_init(int boot_mode);
/* Parser task only, before processing each batch of input. */
void connection_poll(void);
void connection_wake(void);
void connection_config_pending(void);
void connection_config_complete(void);
esp_err_t connection_manual_restart(int mode);
int connection_mode(void);
/* Sender calls packet under connection_lock; receive only records discovery. */
void connection_probe_ready(uint32_t session);
bool connection_probe_packet(uint8_t packet[38]);
void connection_probe_receive(const uint8_t *data, unsigned size);

/* Serialize transport submissions/completions with route retirement. No waits
 * for endpoint completion or configuration storage while holding this mutex. */
void connection_lock(void);
void connection_unlock(void);
bool connection_selected(int transport);
bool connection_can_send(int transport);
uint32_t connection_epoch(void);
void connection_flight(int transport, uint32_t epoch, bool submitted);
void connection_link(int transport, bool ready);
void connection_usb_reset(void);
void connection_usb_suspend(bool suspended);
void connection_usb_mode(uint8_t mode);
