#include "SYS/wireless_extension.h"
#include "protocol.h"
#include <string.h>

report_kind_t wireless_report_kind(uint32_t type)
{
    switch (type) {
    case MOUSE_MODE: return REPORT_MOUSE;
    case LEGACY_PTP_MODE: return REPORT_LEGACY;
    case HAPTIC_PTP_MODE: return REPORT_HAPTIC;
    default: return REPORT_NONE;
    }
}

size_t report_size(report_kind_t kind)
{
    switch (kind) {
    case REPORT_MOUSE: return sizeof(mouse_hid_report_t);
    case REPORT_LEGACY: return sizeof(legacy_ptp_report_t);
    case REPORT_HAPTIC: return sizeof(haptic_ptp_report_t);
    default: return 0;
    }
}

bool wireless_decode(const uint8_t *data, int len, wireless_msg_t *out)
{
    if (!data || !out || len < 4) return false;
    uint32_t type;
    memcpy(&type, data, sizeof(type));
    size_t size = report_size(wireless_report_kind(type));
    if (type == WIRE_AUX || type == WIRE_SURFACE) {
        wire_surface_t surface; wire_action_t action;
        bool valid = type == WIRE_AUX ? wire_action_decode(data,len,&action) : wire_surface_decode(data,len,WIRE_SURFACE,&surface);
        if (!valid) return false;
        size = 34;
    }
    if (type == WIRE_SETTINGS_ACK) {
        wire_settings_t settings;
        if (!wire_settings_decode(data,len,type,&settings)) return false;
        size = 34;
    }
    if (type == VBUS_STATUS) size = sizeof(vbus_msg_t);
    if (type == ALIVE_MODE) size = sizeof(alive_msg_t);
    if (!size || (size_t)len < 4 + size) return false;
    *out = (wireless_msg_t){.type = type};
    memcpy(&out->payload, data + 4, size);
    return true;
}
