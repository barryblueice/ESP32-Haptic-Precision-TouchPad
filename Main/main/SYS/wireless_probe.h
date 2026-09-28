#pragma once
#include "wireless_extension.h"

/* Discovery never establishes a SURFACE/input/settings session. */
enum { WIRE_PROBE = 10, WIRE_PROBE_ACK = 11, WIRE_PROBE_VERSION = 1 };
enum { PROBE_RETRY_MS = 250, PROBE_WINDOW_MS = 750,
       PROBE_PERIOD_MS = 1000, PROBE_FRESH_MS = 2500 };
typedef struct { uint32_t session, round; } wire_probe_t;
static inline void wire_probe_encode(uint8_t out[38], uint32_t type, const wire_probe_t *p)
{
    memset(out, 0, 38); wire_put32(out, type); out[4] = WIRE_PROBE_VERSION;
    wire_put32(out + 5, p->session); wire_put32(out + 9, p->round);
}
static inline bool wire_probe_decode(const uint8_t *b, unsigned n, uint32_t type, wire_probe_t *p)
{
    if (!b || n != 38 || (type != WIRE_PROBE && type != WIRE_PROBE_ACK) ||
        wire_u32(b) != type || b[4] != WIRE_PROBE_VERSION ||
        !wire_u32(b + 5) || !wire_u32(b + 9)) return false;
    for (unsigned i = 13; i < 38; ++i) if (b[i]) return false;
    *p = (wire_probe_t){wire_u32(b + 5), wire_u32(b + 9)}; return true;
}
typedef struct {
    wire_probe_t token;
    uint32_t started_at, seen_at;
    unsigned attempts;
    bool started, answered, seen;
} wireless_probe_t;
static inline void wireless_probe_begin(wireless_probe_t *p, uint32_t now)
{
    if (!++p->token.round) ++p->token.round;
    p->started_at = now; p->attempts = 0; p->started = true; p->answered = false;
}
static inline bool wireless_probe_fresh(const wireless_probe_t *p, uint32_t now)
{ return p->seen && (uint32_t)(now - p->seen_at) < PROBE_FRESH_MS; }
static inline bool wireless_probe_packet(wireless_probe_t *p, uint32_t now, uint8_t out[38])
{
    uint32_t elapsed = now - p->started_at;
    if (!p->token.session || !p->started || p->answered || elapsed >= PROBE_WINDOW_MS ||
        p->attempts >= 3 || elapsed < p->attempts * PROBE_RETRY_MS) return false;
    /* Do not burst missed attempts after the sender was busy. */
    p->attempts = elapsed / PROBE_RETRY_MS + 1;
    wire_probe_encode(out, WIRE_PROBE, &p->token); return true;
}
static inline bool wireless_probe_ack(wireless_probe_t *p, const uint8_t *b, unsigned n, uint32_t now)
{
    wire_probe_t ack;
    if (!wire_probe_decode(b, n, WIRE_PROBE_ACK, &ack) || !p->started || !p->attempts ||
        p->answered || (uint32_t)(now - p->started_at) >= PROBE_WINDOW_MS ||
        ack.session != p->token.session || ack.round != p->token.round) return false;
    p->answered = p->seen = true; p->seen_at = now; return true;
}
