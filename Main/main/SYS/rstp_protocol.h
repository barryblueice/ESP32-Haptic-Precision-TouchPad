#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { RSTP_OK, RSTP_UNSUPPORTED, RSTP_VERSION, RSTP_LENGTH, RSTP_INVALID,
       RSTP_BUSY, RSTP_STORAGE, RSTP_RESTART };
enum { RSTP_INFO = 1, RSTP_READ, RSTP_WRITE };
enum { CFG_INTENSITY, CFG_LEVEL, CFG_LIGHT, CFG_MEDIUM, CFG_STRONG,
       CFG_ROTATION, CFG_SLEEP, CFG_RESERVED, CFG_TIMEOUT = 8, CFG_EDGES = 12 };
typedef struct { uint8_t bytes[32]; } device_config_t;
typedef struct { uint8_t command; uint16_t sequence, status; device_config_t config; } rstp_request_t;
uint32_t rstp_u32(const uint8_t *p);
void rstp_put32(uint8_t *p, uint32_t value);
void device_config_defaults(device_config_t *config);
bool device_config_valid(const device_config_t *config);
bool device_config_supported(const device_config_t *old, const device_config_t *next, uint32_t caps);
/* False means the request cannot be addressed and must be discarded. */
bool rstp_decode(const uint8_t *data, size_t size, rstp_request_t *request);
void rstp_response(uint8_t out[64], const rstp_request_t *request, uint16_t status,
                   const uint8_t *payload, uint16_t length);
