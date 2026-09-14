#include "rstp_protocol.h"
#include <string.h>

static uint16_t u16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
uint32_t rstp_u32(const uint8_t *p) { return u16(p) | ((uint32_t)u16(p + 2) << 16); }
void rstp_put32(uint8_t *p, uint32_t v) { for (unsigned i = 0; i < 4; ++i) p[i] = v >> (8 * i); }
void device_config_defaults(device_config_t *c)
{
    *c = (device_config_t){{63, 2, 80, 100, 130, 0, 1, 0}};
    rstp_put32(c->bytes + CFG_TIMEOUT, 180000);
    for (unsigned i = CFG_EDGES; i < 32; i += 5) { c->bytes[i + 3] = 5; c->bytes[i + 4] = 2; }
}
bool device_config_valid(const device_config_t *c)
{
    const uint8_t *b = c->bytes;
    uint32_t timeout = rstp_u32(b + CFG_TIMEOUT);
    if (b[0] > 100 || b[1] < 1 || b[1] > 3 || !b[2] || b[2] > b[3] || b[3] > b[4] ||
        b[5] > 3 || b[6] > 1 || b[7] || timeout < 1000 || timeout > 3600000 || timeout % 1000) return false;
    for (unsigned i = CFG_EDGES; i < 32; i += 5)
        if (b[i] > 1 || b[i+1] > 4 || (b[i] && !b[i+1]) || b[i+2] > 1 ||
            !b[i+3] || b[i+3] > 15 || !b[i+4] || b[i+4] > 10) return false;
    return true;
}
bool device_config_supported(const device_config_t *a, const device_config_t *b, uint32_t caps)
{
    static const uint8_t start[] = {0, 1, 2, 5, 6, 12}, end[] = {1, 2, 5, 6, 12, 32};
    for (unsigned i = 0; i < 6; ++i)
        if (!(caps & (1U << i)) && memcmp(a->bytes + start[i], b->bytes + start[i], end[i] - start[i])) return false;
    return true;
}
bool rstp_decode(const uint8_t *b, size_t n, rstp_request_t *r)
{
    if (!b || n < 12 || memcmp(b, "RSTP", 4) || !u16(b + 6)) return false;
    *r = (rstp_request_t){.command = b[5], .sequence = u16(b + 6)};
    uint16_t length = u16(b + 8);
    if (b[4] != 1) r->status = RSTP_VERSION;
    else if (n != 64 || length > 52) r->status = RSTP_LENGTH;
    else if (u16(b + 10)) r->status = RSTP_INVALID;
    else if (r->command < RSTP_INFO || r->command > RSTP_WRITE) r->status = RSTP_UNSUPPORTED;
    else if (length != (r->command == RSTP_WRITE ? 32 : 0)) r->status = RSTP_LENGTH;
    else {
        for (unsigned i = 12 + length; i < 64; ++i) if (b[i]) r->status = RSTP_INVALID;
        if (r->command == RSTP_WRITE) {
            memcpy(r->config.bytes, b + 12, 32);
            if (!device_config_valid(&r->config)) r->status = RSTP_INVALID;
        }
    }
    return true;
}
void rstp_response(uint8_t out[64], const rstp_request_t *r, uint16_t status, const uint8_t *payload, uint16_t n)
{
    memset(out, 0, 64); memcpy(out, "RSTP", 4);
    out[4] = 1; out[5] = r->command; out[6] = r->sequence; out[7] = r->sequence >> 8;
    out[10] = status; out[11] = status >> 8;
    if (status == RSTP_OK && n <= 52 && payload) { out[8] = n; memcpy(out + 12, payload, n); }
}
