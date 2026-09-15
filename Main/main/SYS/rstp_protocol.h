#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { RSTP_OK, RSTP_UNSUPPORTED, RSTP_VERSION, RSTP_LENGTH, RSTP_INVALID,
       RSTP_BUSY, RSTP_STORAGE, RSTP_RESTART };
enum { RSTP_INFO = 1, RSTP_READ, RSTP_WRITE };
enum { CFG_INTENSITY, CFG_LEVEL, CFG_LIGHT, CFG_MEDIUM, CFG_STRONG,
       CFG_ROTATION, CFG_SLEEP, CFG_EDGE_REPEAT, CFG_TIMEOUT = 8, CFG_EDGES = 12,
       CFG_POINTS = 32, DEVICE_CONFIG_SIZE = 52 };
enum { RSTP_CAP_EDGES = 0x20, RSTP_CAP_ARROW_KEYS = 0x40, RSTP_CAP_EDGE_REPEAT = 0x80,
       RSTP_CAP_POINTS = 0x100, RSTP_CAP_POINT_TO_EDGE = 0x200 };
/* Wire name edge_repeat_mask: retain the captured edge/axis outside its band.
 * Steps still require movement; this does not enable timed auto-repeat. */
typedef struct { uint8_t bytes[DEVICE_CONFIG_SIZE]; } device_config_t;
static inline bool device_config_sleep(const device_config_t *c) { return (c->bytes[CFG_SLEEP] & 1) != 0; }
static inline bool device_config_point_to_edge(const device_config_t *c, unsigned point)
{ return point < 4 && (c->bytes[CFG_SLEEP] & (2U << point)) != 0; }
uint32_t rstp_capabilities_normalize(uint32_t caps);
bool device_config_load_record(device_config_t *out, const uint8_t *record, size_t size);
void device_config_store_record(uint8_t record[60], const device_config_t *config);
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
