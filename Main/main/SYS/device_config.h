#pragma once
#include "rstp_protocol.h"
#include "esp_err.h"

esp_err_t device_config_init(void);
bool device_config_ready(void);
void device_config_get(device_config_t *config);
uint8_t device_config_value(unsigned offset);
uint32_t device_config_capabilities(void);
void device_config_disable(uint32_t capabilities);
/* Serialized, blocking task APIs. Never call from USB callbacks or the parser. */
uint16_t device_config_save(const device_config_t *config);
esp_err_t device_config_set_legacy(unsigned offset, uint8_t value, bool persist);
/* Bits 0/1 select intensity/press level; apply a combined Windows update once. */
esp_err_t device_config_set_controls(uint8_t mask, uint8_t intensity, uint8_t level, bool persist);
/* Called only by the parser between frames; true means input is quiesced. */
bool device_config_parser_boundary(void);
uint8_t device_config_rotation(void);
uint16_t device_config_x_max(void);
uint16_t device_config_y_max(void);
