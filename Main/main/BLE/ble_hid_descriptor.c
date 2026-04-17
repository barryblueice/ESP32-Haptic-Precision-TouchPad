#include <stdio.h>
#include <string.h>

#define REPORTID_TOUCHPAD         0x01
#define REPORTID_MAX_COUNT        0x03
#define REPORTID_PTPHQA           0x04
#define REPORTID_FEATURE          0x05
#define REPORTID_FUNCTION_SWITCH  0x06
#define REPORTID_HAPTIC_FEATURE    0x0C

const uint8_t ble_ptp_hid_report_descriptor[] = {
    
    //TOUCH PAD input TLC
    0x05, 0x0d,                         // USAGE_PAGE (Digitizers)
    0x09, 0x05,                         // USAGE (Touch Pad)
    0xa1, 0x01,                         // COLLECTION (Application)
    0x85, REPORTID_TOUCHPAD,            // REPORT_ID (Touch pad)

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

    0x55, 0x0C,                         // UNIT_EXPONENT (-4)
    0x66, 0x01, 0x10,                   // UNIT (Seconds)
    0x47, 0xff, 0xff, 0x00, 0x00,       // PHYSICAL_MAXIMUM (65535)
    0x27, 0xff, 0xff, 0x00, 0x00,       // LOGICAL_MAXIMUM (65535)
    0x75, 0x10,                         // REPORT_SIZE (16)
    0x95, 0x01,                         // REPORT_COUNT (1)
    0x05, 0x0d,                         // USAGE_PAGE (Digitizers)
    0x09, 0x56,                         // USAGE (Scan Time)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)
    0x09, 0x54,                         // USAGE (Contact count)
    0x25, 0x7f,                         // LOGICAL_MAXIMUM (127)
    0x95, 0x01,                         // REPORT_COUNT (1)
    0x75, 0x08,                         // REPORT_SIZE (8)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)
    0x05, 0x09,                         // USAGE_PAGE (Button)
    0x09, 0x01,                         // USAGE_(Button 1)
    0x25, 0x01,                         // LOGICAL_MAXIMUM (1)
    0x75, 0x01,                         // REPORT_SIZE (1)
    0x95, 0x03,                         // REPORT_COUNT (3)
    0x81, 0x02,                         // INPUT (Data,Var,Abs)
    0x95, 0x05,                         // REPORT_COUNT (5)
    0x81, 0x03,                         // INPUT (Cnst,Var,Abs)
    0x05, 0x0d,                         // USAGE_PAGE (Digitizer)
    0x85, REPORTID_MAX_COUNT,           // REPORT_ID (Feature)
    0x09, 0x55,                         // USAGE (Contact Count Maximum)
    0x09, 0x59,                         // USAGE (Pad TYpe)
    0x75, 0x04,                         // REPORT_SIZE (4)
    0x95, 0x02,                         // REPORT_COUNT (2)
    0x25, 0x0f,                         // LOGICAL_MAXIMUM (15)
    0xb1, 0x02,                         // FEATURE (Data,Var,Abs)
    0x06, 0x00, 0xff,                   // USAGE_PAGE (Vendor Defined)
    0x85, REPORTID_PTPHQA,              // REPORT_ID (PTPHQA)
    0x09, 0xC5,                         // USAGE (Vendor Usage 0xC5)
    0x15, 0x00,                         // LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00,                   // LOGICAL_MAXIMUM (0xff)
    0x75, 0x08,                         // REPORT_SIZE (8)
    0x96, 0x00, 0x01,                   // REPORT_COUNT (0x100 (256))
    0xb1, 0x02,                         // FEATURE (Data,Var,Abs)
    0xc0,                               // END_COLLECTION
    //CONFIG TLC
    0x05, 0x0d,                         // USAGE_PAGE (Digitizer)
    0x09, 0x0E,                         // USAGE (Configuration)
    0xa1, 0x01,                         // COLLECTION (Application)
    0x85, REPORTID_FEATURE,             // REPORT_ID (Feature)
    0x09, 0x22,                         // USAGE (Finger)
    0xa1, 0x02,                         // COLLECTION (logical)
    0x09, 0x52,                         // USAGE (Input Mode)
    0x15, 0x00,                         // LOGICAL_MINIMUM (0)
    0x25, 0x0a,                         // LOGICAL_MAXIMUM (10)
    0x75, 0x08,                         // REPORT_SIZE (8)
    0x95, 0x01,                         // REPORT_COUNT (1)
    0xb1, 0x02,                         // FEATURE (Data,Var,Abs
    0xc0,                               // END_COLLECTION
    0x09, 0x22,                         // USAGE (Finger)
    0xa1, 0x00,                         // COLLECTION (physical)
    0x85, REPORTID_FUNCTION_SWITCH,     // REPORT_ID (Feature)
    0x09, 0x57,                         // USAGE(Surface switch)
    0x09, 0x58,                         // USAGE(Button switch)
    0x75, 0x01,                         // REPORT_SIZE (1)
    0x95, 0x02,                         // REPORT_COUNT (2)
    0x25, 0x01,                         // LOGICAL_MAXIMUM (1)
    0xb1, 0x02,                         // FEATURE (Data,Var,Abs)
    0x95, 0x06,                         // REPORT_COUNT (6)
    0xb1, 0x03,                         // FEATURE (Cnst,Var,Abs)
    0xc0,                               // END_COLLECTION
    0xc0,                               // END_COLLECTION
};

const uint16_t ble_ptp_hid_report_len = sizeof(ble_ptp_hid_report_descriptor);