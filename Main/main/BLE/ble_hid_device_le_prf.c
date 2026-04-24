#include "BLE/hidd_le_prf_int.h"
#include "esp_gatt_common_api.h"
#include <string.h>
#include "esp_log.h"

#include "I2C/SUB_DEV/sub_dev.h"

#include "BLE/BLE_bluedroid.h"

#include "NVS/nvs_handle.h"

#include "SYS/hid_msg.h"

#include "I2C/TP/i2c_hid.h"

#include "GPIO/gpio_handle.h"

#include "sdkconfig.h"

#define TAG "BLE_HID_DEVICE_LE_PRF"

struct prf_char_pres_fmt {
    uint16_t unit;
    uint16_t description;
    uint8_t format;
    uint8_t exponent;
    uint8_t name_space;
};

static hid_report_map_t hid_rpt_map[HID_NUM_REPORTS];

enum {
    BAS_IDX_SVC,

    BAS_IDX_BATT_LVL_CHAR,
    BAS_IDX_BATT_LVL_VAL,
    BAS_IDX_BATT_LVL_NTF_CFG,
    BAS_IDX_BATT_LVL_PRES_FMT,

    BAS_IDX_NB,
};

#define HI_UINT16(a) (((a) >> 8) & 0xFF)
#define LO_UINT16(a) ((a) & 0xFF)
#define PROFILE_NUM            1
#define PROFILE_APP_IDX        0

struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
};

hidd_le_env_t hidd_le_env;

uint8_t hidReportMapLen = 0;
uint8_t hidProtocolMode = HID_PROTOCOL_MODE_REPORT;

static const uint8_t hidInfo[HID_INFORMATION_LEN] = {
    LO_UINT16(0x0111), HI_UINT16(0x0111),
    0x00,
    HID_KBD_FLAGS
};

bool ble_hid_is_connected = false;

static uint16_t hidExtReportRefDesc = ESP_GATT_UUID_BATTERY_LEVEL;

#if CONFIG_BLE_ENABLE_PTP_MODE
static uint8_t hidReportRefPTPIn[HID_REPORT_REF_LEN] =
            { HID_RPT_ID_PTP_IN, HID_REPORT_TYPE_INPUT };
static uint8_t hidReportRefGenericFeature[HID_REPORT_REF_LEN] =
             { REPORTID_BUTTON_PRESS_THRESHOLD, HID_REPORT_TYPE_FEATURE };
#else
static uint8_t hidReportRefMouseIn[HID_REPORT_REF_LEN] =
            { HID_RPT_ID_MOUSE_IN, HID_REPORT_TYPE_INPUT };
static uint8_t hidReportRefGenericFeature[HID_REPORT_REF_LEN] =
             { HID_RPT_ID_FEATURE, HID_REPORT_TYPE_FEATURE };
#endif

static uint16_t hid_le_svc = ATT_SVC_HID;
uint16_t            hid_count = 0;
esp_gatts_incl_svc_desc_t incl_svc = {0};

static uint16_t bas_handle_table[BAS_IDX_NB];

#define CHAR_DECLARATION_SIZE   (sizeof(uint8_t))
static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t include_service_uuid = ESP_GATT_UUID_INCLUDE_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint16_t hid_info_char_uuid = ESP_GATT_UUID_HID_INFORMATION;
static const uint16_t hid_report_map_uuid    = ESP_GATT_UUID_HID_REPORT_MAP;
static const uint16_t hid_control_point_uuid = ESP_GATT_UUID_HID_CONTROL_POINT;
static const uint16_t hid_report_uuid = ESP_GATT_UUID_HID_REPORT;
static const uint16_t hid_proto_mode_uuid = ESP_GATT_UUID_HID_PROTO_MODE;
static const uint16_t hid_mouse_input_uuid = ESP_GATT_UUID_HID_BT_MOUSE_INPUT;
static const uint16_t hid_repot_map_ext_desc_uuid = ESP_GATT_UUID_EXT_RPT_REF_DESCR;
static const uint16_t hid_report_ref_descr_uuid = ESP_GATT_UUID_RPT_REF_DESCR;

#if CONFIG_BLE_ENABLE_PTP_MODE
static const uint16_t char_decl_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t report_reference_uuid = ESP_GATT_UUID_RPT_REF_DESCR;
#endif

// static const uint8_t char_prop_notify = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t char_prop_read = ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t char_prop_write_nr = ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static const uint8_t char_prop_read_write = ESP_GATT_CHAR_PROP_BIT_WRITE|ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t char_prop_read_notify = ESP_GATT_CHAR_PROP_BIT_READ|ESP_GATT_CHAR_PROP_BIT_NOTIFY;

