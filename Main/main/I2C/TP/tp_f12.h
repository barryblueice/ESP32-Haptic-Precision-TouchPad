#ifndef TP_F12_H
#define TP_F12_H

#include <stdbool.h>
#include <stdint.h>

/* Synaptics F12 Data1 object types, NOT HID Tip/Confidence bit flags.
 * The TM3651 attention report has five 8-byte records at packet offset 4.
 * Source: Synaptics-authored drivers/input/rmi4/rmi_f12.c. */
enum {
    TP_F12_NONE = 0x00, TP_F12_FINGER = 0x01, TP_F12_STYLUS = 0x02,
    TP_F12_PALM = 0x03, TP_F12_UNCLASSIFIED = 0x04,
    TP_F12_GLOVED_FINGER = 0x06
};

static inline bool tp_f12_present(uint8_t type)
{
    /* Unknown objects also prevent false lift/recovery admission. */
    return type != TP_F12_NONE;
}

static inline bool tp_f12_finger(uint8_t type)
{
    return type == TP_F12_FINGER || type == TP_F12_GLOVED_FINGER;
}

static inline uint8_t tp_f12_present_mask(const uint8_t packet[64])
{
    uint8_t mask = 0;
    for (unsigned i = 0; i < 5; ++i)
        if (tp_f12_present(packet[4 + 8*i])) mask |= 1U << i;
    return mask;
}

static inline bool tp_f12_position_valid(const uint8_t record[8])
{
    unsigned x = record[1] | ((unsigned)record[2] << 8);
    unsigned y = record[3] | ((unsigned)record[4] << 8);
    return x <= 2302 && y <= 1532;
}

#define PALM_MAJOR_LIMIT 0x10
#define PALM_MINOR_LIMIT 0x10
#define PALM_RATIO_LIMIT 2.0f

static inline bool tp_f12_shape_valid(const uint8_t record[8])
{
    /* Restore the pre-5941104 filter exactly: these names designate packet
     * axes, not sorted lengths. minor == 0 deliberately gives ratio zero. */
    uint8_t major = record[6], minor = record[7];
    if (!tp_f12_position_valid(record) ||
        major > PALM_MAJOR_LIMIT || minor > PALM_MINOR_LIMIT)
        return false;
    float ratio = minor > 0 ? (float)major / minor : 0;
    return ratio <= PALM_RATIO_LIMIT;
}

static inline bool tp_f12_confident(const uint8_t record[8])
{
    return tp_f12_finger(record[0]) && tp_f12_shape_valid(record);
}

/* Ordinary input historically admitted 03 via status & 1. Restore its
 * normal size/ratio filtering without interpreting unknown types as bits.
 * Keep 04 reserved for special gestures and 06 finger support intact. */
static inline bool tp_f12_touch_confident(const uint8_t record[8])
{
    return (tp_f12_finger(record[0]) || record[0] == TP_F12_PALM) &&
        tp_f12_shape_valid(record);
}

/* Unclassified objects are admitted only to local edge/corner recognition.
 * Keep ordinary HID confidence and pressure/knuckle eligibility unchanged. */
static inline bool tp_f12_gesture_eligible(const uint8_t record[8])
{
    if (record[0] != TP_F12_UNCLASSIFIED) return tp_f12_confident(record);
    return tp_f12_position_valid(record);
}

static inline const char *tp_f12_type_name(uint8_t type)
{
    switch (type) {
    case TP_F12_NONE: return "none";
    case TP_F12_FINGER: return "finger";
    case TP_F12_STYLUS: return "stylus";
    case TP_F12_PALM: return "palm";
    case TP_F12_UNCLASSIFIED: return "unclassified";
    case TP_F12_GLOVED_FINGER: return "gloved_finger";
    default: return "other";
    }
}
#endif
