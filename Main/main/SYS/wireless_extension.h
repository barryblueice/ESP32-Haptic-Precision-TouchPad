#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Extension payload in the existing 38-byte ESP-NOW envelope. */
enum { WIRE_AUX = 5, WIRE_SURFACE = 6, WIRE_SURFACE_ACK = 7, WIRE_VERSION = 2 };
enum { WIRE_SETTINGS = 8, WIRE_SETTINGS_ACK = 9, WIRE_SETTINGS_VERSION = 1 };
enum { WIRE_SETTING_INTENSITY = 1, WIRE_SETTING_LEVEL = 2 };
enum { WIRE_SETTINGS_OK, WIRE_SETTINGS_BUSY, WIRE_SETTINGS_STORAGE, WIRE_SETTINGS_UNSUPPORTED };
typedef struct {
    uint32_t session, client, sequence;
    uint8_t mask, intensity, level, status;
} wire_settings_t;
typedef struct { uint8_t version, rotation; uint32_t session; } wire_surface_t;
typedef struct { uint32_t session, sequence; uint8_t action; int16_t steps; bool hold; } wire_action_t;
static inline uint32_t wire_u32(const uint8_t *b)
{ return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24); }
static inline void wire_put32(uint8_t *b, uint32_t v)
{ for (unsigned i=0;i<4;++i) b[i]=(uint8_t)(v>>(8*i)); }
/* A zero-mask request reads the current values. Only applied values appear in ACKs. */
static inline void wire_settings_encode(uint8_t out[38], uint32_t type, const wire_settings_t *s)
{
    memset(out,0,38); wire_put32(out,type); out[4]=WIRE_SETTINGS_VERSION;
    wire_put32(out+5,s->session); wire_put32(out+9,s->client); wire_put32(out+13,s->sequence);
    out[17]=s->mask; out[18]=s->intensity; out[19]=s->level; out[20]=s->status;
}
static inline bool wire_settings_decode(const uint8_t *b, unsigned n, uint32_t type, wire_settings_t *s)
{
    if (!b || n!=38 || (type!=WIRE_SETTINGS && type!=WIRE_SETTINGS_ACK) || wire_u32(b)!=type ||
        b[4]!=WIRE_SETTINGS_VERSION || !wire_u32(b+5) || !wire_u32(b+9) || !wire_u32(b+13) || b[17]>3) return false;
    if (type==WIRE_SETTINGS) {
        if (b[20] || ((b[17]&1) ? b[18]>100 : b[18]!=0) ||
            ((b[17]&2) ? (b[19]<1 || b[19]>3) : b[19]!=0)) return false;
    } else if (b[18]>100 || b[19]<1 || b[19]>3 || b[20]>WIRE_SETTINGS_UNSUPPORTED) return false;
    for (unsigned i=21;i<38;++i) if (b[i]) return false;
    *s=(wire_settings_t){wire_u32(b+5),wire_u32(b+9),wire_u32(b+13),b[17],b[18],b[19],b[20]}; return true;
}
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
    out[15]=a->hold;
}
static inline bool wire_action_decode(const uint8_t *b, unsigned n, wire_action_t *a)
{
    /* 1..6 retain directional semantics; 13..47 carry function bindings. */
    if(n!=38 || wire_u32(b)!=WIRE_AUX || !wire_u32(b+4) || !wire_u32(b+8) ||
        b[12]>47 || (b[12]>6 && b[12]<13)) return false;
    int16_t steps=(int16_t)((uint16_t)b[13]|((uint16_t)b[14]<<8));
    if ((!b[12] && steps) || (b[12] && (!steps || steps < -127 || steps > 127))) return false;
    if (b[12]>=13 && steps<0) return false;
    if (b[15]>1 || (b[15] && ((b[12]!=1 && b[12]!=2 && b[12]!=5 && b[12]!=6) || (steps!=1 && steps!=-1)))) return false;
    for(unsigned i=16;i<38;++i) if(b[i]) return false;
    *a=(wire_action_t){wire_u32(b+4),wire_u32(b+8),b[12],steps,b[15]!=0}; return true;
}