#if CONFIG_BLE_ENABLE_PTP_MODE
static uint8_t ptp_input_mode_data[] = {0x03};
static uint8_t ptp_function_switch_data[] = {0x03};
static uint8_t ptp_max_count_data[] = {0x15};
static uint8_t ptp_button_press_threshold_data[] = {0x02};
static uint8_t ptp_haptic_intensity_data[] = {0x02};
static uint8_t ptp_haptic_waveform_data[] = {
    0x01, 0x10, 0x02, 0x10, 0x03, 0x10, 0x04, 0x10, 0x05, 0x10,
    20, 20, 20, 20, 20
};
static uint8_t ptp_haptic_manual_trigger_data[] = {0x01, 0x02, 0x00, 0x00, 0x00, 0xE8, 0x03};
#else
static uint8_t mouse_feature_report_data[] = {0x02, 0x05, 0x01};
#endif

/// battery Service
static const uint16_t battery_svc = ESP_GATT_UUID_BATTERY_SERVICE_SVC;

static const uint16_t bat_lev_uuid = ESP_GATT_UUID_BATTERY_LEVEL;
static const uint8_t   bat_lev_ccc[2] = {0x00, 0x00};
static const uint16_t char_format_uuid = ESP_GATT_UUID_CHAR_PRESENT_FORMAT;

#if CONFIG_BLE_ENABLE_PTP_MODE
// static uint8_t ptp_touchpad_report_ref[] = {REPORTID_TOUCHPAD, 0x01};
static uint8_t ptp_max_count_ref[] = {REPORTID_MAX_COUNT, 0x03};
static uint8_t ptphqa_report_ref[] = {REPORTID_PTPHQA, 0x03};
static uint8_t ptp_feature_report_ref[] = {REPORTID_FEATURE, 0x03};
static uint8_t ptp_function_switch_ref[] = {REPORTID_FUNCTION_SWITCH, 0x03};
static uint8_t ptp_haptic_intensity_ref[] = {REPORTID_HAPTIC_INTENSITY, 0x03};
static uint8_t ptp_haptic_waveform_ref[] = {REPORTID_HAPTIC_WAVEFORM_LIST, 0x03};
static uint8_t ptp_haptic_manual_trigger_ref[] = {REPORTID_HAPTIC_MANUAL_TRIGGER, 0x02};
#endif

static uint8_t battery_level = 100;

static esp_gatts_attr_db_t bas_att_db[BAS_IDX_NB] = {
    [BAS_IDX_SVC]               =  {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
                                            sizeof(uint16_t), sizeof(battery_svc), (uint8_t *)&battery_svc}},

    [BAS_IDX_BATT_LVL_CHAR]    = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
                                                   CHAR_DECLARATION_SIZE,CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_notify}},

    [BAS_IDX_BATT_LVL_VAL]             	= {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&bat_lev_uuid, ESP_GATT_PERM_READ,
                                                                sizeof(uint8_t),sizeof(uint8_t), &battery_level}},

    [BAS_IDX_BATT_LVL_NTF_CFG]     	=  {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE,
                                                          sizeof(uint16_t),sizeof(bat_lev_ccc), (uint8_t *)bat_lev_ccc}},

    [BAS_IDX_BATT_LVL_PRES_FMT]  = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&char_format_uuid, ESP_GATT_PERM_READ,
                                                        sizeof(struct prf_char_pres_fmt), 0, NULL}},
};


