#include "rstp_protocol.h"
#include <string.h>

static uint16_t u16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
uint32_t rstp_u32(const uint8_t *p) { return u16(p) | ((uint32_t)u16(p + 2) << 16); }
void rstp_put32(uint8_t *p, uint32_t v) { for (unsigned i = 0; i < 4; ++i) p[i] = v >> (8 * i); }
void device_config_defaults(device_config_t *c)
{
    *c = (device_config_t){{63, 2, 80, 100, 130, 0, 1, 0}};
    rstp_put32(c->bytes + CFG_TIMEOUT, 180000);
    for (unsigned i = CFG_EDGES; i < CFG_POINTS; i += 5) { c->bytes[i + 3] = 5; c->bytes[i + 4] = 2; }
    for (unsigned i = CFG_POINTS; i < CFG_WIRELESS_LIGHT; i += 4) { c->bytes[i + 2] = 5; c->bytes[i + 3] = 2; }
    c->bytes[CFG_WIRELESS_LIGHT] = 20;
    c->bytes[CFG_WIRELESS_MEDIUM] = 35;
    c->bytes[CFG_WIRELESS_STRONG] = 70;
}
bool device_config_valid(const device_config_t *c)
{
    const uint8_t *b = c->bytes;
    uint32_t timeout = rstp_u32(b + CFG_TIMEOUT);
    if (b[0] > 100 || b[1] < 1 || b[1] > 3 || !b[2] || b[2] > b[3] || b[3] > b[4] ||
        b[5] > 3 || (b[6] & 0xe0) || timeout < 1000 || timeout > 3600000 || timeout % 1000) return false;
    for (unsigned i = CFG_EDGES; i < 32; i += 5) {
        unsigned max_width = i < CFG_EDGES + 10 ? 30 : 15;
        if (b[i] > 1 || b[i+1] > 6 || (b[i] && !b[i+1]) || b[i+2] > 1 ||
            !b[i+3] || b[i+3] > max_width || !b[i+4] || b[i+4] > 10) return false;
    }
    for (unsigned i = CFG_POINTS; i < CFG_WIRELESS_LIGHT; i += 4)
        if (b[i] > 1 || b[i+1] > 12 || (b[i] && !b[i+1]) ||
            !b[i+2] || b[i+2] > 30 || !b[i+3] || b[i+3] > 10) return false;
    if (!b[48] || b[48] > b[49] || b[49] > b[50] || b[50] > 100 || b[51]) return false;
    return true;
}
uint32_t rstp_capabilities_normalize(uint32_t caps)
{
    if (!(caps & RSTP_CAP_EDGES)) caps &= ~(RSTP_CAP_ARROW_KEYS | RSTP_CAP_EDGE_REPEAT);
    if ((caps & (RSTP_CAP_EDGES | RSTP_CAP_POINTS)) != (RSTP_CAP_EDGES | RSTP_CAP_POINTS))
        caps &= ~RSTP_CAP_POINT_TO_EDGE;
    return caps;
}
void device_config_store_record(uint8_t record[60], const device_config_t *c)
{
    const uint8_t header[8] = {'R','S','C','F',DEVICE_CONFIG_VERSION,0,52,0};
    memcpy(record, header, 8); memcpy(record + 8, c->bytes, DEVICE_CONFIG_SIZE);
}
bool device_config_load_record(device_config_t *out, const uint8_t *r, size_t size)
{
    if (!r || size < 8 || memcmp(r, "RSCF", 4) || r[5] || r[7]) return false;
    bool v1 = r[4] == 1 && r[6] == 32 && size == 40;
    bool v2 = r[4] == 2 && r[6] == 52 && size == 60;
    bool v3 = r[4] == 3 && r[6] == 52 && size == 60;
    if (!v1 && !v2 && !v3) return false;
    device_config_t c; device_config_defaults(&c);
    if (v1 && (r[14] > 1 || (r[15] & 0xf0))) return false;
    memcpy(c.bytes, r + 8, v3 ? DEVICE_CONFIG_SIZE : 32);
    if (v2) {
        for (unsigned p = 0; p < 4; ++p) {
            const uint8_t *old = r + 8 + CFG_POINTS + p * 5;
            uint8_t *next = c.bytes + CFG_POINTS + p * 4;
            if (old[1] > 12 || old[2] > 1) return false;
            next[0] = old[0];
            next[1] = old[1] && old[2] ? ((old[1] & 1) ? old[1] + 1 : old[1] - 1) : old[1];
            next[2] = old[3]; next[3] = old[4];
        }
    }
    if (!device_config_valid(&c)) return false;
    *out = c; return true;
}
bool device_config_supported(const device_config_t *a, const device_config_t *b, uint32_t caps)
{
    caps = rstp_capabilities_normalize(caps);
    static const uint8_t start[] = {0, 1, 2, 5, 6, 12}, end[] = {1, 2, 5, 6, 6, 32};
    for (unsigned i = 0; i < 6; ++i)
        if (!(caps & (1U << i)) && memcmp(a->bytes + start[i], b->bytes + start[i], end[i] - start[i])) return false;
    if (!(caps & (1U << 4)) && memcmp(a->bytes + CFG_TIMEOUT, b->bytes + CFG_TIMEOUT, 4)) return false;
    if (!(caps & (1U << 4)) && ((a->bytes[6] ^ b->bytes[6]) & 1)) return false;
    if (!(caps & RSTP_CAP_POINT_TO_EDGE) && (b->bytes[6] & 0x1e)) return false;
    if (!(caps & RSTP_CAP_POINTS)) {
        device_config_t defaults; device_config_defaults(&defaults);
        if (memcmp(b->bytes + CFG_POINTS, defaults.bytes + CFG_POINTS, 16) || (b->bytes[7] & 0xf0)) return false;
    }
    if ((caps & (RSTP_CAP_EDGES | RSTP_CAP_EDGE_REPEAT)) != (RSTP_CAP_EDGES | RSTP_CAP_EDGE_REPEAT) &&
        ((a->bytes[CFG_EDGE_REPEAT] ^ b->bytes[CFG_EDGE_REPEAT]) & 0x0f)) return false;
    for (unsigned i = CFG_EDGES; i < 32; i += 5)
        if ((caps & (RSTP_CAP_EDGES | RSTP_CAP_ARROW_KEYS)) != (RSTP_CAP_EDGES | RSTP_CAP_ARROW_KEYS) &&
            (a->bytes[i+1] >= 5 || b->bytes[i+1] >= 5) && memcmp(a->bytes + i, b->bytes + i, 5)) return false;
    if (!(caps & RSTP_CAP_WIRELESS_THRESHOLDS) &&
        memcmp(a->bytes + CFG_WIRELESS_LIGHT, b->bytes + CFG_WIRELESS_LIGHT, 3)) return false;
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
    else if (length != (r->command == RSTP_WRITE ? DEVICE_CONFIG_SIZE : 0)) r->status = RSTP_LENGTH;
    else {
        for (unsigned i = 12 + length; i < 64; ++i) if (b[i]) r->status = RSTP_INVALID;
        if (r->command == RSTP_WRITE) {
            memcpy(r->config.bytes, b + 12, DEVICE_CONFIG_SIZE);
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
