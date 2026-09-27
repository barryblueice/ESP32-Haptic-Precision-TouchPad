#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

void connection_init(int boot_mode);
/* Parser task only, before processing each batch of input. */
void connection_poll(void);
void connection_wake(void);
void connection_config_pending(void);
void connection_config_complete(void);
esp_err_t connection_manual_restart(int mode);
int connection_mode(void);

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
void connection_usb_mode(uint8_t mode);