static esp_gatts_attr_db_t hidd_le_gatt_db[HIDD_LE_IDX_NB] = {
    [HIDD_LE_IDX_SVC] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ, sizeof(uint16_t), sizeof(hid_le_svc), (uint8_t *)&hid_le_svc}},

    [HIDD_LE_IDX_INCL_SVC] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&include_service_uuid, ESP_GATT_PERM_READ, sizeof(esp_gatts_incl_svc_desc_t), sizeof(esp_gatts_incl_svc_desc_t), (uint8_t *)&incl_svc}},

    [HIDD_LE_IDX_HID_INFO_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read}},
    [HIDD_LE_IDX_HID_INFO_VAL]  = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_info_char_uuid, ESP_GATT_PERM_READ, sizeof(hids_hid_info_t), sizeof(hidInfo), (uint8_t *)&hidInfo}},

    [HIDD_LE_IDX_HID_CTNL_PT_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_write_nr}},
    [HIDD_LE_IDX_HID_CTNL_PT_VAL]  = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_control_point_uuid, ESP_GATT_PERM_WRITE, sizeof(uint8_t), 0, NULL}},

    [HIDD_LE_IDX_REPORT_MAP_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read}},
    [HIDD_LE_IDX_REPORT_MAP_VAL] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_map_uuid, ESP_GATT_PERM_READ, HIDD_LE_REPORT_MAP_MAX_LEN, 0, NULL}},
    [HIDD_LE_IDX_REPORT_MAP_EXT_REP_REF] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_repot_map_ext_desc_uuid, ESP_GATT_PERM_READ, sizeof(uint16_t), sizeof(uint16_t), (uint8_t *)&hidExtReportRefDesc}},

    [HIDD_LE_IDX_PROTO_MODE_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write}},
    [HIDD_LE_IDX_PROTO_MODE_VAL]  = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_proto_mode_uuid, (ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE), sizeof(uint8_t), sizeof(hidProtocolMode), (uint8_t *)&hidProtocolMode}},

    #if CONFIG_BLE_ENABLE_PTP_MODE
        [HIDD_LE_IDX_REPORT_PTP_IN_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_notify}},
        [HIDD_LE_IDX_REPORT_PTP_IN_VAL]  = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_uuid, ESP_GATT_PERM_READ, HIDD_LE_REPORT_MAX_LEN, 0, NULL}},
        [HIDD_LE_IDX_REPORT_PTP_IN_CCC]  = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, (ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE), sizeof(uint16_t), 0, NULL}},
        [HIDD_LE_IDX_REPORT_PTP_REP_REF] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_ref_descr_uuid, ESP_GATT_PERM_READ, sizeof(hidReportRefPTPIn), sizeof(hidReportRefPTPIn), (uint8_t *)&hidReportRefPTPIn}},

        [HIDD_LE_IDX_REPORT_PTP_FEATURE_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&char_decl_uuid, ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&char_prop_read_write}},
        [HIDD_LE_IDX_REPORT_PTP_FEATURE_VAL] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, HIDD_LE_REPORT_MAX_LEN, sizeof(ptp_input_mode_data), (uint8_t *)&ptp_input_mode_data}},
        [HIDD_LE_IDX_REPORT_PTP_FEATURE_REP_REF] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&report_reference_uuid, ESP_GATT_PERM_READ, sizeof(ptp_feature_report_ref), sizeof(ptp_feature_report_ref), (uint8_t *)&ptp_feature_report_ref}},

        [HIDD_LE_IDX_REPORT_PTPHQA_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&char_decl_uuid, ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&char_prop_read}},
        [HIDD_LE_IDX_REPORT_PTPHQA_VAL]  = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_uuid, ESP_GATT_PERM_READ, HIDD_LE_REPORT_MAX_LEN, 0, NULL}},
        [HIDD_LE_IDX_REPORT_PTPHQA_REP_REF] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&report_reference_uuid, ESP_GATT_PERM_READ, sizeof(ptphqa_report_ref), sizeof(ptphqa_report_ref), (uint8_t *)&ptphqa_report_ref}},

        [HIDD_LE_IDX_REPORT_FUNCTION_SWITCH_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&char_decl_uuid, ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&char_prop_read_write}},
        [HIDD_LE_IDX_REPORT_FUNCTION_SWITCH_VAL]  = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, HIDD_LE_REPORT_MAX_LEN, sizeof(ptp_function_switch_data), (uint8_t *)&ptp_function_switch_data}},
        [HIDD_LE_IDX_REPORT_FUNCTION_SWITCH_REP_REF] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&report_reference_uuid, ESP_GATT_PERM_READ, sizeof(ptp_function_switch_ref), sizeof(ptp_function_switch_ref), (uint8_t *)&ptp_function_switch_ref}},

        [HIDD_LE_IDX_REPORT_HAPTIC_INTENSITY_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&char_decl_uuid, ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&char_prop_read_write}},
        [HIDD_LE_IDX_REPORT_HAPTIC_INTENSITY_VAL]  = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, HIDD_LE_REPORT_MAX_LEN, sizeof(ptp_haptic_intensity_data), (uint8_t *)&ptp_haptic_intensity_data}},
        [HIDD_LE_IDX_REPORT_HAPTIC_INTENSITY_REP_REF] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&report_reference_uuid, ESP_GATT_PERM_READ, sizeof(ptp_haptic_intensity_ref), sizeof(ptp_haptic_intensity_ref), (uint8_t *)&ptp_haptic_intensity_ref}},

        [HIDD_LE_IDX_REPORT_HAPTIC_WAVEFORM_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&char_decl_uuid, ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&char_prop_read_write}},
        [HIDD_LE_IDX_REPORT_HAPTIC_WAVEFORM_VAL]  = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, HIDD_LE_REPORT_MAX_LEN, sizeof(ptp_haptic_waveform_data), (uint8_t *)&ptp_haptic_waveform_data}},
        [HIDD_LE_IDX_REPORT_HAPTIC_WAVEFORM_REP_REF] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&report_reference_uuid, ESP_GATT_PERM_READ, sizeof(ptp_haptic_waveform_ref), sizeof(ptp_haptic_waveform_ref), (uint8_t *)&ptp_haptic_waveform_ref}},

        [HIDD_LE_IDX_REPORT_HAPTIC_MANUAL_TRIGGER_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&char_decl_uuid, ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&char_prop_read_write}},
        [HIDD_LE_IDX_REPORT_HAPTIC_MANUAL_TRIGGER_VAL]  = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, HIDD_LE_REPORT_MAX_LEN, sizeof(ptp_haptic_manual_trigger_data), (uint8_t *)&ptp_haptic_manual_trigger_data}},
        [HIDD_LE_IDX_REPORT_HAPTIC_MANUAL_TRIGGER_REP_REF] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&report_reference_uuid, ESP_GATT_PERM_READ, sizeof(ptp_haptic_manual_trigger_ref), sizeof(ptp_haptic_manual_trigger_ref), (uint8_t *)&ptp_haptic_manual_trigger_ref}},

        [HIDD_LE_IDX_REPORT_MAX_COUNT_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&char_decl_uuid, ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&char_prop_read}},
        [HIDD_LE_IDX_REPORT_MAX_COUNT_VAL]  = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_uuid, ESP_GATT_PERM_READ, HIDD_LE_REPORT_MAX_LEN, sizeof(ptp_max_count_data), (uint8_t *)&ptp_max_count_data}},
        [HIDD_LE_IDX_REPORT_MAX_COUNT_REP_REF] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&report_reference_uuid, ESP_GATT_PERM_READ, sizeof(ptp_max_count_ref), sizeof(ptp_max_count_ref), (uint8_t *)&ptp_max_count_ref}},

    #else

        [HIDD_LE_IDX_REPORT_MOUSE_IN_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_notify}},
        [HIDD_LE_IDX_REPORT_MOUSE_IN_VAL]  = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_uuid, ESP_GATT_PERM_READ, HIDD_LE_REPORT_MAX_LEN, 0, NULL}},
        [HIDD_LE_IDX_REPORT_MOUSE_IN_CCC]  = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, (ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE), sizeof(uint16_t), 0, NULL}},
        [HIDD_LE_IDX_REPORT_MOUSE_REP_REF] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_ref_descr_uuid, ESP_GATT_PERM_READ, sizeof(hidReportRefMouseIn), sizeof(hidReportRefMouseIn), (uint8_t *)&hidReportRefMouseIn}},

        [HIDD_LE_IDX_BOOT_MOUSE_IN_REPORT_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_notify}},
        [HIDD_LE_IDX_BOOT_MOUSE_IN_REPORT_VAL]  = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_mouse_input_uuid, ESP_GATT_PERM_READ, HIDD_LE_BOOT_REPORT_MAX_LEN, 0, NULL}},
        [HIDD_LE_IDX_BOOT_MOUSE_IN_REPORT_NTF_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, (ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE), sizeof(uint16_t), 0, NULL}},

    #endif

    [HIDD_LE_IDX_REPORT_CHAR]    = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write}},
    #if CONFIG_BLE_ENABLE_PTP_MODE
    [HIDD_LE_IDX_REPORT_VAL]     = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, HIDD_LE_REPORT_MAX_LEN, sizeof(ptp_button_press_threshold_data), (uint8_t *)&ptp_button_press_threshold_data}},
    #else
    [HIDD_LE_IDX_REPORT_VAL]     = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_uuid, ESP_GATT_PERM_READ, HIDD_LE_REPORT_MAX_LEN, sizeof(mouse_feature_report_data), (uint8_t *)&mouse_feature_report_data}},
    #endif
    [HIDD_LE_IDX_REPORT_REP_REF] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_ref_descr_uuid, ESP_GATT_PERM_READ, sizeof(hidReportRefGenericFeature), sizeof(hidReportRefGenericFeature), (uint8_t *)&hidReportRefGenericFeature}},
};

