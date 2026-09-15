#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Extension payload in the existing 38-byte ESP-NOW envelope. */
enum { WIRE_AUX = 5, WIRE_SURFACE = 6, WIRE_SURFACE_ACK = 7, WIRE_VERSION = 1 };
typedef struct { uint8_t version, rotation; uint32_t session; } wire_surface_t;
typedef struct { uint32_t session, sequence; uint8_t action; int16_t steps; } wire_action_t;
static inline uint32_t wire_u32(const uint8_t *b)
{ return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24); }
static inline void wire_put32(uint8_t *b, uint32_t v)
{ for (unsigned i=0;i<4;++i) b[i]=(uint8_t)(v>>(8*i)); }
static inline void wire_surface_encode(uint8_t out[38], uint32_t type, const wire_surface_t *s)
{
    memset(out,0,38); wire_put32(out,type); out[4]=s->version; out[5]=s->rotation; wire_put32(out+6,s->session);
}
static inline bool wire_surface_decode(const uint8_t *b, unsigned n, uint32_t type, wire_surface_t *s)
{
    if (n!=38 || wire_u32(b)!=type || b[4]!=WIRE_VERSION || b[5]>3 || !wire_u32(b+6)) return false;
    for (unsigned i=10;i<38;++i) if(b[i]) return false;
    *s=(wire_surface_t){b[4],b[5],wire_u32(b+6)}; return true;
}
static inline void wire_action_encode(uint8_t out[38], const wire_action_t *a)
{
    memset(out,0,38); wire_put32(out,WIRE_AUX); wire_put32(out+4,a->session);
    wire_put32(out+8,a->sequence); out[12]=a->action; out[13]=(uint8_t)a->steps; out[14]=(uint16_t)a->steps>>8;
}
static inline bool wire_action_decode(const uint8_t *b, unsigned n, wire_action_t *a)
{
    if(n!=38 || wire_u32(b)!=WIRE_AUX || !wire_u32(b+4) || !wire_u32(b+8) || b[12]>6) return false;
    int16_t steps=(int16_t)((uint16_t)b[13]|((uint16_t)b[14]<<8));
    if ((!b[12] && steps) || (b[12] && (!steps || steps < -127 || steps > 127))) return false;
    for(unsigned i=15;i<38;++i) if(b[i]) return false;
    *a=(wire_action_t){wire_u32(b+4),wire_u32(b+8),b[12],steps}; return true;
}
