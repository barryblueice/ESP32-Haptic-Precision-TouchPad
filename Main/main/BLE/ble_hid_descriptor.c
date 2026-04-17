#include <stdio.h>
#include <string.h>

const uint8_t ble_mouse_hid_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        // Report Id (1)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Buttons)
    0x19, 0x01,        //     Usage Minimum (01)
    0x29, 0x03,        //     Usage Maximum (03)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x03,        //     Report Count (3)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data, Var, Abs) - 3 Buttons
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x05,        //     Report Size (5)
    0x81, 0x03,        //     Input (Cnst, Var, Abs) - 5 bit Padding
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127) <-- 尝试保持，如果还报错就改 0x80
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x06,        //     Input (Data, Var, Rel) - X, Y, Wheel
    0xC0,              //   End Collection (Physical)
    0xC0               // End Collection (Application)
};

const uint8_t ble_ptp_hid_report_descriptor[] = {
    0x05, 0x0D,                    // USAGE_PAGE (Digitizers)
    0x09, 0x05,                    // USAGE (Touch Pad)
    0xA1, 0x01,                    // COLLECTION (Application)
    0x85, 0x01,                    // REPORT_ID (1)

    // -------- Finger 0 --------
    0x09, 0x22,                         // USAGE (Finger)
    0xA1, 0x02,                         // COLLECTION (Logical)

    0x05, 0x0D,                         // USAGE_PAGE (Digitizers)
    0x09, 0x47,                         // USAGE (Confidence)
    0x09, 0x42,                         // USAGE (Tip Switch)
    0x15, 0x00,                         // LOGICAL_MINIMUM (0)
    0x25, 0x01,                         // LOGICAL_MAXIMUM (1)
    0x75, 0x01,                         // REPORT_SIZE (1)
    0x95, 0x02,                         // REPORT_COUNT (2)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)

    0x09, 0x51,                         // USAGE (Contact Identifier)
    0x25, 0x3F,                         // LOGICAL_MAXIMUM (63)
    0x75, 0x06,                         // REPORT_SIZE (6)
    0x95, 0x01,                         // REPORT_COUNT (1)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)

    // ---- X Axis ----
    0x05, 0x01,                         // USAGE_PAGE (Generic Desktop)
    0x09, 0x30,                         // USAGE (X)
    0x15, 0x00,                         // LOGICAL_MINIMUM (0)
    0x26, 0xFA, 0x08,                          // LOGICAL_MAXIMUM

    0x35, 0x00,                         // PHYSICAL_MINIMUM (0)
    0x46, 0x7D, 0x04,                         // PHYSICAL_MAXIMUM
    0x55, 0x0E,             // UNIT_EXPONENT (-3)
    0x65, 0x11,                      // UNIT (Centimeter)

    0x75, 0x10,                         // REPORT_SIZE (16)
    0x95, 0x01,                         // REPORT_COUNT (1)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)

    // ---- Y Axis ----
    0x09, 0x31,                         // USAGE (Y)
    0x15, 0x00,                         // LOGICAL_MINIMUM (0)
    0x26, 0xFC, 0x05,                          // LOGICAL_MAXIMUM

    0x35, 0x00,                         // PHYSICAL_MINIMUM (0)
    0x46, 0xFE, 0x02,                         // PHYSICAL_MAXIMUM

    0x75, 0x10,                         // REPORT_SIZE (16)
    0x95, 0x01,                         // REPORT_COUNT (1)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)

    0xC0,                               // END_COLLECTION

    // -------- Finger 1 --------
    0x09, 0x22,                         // USAGE (Finger)
    0xA1, 0x02,                         // COLLECTION (Logical)

    0x05, 0x0D,                         // USAGE_PAGE (Digitizers)
    0x09, 0x47,                         // USAGE (Confidence)
    0x09, 0x42,                         // USAGE (Tip Switch)
    0x15, 0x00,                         // LOGICAL_MINIMUM (0)
    0x25, 0x01,                         // LOGICAL_MAXIMUM (1)
    0x75, 0x01,                         // REPORT_SIZE (1)
    0x95, 0x02,                         // REPORT_COUNT (2)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)

    0x09, 0x51,                         // USAGE (Contact Identifier)
    0x25, 0x3F,                         // LOGICAL_MAXIMUM (63)
    0x75, 0x06,                         // REPORT_SIZE (6)
    0x95, 0x01,                         // REPORT_COUNT (1)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)

    // ---- X Axis ----
    0x05, 0x01,                         // USAGE_PAGE (Generic Desktop)
    0x09, 0x30,                         // USAGE (X)
    0x15, 0x00,                         // LOGICAL_MINIMUM (0)
    0x26, 0xFA, 0x08,                          // LOGICAL_MAXIMUM

    0x35, 0x00,                         // PHYSICAL_MINIMUM (0)
    0x46, 0x7D, 0x04,                         // PHYSICAL_MAXIMUM
    0x55, 0x0E,             // UNIT_EXPONENT (-3)
    0x65, 0x11,                      // UNIT (Centimeter)

    0x75, 0x10,                         // REPORT_SIZE (16)
    0x95, 0x01,                         // REPORT_COUNT (1)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)

    // ---- Y Axis ----
    0x09, 0x31,                         // USAGE (Y)
    0x15, 0x00,                         // LOGICAL_MINIMUM (0)
    0x26, 0xFC, 0x05,                          // LOGICAL_MAXIMUM

    0x35, 0x00,                         // PHYSICAL_MINIMUM (0)
    0x46, 0xFE, 0x02,                         // PHYSICAL_MAXIMUM

    0x75, 0x10,                         // REPORT_SIZE (16)
    0x95, 0x01,                         // REPORT_COUNT (1)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)

    0xC0,                               // END_COLLECTION

    // -------- Finger 2 --------
    0x09, 0x22,                         // USAGE (Finger)
    0xA1, 0x02,                         // COLLECTION (Logical)

    0x05, 0x0D,                         // USAGE_PAGE (Digitizers)
    0x09, 0x47,                         // USAGE (Confidence)
    0x09, 0x42,                         // USAGE (Tip Switch)
    0x15, 0x00,                         // LOGICAL_MINIMUM (0)
    0x25, 0x01,                         // LOGICAL_MAXIMUM (1)
    0x75, 0x01,                         // REPORT_SIZE (1)
    0x95, 0x02,                         // REPORT_COUNT (2)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)

    0x09, 0x51,                         // USAGE (Contact Identifier)
    0x25, 0x3F,                         // LOGICAL_MAXIMUM (63)
    0x75, 0x06,                         // REPORT_SIZE (6)
    0x95, 0x01,                         // REPORT_COUNT (1)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)

    // ---- X Axis ----
    0x05, 0x01,                         // USAGE_PAGE (Generic Desktop)
    0x09, 0x30,                         // USAGE (X)
    0x15, 0x00,                         // LOGICAL_MINIMUM (0)
    0x26, 0xFA, 0x08,                          // LOGICAL_MAXIMUM

    0x35, 0x00,                         // PHYSICAL_MINIMUM (0)
    0x46, 0x7D, 0x04,                         // PHYSICAL_MAXIMUM
    0x55, 0x0E,             // UNIT_EXPONENT (-3)
    0x65, 0x11,                      // UNIT (Centimeter)

    0x75, 0x10,                         // REPORT_SIZE (16)
    0x95, 0x01,                         // REPORT_COUNT (1)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)

    // ---- Y Axis ----
    0x09, 0x31,                         // USAGE (Y)
    0x15, 0x00,                         // LOGICAL_MINIMUM (0)
    0x26, 0xFC, 0x05,                          // LOGICAL_MAXIMUM

    0x35, 0x00,                         // PHYSICAL_MINIMUM (0)
    0x46, 0xFE, 0x02,                         // PHYSICAL_MAXIMUM

    0x75, 0x10,                         // REPORT_SIZE (16)
    0x95, 0x01,                         // REPORT_COUNT (1)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)

    0xC0,                               // END_COLLECTION

    // -------- Finger 3 --------
    0x09, 0x22,                         // USAGE (Finger)
    0xA1, 0x02,                         // COLLECTION (Logical)

    0x05, 0x0D,                         // USAGE_PAGE (Digitizers)
    0x09, 0x47,                         // USAGE (Confidence)
    0x09, 0x42,                         // USAGE (Tip Switch)
    0x15, 0x00,                         // LOGICAL_MINIMUM (0)
    0x25, 0x01,                         // LOGICAL_MAXIMUM (1)
    0x75, 0x01,                         // REPORT_SIZE (1)
    0x95, 0x02,                         // REPORT_COUNT (2)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)

    0x09, 0x51,                         // USAGE (Contact Identifier)
    0x25, 0x3F,                         // LOGICAL_MAXIMUM (63)
    0x75, 0x06,                         // REPORT_SIZE (6)
    0x95, 0x01,                         // REPORT_COUNT (1)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)

    // ---- X Axis ----
    0x05, 0x01,                         // USAGE_PAGE (Generic Desktop)
    0x09, 0x30,                         // USAGE (X)
    0x15, 0x00,                         // LOGICAL_MINIMUM (0)
    0x26, 0xFA, 0x08,                          // LOGICAL_MAXIMUM

    0x35, 0x00,                         // PHYSICAL_MINIMUM (0)
    0x46, 0x7D, 0x04,                         // PHYSICAL_MAXIMUM
    0x55, 0x0E,             // UNIT_EXPONENT (-3)
    0x65, 0x11,                      // UNIT (Centimeter)

    0x75, 0x10,                         // REPORT_SIZE (16)
    0x95, 0x01,                         // REPORT_COUNT (1)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)

    // ---- Y Axis ----
    0x09, 0x31,                         // USAGE (Y)
    0x15, 0x00,                         // LOGICAL_MINIMUM (0)
    0x26, 0xFC, 0x05,                          // LOGICAL_MAXIMUM

    0x35, 0x00,                         // PHYSICAL_MINIMUM (0)
    0x46, 0xFE, 0x02,                         // PHYSICAL_MAXIMUM

    0x75, 0x10,                         // REPORT_SIZE (16)
    0x95, 0x01,                         // REPORT_COUNT (1)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)

    0xC0,                               // END_COLLECTION

    // -------- Finger 4 --------
    0x09, 0x22,                         // USAGE (Finger)
    0xA1, 0x02,                         // COLLECTION (Logical)

    0x05, 0x0D,                         // USAGE_PAGE (Digitizers)
    0x09, 0x47,                         // USAGE (Confidence)
    0x09, 0x42,                         // USAGE (Tip Switch)
    0x15, 0x00,                         // LOGICAL_MINIMUM (0)
    0x25, 0x01,                         // LOGICAL_MAXIMUM (1)
    0x75, 0x01,                         // REPORT_SIZE (1)
    0x95, 0x02,                         // REPORT_COUNT (2)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)

    0x09, 0x51,                         // USAGE (Contact Identifier)
    0x25, 0x3F,                         // LOGICAL_MAXIMUM (63)
    0x75, 0x06,                         // REPORT_SIZE (6)
    0x95, 0x01,                         // REPORT_COUNT (1)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)

    // ---- X Axis ----
    0x05, 0x01,                         // USAGE_PAGE (Generic Desktop)
    0x09, 0x30,                         // USAGE (X)
    0x15, 0x00,                         // LOGICAL_MINIMUM (0)
    0x26, 0xFA, 0x08,                          // LOGICAL_MAXIMUM

    0x35, 0x00,                         // PHYSICAL_MINIMUM (0)
    0x46, 0x7D, 0x04,                         // PHYSICAL_MAXIMUM
    0x55, 0x0E,             // UNIT_EXPONENT (-3)
    0x65, 0x11,                      // UNIT (Centimeter)

    0x75, 0x10,                         // REPORT_SIZE (16)
    0x95, 0x01,                         // REPORT_COUNT (1)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)

    // ---- Y Axis ----
    0x09, 0x31,                         // USAGE (Y)
    0x15, 0x00,                         // LOGICAL_MINIMUM (0)
    0x26, 0xFC, 0x05,                          // LOGICAL_MAXIMUM

    0x35, 0x00,                         // PHYSICAL_MINIMUM (0)
    0x46, 0xFE, 0x02,                         // PHYSICAL_MAXIMUM

    0x75, 0x10,                         // REPORT_SIZE (16)
    0x95, 0x01,                         // REPORT_COUNT (1)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)

    0xC0,                               // END_COLLECTION

    0x05, 0x0D,
    0x09, 0x56,
    0x15, 0x00,
    0x27, 0xFF, 0xFF, 0x00, 0x00,
    0x75, 0x10,
    0x95, 0x01,
    0x81, 0x02,

    0x09, 0x54,
    0x25, 0x02,
    0x75, 0x08,
    0x95, 0x01,
    0x81, 0x02,

    0x05, 0x09,
    0x09, 0x01,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x01,
    0x81, 0x02,

    0x95, 0x07,
    0x81, 0x03,

    0xC0,

    0x05, 0x0D,
    0x09, 0x0E,
    0xA1, 0x01,
    0x85, 0x02,

    0x09, 0x55,
    0x15, 0x00,
    0x25, 0x02,
    0x75, 0x08,
    0x95, 0x01,
    0xB1, 0x02,

    0x09, 0x59,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x08,
    0x95, 0x01,
    0xB1, 0x02,

    0xC0
};

const uint16_t ble_mouse_hid_report_len = sizeof(ble_mouse_hid_report_descriptor);
const uint16_t ble_ptp_hid_report_len = sizeof(ble_ptp_hid_report_descriptor);