void hidd_le_prepare_gatt_table() {
    #if CONFIG_BLE_ENABLE_PTP_MODE
        hidd_le_gatt_db[HIDD_LE_IDX_REPORT_MAP_VAL].att_desc.length = ble_ptp_hid_report_len;
        hidd_le_gatt_db[HIDD_LE_IDX_REPORT_MAP_VAL].att_desc.value = (uint8_t *)ble_ptp_hid_report_descriptor;
    #else
        hidd_le_gatt_db[HIDD_LE_IDX_REPORT_MAP_VAL].att_desc.length = ble_mouse_hid_report_len;
        hidd_le_gatt_db[HIDD_LE_IDX_REPORT_MAP_VAL].att_desc.value = (uint8_t *)ble_mouse_hid_report_descriptor;
    #endif
}

static void hid_add_id_tbl(void);

static uint16_t current_mtu = 23;

void esp_hidd_prf_cb_hdl(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
									esp_ble_gatts_cb_param_t *param) {
    switch(event) {
        case ESP_GATTS_MTU_EVT:
            current_mtu = param->mtu.mtu;
            ESP_LOGI(HID_LE_PRF_TAG, "MTU exchange, MTU %d", param->mtu.mtu);
            break;
        case ESP_GATTS_REG_EVT: {
            esp_ble_gap_config_local_icon (ESP_BLE_APPEARANCE_GENERIC_HID);
            esp_hidd_cb_param_t hidd_param;
            hidd_param.init_finish.state = param->reg.status;
            if(param->reg.app_id == HIDD_APP_ID) {
                hidd_le_env.gatt_if = gatts_if;
                if(hidd_le_env.hidd_cb != NULL) {
                    (hidd_le_env.hidd_cb)(ESP_HIDD_EVENT_REG_FINISH, &hidd_param);
                    hidd_le_create_service(hidd_le_env.gatt_if);
                }
            }
            if(param->reg.app_id == BATTRAY_APP_ID) {
                hidd_param.init_finish.gatts_if = gatts_if;
                 if(hidd_le_env.hidd_cb != NULL) {
                    (hidd_le_env.hidd_cb)(ESP_BAT_EVENT_REG, &hidd_param);
                }

            }

            break;
        }
        case ESP_GATTS_CONF_EVT: {
            break;
        }
        case ESP_GATTS_CREATE_EVT:
            break;
        case ESP_GATTS_CONNECT_EVT: {

            ble_hid_is_connected = true;

            led_send_command(GPIO_LED_3, LED_CMD_STOP, 100, 1000, 0, false);

            led_send_command(GPIO_LED_3, LED_CMD_BLINK, 500, 2000, 3, false);

            esp_hidd_cb_param_t cb_param = {0};
			ESP_LOGI(HID_LE_PRF_TAG, "HID connection establish, conn_id = %x",param->connect.conn_id);
			memcpy(cb_param.connect.remote_bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            cb_param.connect.conn_id = param->connect.conn_id;
            hidd_clcb_alloc(param->connect.conn_id, param->connect.remote_bda);
            esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_NO_MITM);
            if(hidd_le_env.hidd_cb != NULL) {
                (hidd_le_env.hidd_cb)(ESP_HIDD_EVENT_BLE_CONNECT, &cb_param);
            }
            break;
        }
        case ESP_GATTS_DISCONNECT_EVT: {

            ble_hid_is_connected = false;

            led_send_command(GPIO_LED_3, LED_CMD_BLINK, 100, 1000, 2, true);

			 if(hidd_le_env.hidd_cb != NULL) {
                    (hidd_le_env.hidd_cb)(ESP_HIDD_EVENT_BLE_DISCONNECT, NULL);
             }
            hidd_clcb_dealloc(param->disconnect.conn_id);
            break;
        }
        case ESP_GATTS_CLOSE_EVT:
            break;
        case ESP_GATTS_WRITE_EVT: {
            #if CONFIG_BLE_ENABLE_PTP_MODE
                if (param->write.need_rsp) {
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
                }

                if (param->write.handle == hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_PTP_IN_CCC]) {
                    uint16_t value = param->write.value[0] | (param->write.value[1] << 8);
                    if (value == 0x0001) {
                        ESP_LOGI("HID_DEV", "PTP Notification Enabled!");
                    } else if (value == 0x0000) {
                        ESP_LOGI("HID_DEV", "PTP Notification Disabled!");
                    }
                }

                if (param->write.handle == hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_PTP_FEATURE_VAL] &&
                    param->write.len >= 1) {
                    ptp_input_mode_data[0] = param->write.value[0];

                    if (ptp_input_mode_data[0] == 0x03) {
                        current_tp_mode = PTP_MODE;
                        touchpad_mode_set(true);
                    } else {
                        current_tp_mode = MOUSE_MODE;
                        touchpad_mode_set(false);
                    }
                }

                if (param->write.handle == hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_FUNCTION_SWITCH_VAL] &&
                    param->write.len >= 1) {
                    ptp_function_switch_data[0] = param->write.value[0];
                }

                if (param->write.handle == hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_VAL] &&
                    param->write.len >= 1) {
                    uint8_t threshold = param->write.value[0];
                    if (threshold < 0x01) {
                        threshold = 0x01;
                    } else if (threshold > 0x03) {
                        threshold = 0x03;
                    }
                    ptp_button_press_threshold_data[0] = threshold;
                    ptp_button_press_threshold = threshold;
                }

                if (param->write.handle == hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_HAPTIC_INTENSITY_VAL] &&
                    param->write.len >= 1) {
                    uint8_t intensity = param->write.value[0];
                    if (intensity > 0x04) {
                        intensity = 0x04;
                    }
                    ptp_haptic_intensity_data[0] = intensity;
                    ptp_haptic_click_intensity = intensity;
                }

                if (param->write.handle == hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_HAPTIC_WAVEFORM_VAL] &&
                    param->write.len > 0) {
                    size_t copy_len = param->write.len;
                    if (copy_len > sizeof(ptp_haptic_waveform_data)) {
                        copy_len = sizeof(ptp_haptic_waveform_data);
                    }
                    memcpy(ptp_haptic_waveform_data, param->write.value, copy_len);
                }

                if (param->write.handle == hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_HAPTIC_MANUAL_TRIGGER_VAL] &&
                    param->write.len > 0) {
                    size_t copy_len = param->write.len;
                    if (copy_len > sizeof(ptp_haptic_manual_trigger_data)) {
                        copy_len = sizeof(ptp_haptic_manual_trigger_data);
                    }
                    memcpy(ptp_haptic_manual_trigger_data, param->write.value, copy_len);
                }
            #endif
            break;
        }
        case ESP_GATTS_READ_EVT: {
            #if CONFIG_BLE_ENABLE_PTP_MODE
                if (!param->read.need_rsp) {
                    break;
                }

                if (param->read.handle == hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_PTPHQA_VAL]) {
                    ESP_LOGI(HID_LE_PRF_TAG, "GATT read event, handle = %d", param->read.handle);

                    esp_gatt_rsp_t rsp;
                    memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
                    rsp.attr_value.handle = param->read.handle;

                    uint16_t total_hqa_len = 256;
                    uint16_t offset = param->read.offset;

                    uint16_t max_payload = current_mtu - 1;
                    if (offset >= total_hqa_len) {
                        rsp.attr_value.len = 0;
                        rsp.attr_value.offset = offset;
                        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
                        break;
                    }

                    uint16_t remaining = total_hqa_len - offset;
                    uint16_t send_len = (remaining > max_payload) ? max_payload : remaining;

                    rsp.attr_value.len = send_len;
                    rsp.attr_value.offset = offset;

                    memset(rsp.attr_value.value, 0, send_len);

                    esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
                }
            #endif
            break;
        }
        case ESP_GATTS_CREAT_ATTR_TAB_EVT: {
            if (param->add_attr_tab.status == ESP_GATT_OK &&
                param->add_attr_tab.num_handle == BAS_IDX_NB) {
                memcpy(bas_handle_table, param->add_attr_tab.handles, sizeof(bas_handle_table));
            }
            if (param->add_attr_tab.num_handle == BAS_IDX_NB &&
                param->add_attr_tab.svc_uuid.uuid.uuid16 == ESP_GATT_UUID_BATTERY_SERVICE_SVC &&
                param->add_attr_tab.status == ESP_GATT_OK) {
                incl_svc.start_hdl = param->add_attr_tab.handles[BAS_IDX_SVC];
                incl_svc.end_hdl = incl_svc.start_hdl + BAS_IDX_NB -1;
                ESP_LOGI(HID_LE_PRF_TAG, "%s(), start added the hid service to the stack database. incl_handle = %d",
                           __func__, incl_svc.start_hdl);
                esp_ble_gatts_create_attr_tab(hidd_le_gatt_db, gatts_if, HIDD_LE_IDX_NB, 0);
            }
            if (param->add_attr_tab.num_handle == HIDD_LE_IDX_NB &&
                param->add_attr_tab.status == ESP_GATT_OK) {
                memcpy(hidd_le_env.hidd_inst.att_tbl, param->add_attr_tab.handles,
                            HIDD_LE_IDX_NB*sizeof(uint16_t));
                ESP_LOGI(HID_LE_PRF_TAG, "hid svc handle = %x",hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_SVC]);
                hid_add_id_tbl();
		        esp_ble_gatts_start_service(hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_SVC]);
            } else {
                esp_ble_gatts_start_service(param->add_attr_tab.handles[0]);
            }
            break;
         }

        default:
            break;
    }
}

void hidd_le_create_service(esp_gatt_if_t gatts_if) {
    esp_ble_gatts_create_attr_tab(bas_att_db, gatts_if, BAS_IDX_NB, 0);

}

void hidd_le_init(void) {

    memset(&hidd_le_env, 0, sizeof(hidd_le_env_t));
}

void hidd_clcb_alloc (uint16_t conn_id, esp_bd_addr_t bda) {
    uint8_t                   i_clcb = 0;
    hidd_clcb_t      *p_clcb = NULL;

    for (i_clcb = 0, p_clcb= hidd_le_env.hidd_clcb; i_clcb < HID_MAX_APPS; i_clcb++, p_clcb++) {
        if (!p_clcb->in_use) {
            p_clcb->in_use      = true;
            p_clcb->conn_id     = conn_id;
            p_clcb->connected   = true;
            memcpy (p_clcb->remote_bda, bda, ESP_BD_ADDR_LEN);
            break;
        }
    }
    return;
}

bool hidd_clcb_dealloc (uint16_t conn_id) {
    uint8_t              i_clcb = 0;
    hidd_clcb_t      *p_clcb = NULL;

    for (i_clcb = 0, p_clcb= hidd_le_env.hidd_clcb; i_clcb < HID_MAX_APPS; i_clcb++, p_clcb++) {
            memset(p_clcb, 0, sizeof(hidd_clcb_t));
            return true;
    }

    return false;
}

static struct gatts_profile_inst heart_rate_profile_tab[PROFILE_NUM] = {
    [PROFILE_APP_IDX] = {
        .gatts_cb = esp_hidd_prf_cb_hdl,
        .gatts_if = ESP_GATT_IF_NONE,
    },

};

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param) {
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            heart_rate_profile_tab[PROFILE_APP_IDX].gatts_if = gatts_if;
        } else {
            ESP_LOGI(HID_LE_PRF_TAG, "Reg app failed, app_id %04x, status %d",
                    param->reg.app_id,
                    param->reg.status);
            return;
        }
    }

    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            if (gatts_if == ESP_GATT_IF_NONE ||
                    gatts_if == heart_rate_profile_tab[idx].gatts_if) {
                if (heart_rate_profile_tab[idx].gatts_cb) {
                    heart_rate_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}


esp_err_t hidd_register_cb(void) {
	esp_err_t status;
	status = esp_ble_gatts_register_callback(gatts_event_handler);
	return status;
}

void hidd_set_attr_value(uint16_t handle, uint16_t val_len, const uint8_t *value) {
    hidd_inst_t *hidd_inst = &hidd_le_env.hidd_inst;
    if(hidd_inst->att_tbl[HIDD_LE_IDX_HID_INFO_VAL] <= handle &&
        hidd_inst->att_tbl[HIDD_LE_IDX_REPORT_REP_REF] >= handle) {
        esp_ble_gatts_set_attr_value(handle, val_len, value);
    } else {
        ESP_LOGE(HID_LE_PRF_TAG, "%s error:Invalid handle value.",__func__);
    }
    return;
}

void hidd_get_attr_value(uint16_t handle, uint16_t *length, uint8_t **value) {
    hidd_inst_t *hidd_inst = &hidd_le_env.hidd_inst;
    if(hidd_inst->att_tbl[HIDD_LE_IDX_HID_INFO_VAL] <= handle &&
        hidd_inst->att_tbl[HIDD_LE_IDX_REPORT_REP_REF] >= handle){
        esp_ble_gatts_get_attr_value(handle, length, (const uint8_t **)value);
    } else {
        ESP_LOGE(HID_LE_PRF_TAG, "%s error:Invalid handle value.", __func__);
    }

    return;
}

static void hid_add_id_tbl(void) {
    memset(hid_rpt_map, 0, sizeof(hid_rpt_map));

    hid_rpt_map[0].id = REPORTID_TOUCHPAD;
    hid_rpt_map[0].type = HID_REPORT_TYPE_INPUT;
    #if CONFIG_BLE_ENABLE_PTP_MODE
        hid_rpt_map[0].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_PTP_IN_VAL];
        hid_rpt_map[0].cccdHandle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_PTP_IN_CCC];
    #else
        hid_rpt_map[0].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_MOUSE_IN_VAL];
        hid_rpt_map[0].cccdHandle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_MOUSE_IN_CCC];
    #endif
    hid_rpt_map[0].mode = HID_PROTOCOL_MODE_REPORT;

    #if CONFIG_BLE_ENABLE_PTP_MODE
    hid_rpt_map[1].id = REPORTID_BUTTON_PRESS_THRESHOLD;
    hid_rpt_map[1].type = HID_REPORT_TYPE_FEATURE;
    hid_rpt_map[1].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_VAL];
    hid_rpt_map[1].cccdHandle = 0;
    hid_rpt_map[1].mode = HID_PROTOCOL_MODE_REPORT;

    hid_rpt_map[2].id = REPORTID_FEATURE;
    hid_rpt_map[2].type = HID_REPORT_TYPE_FEATURE;
    hid_rpt_map[2].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_PTP_FEATURE_VAL];
    hid_rpt_map[2].cccdHandle = 0;
    hid_rpt_map[2].mode = HID_PROTOCOL_MODE_REPORT;

    hid_rpt_map[3].id = REPORTID_PTPHQA;
    hid_rpt_map[3].type = HID_REPORT_TYPE_FEATURE;
    hid_rpt_map[3].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_PTPHQA_VAL];
    hid_rpt_map[3].cccdHandle = 0;
    hid_rpt_map[3].mode = HID_PROTOCOL_MODE_REPORT;

    hid_rpt_map[4].id = REPORTID_MAX_COUNT;
    hid_rpt_map[4].type = HID_REPORT_TYPE_FEATURE;
    hid_rpt_map[4].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_MAX_COUNT_VAL];
    hid_rpt_map[4].cccdHandle = 0;
    hid_rpt_map[4].mode = HID_PROTOCOL_MODE_REPORT;

    hid_rpt_map[5].id = REPORTID_FUNCTION_SWITCH;
    hid_rpt_map[5].type = HID_REPORT_TYPE_FEATURE;
    hid_rpt_map[5].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_FUNCTION_SWITCH_VAL];
    hid_rpt_map[5].cccdHandle = 0;
    hid_rpt_map[5].mode = HID_PROTOCOL_MODE_REPORT;

    hid_rpt_map[6].id = REPORTID_HAPTIC_INTENSITY;
    hid_rpt_map[6].type = HID_REPORT_TYPE_FEATURE;
    hid_rpt_map[6].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_HAPTIC_INTENSITY_VAL];
    hid_rpt_map[6].cccdHandle = 0;
    hid_rpt_map[6].mode = HID_PROTOCOL_MODE_REPORT;

    hid_rpt_map[7].id = REPORTID_HAPTIC_WAVEFORM_LIST;
    hid_rpt_map[7].type = HID_REPORT_TYPE_FEATURE;
    hid_rpt_map[7].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_HAPTIC_WAVEFORM_VAL];
    hid_rpt_map[7].cccdHandle = 0;
    hid_rpt_map[7].mode = HID_PROTOCOL_MODE_REPORT;

    hid_rpt_map[8].id = REPORTID_HAPTIC_MANUAL_TRIGGER;
    hid_rpt_map[8].type = HID_REPORT_TYPE_OUTPUT;
    hid_rpt_map[8].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_HAPTIC_MANUAL_TRIGGER_VAL];
    hid_rpt_map[8].cccdHandle = 0;
    hid_rpt_map[8].mode = HID_PROTOCOL_MODE_REPORT;

    hid_dev_register_reports(9, hid_rpt_map);
    #else
    hid_rpt_map[1].id = 0x02;
    hid_rpt_map[1].type = HID_REPORT_TYPE_FEATURE;
    hid_rpt_map[1].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_VAL];
    hid_rpt_map[1].cccdHandle = 0;
    hid_rpt_map[1].mode = HID_PROTOCOL_MODE_REPORT;

    hid_dev_register_reports(2, hid_rpt_map);
    #endif
}

void update_battery_level(esp_gatt_if_t gatts_if, uint16_t conn_id, uint8_t level) {
    esp_ble_gatts_set_attr_value(bas_handle_table[BAS_IDX_BATT_LVL_VAL], sizeof(uint8_t), &level);
    esp_ble_gatts_send_indicate(gatts_if, conn_id,
                                bas_handle_table[BAS_IDX_BATT_LVL_VAL],
                                sizeof(uint8_t), &level, false);
}

void battery_ble_notify_task(void *pvParameters) {
    while (1) {
        int raw_battery = get_battery_percentage();

        uint8_t current_battery_level = raw_battery;

        if (ble_hid_is_connected) {

            update_battery_level(hidd_le_env.gatt_if, ble_conn_id, current_battery_level);
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
