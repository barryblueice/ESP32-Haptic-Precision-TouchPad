#include "SYS/input_pipeline.h"
#include "BLE/hidd_le_prf_int.h"
#include "esp_gatt_common_api.h"
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include "esp_gap_ble_api.h"
#include "nvs.h"
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

static uint8_t hidReportRefMouseIn[HID_REPORT_REF_LEN] =
            { HID_RPT_ID_MOUSE_IN, HID_REPORT_TYPE_INPUT };
static uint8_t hidReportRefGenericFeature[HID_REPORT_REF_LEN] =
             { HID_RPT_ID_FEATURE, HID_REPORT_TYPE_FEATURE };

static uint16_t hid_le_svc = ATT_SVC_HID;
uint16_t            hid_count = 0;
esp_gatts_incl_svc_desc_t incl_svc = {0};

static uint16_t bas_handle_table[BAS_IDX_NB];

/* BTC serializes GATT and GAP callbacks. CCCDs belong to a bonded peer, not
 * the shared attribute table, and must survive a controller restart. */
enum { CCC_MOUSE = 1, CCC_BOOT = 2, CCC_BATTERY = 4, CCC_VALID = 0x80 };
static struct {
    bool connected, secured, bonded, was_bonded, ccc_known;
    uint16_t conn;
    esp_bd_addr_t peer;
    char key[14];
    uint8_t ccc, written;
} hid_peer;

static bool peer_bond_key(const uint8_t *peer, char key[14])
{
    int count = esp_ble_get_bond_device_num();
    if (count <= 0) return false;
    esp_ble_bond_dev_t *bonds = calloc(count, sizeof(*bonds));
    if (!bonds) return false;
    bool found = false;
    if (esp_ble_get_bond_device_list(&count, bonds) == ESP_OK) {
        for (int i = 0; i < count; ++i) {
            esp_ble_bond_dev_t *bond = &bonds[i];
            bool identity = (bond->bond_key.key_mask & ESP_LE_KEY_PID) != 0;
            const uint8_t *address = identity ? bond->bond_key.pid_key.static_addr : bond->bd_addr;
            if (memcmp(peer, bond->bd_addr, ESP_BD_ADDR_LEN) &&
                memcmp(peer, address, ESP_BD_ADDR_LEN)) continue;
            static const char hex[] = "0123456789abcdef";
            key[0] = hex[(identity ? bond->bond_key.pid_key.addr_type : bond->bd_addr_type) & 0xf];
            for (unsigned j = 0; j < ESP_BD_ADDR_LEN; ++j) {
                key[1 + j * 2] = hex[address[j] >> 4];
                key[2 + j * 2] = hex[address[j] & 0xf];
            }
            key[13] = 0;
            found = true;
            break;
        }
    }
    free(bonds);
    return found;
}

static esp_err_t peer_ccc_load(uint8_t *ccc)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ble_ccc", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    uint8_t value = 0;
    err = nvs_get_u8(handle, hid_peer.key, &value);
    nvs_close(handle);
    if (err == ESP_OK && (value & ~7U) != CCC_VALID) return ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) *ccc = value & 7U;
    return err;
}

static esp_err_t peer_ccc_save(uint8_t ccc)
{
    uint8_t stored;
    if (peer_ccc_load(&stored) == ESP_OK && stored == ccc) return ESP_OK;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ble_ccc", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, hid_peer.key, CCC_VALID | ccc);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) ESP_LOGE(TAG, "CCCD persistence failed: %s", esp_err_to_name(err));
    return err;
}

void ble_hid_auth_complete(const uint8_t *peer, bool success)
{
    if (!hid_peer.connected) return;
    char key[14] = {0};
    bool bonded = peer_bond_key(peer, key);
    /* Accept the resolved identity as well as the address used on connect. */
    if (memcmp(peer, hid_peer.peer, ESP_BD_ADDR_LEN)) {
        char connected_key[14] = {0};
        if (!bonded || !peer_bond_key(hid_peer.peer, connected_key) ||
            memcmp(key, connected_key, sizeof(key))) return;
    }
    if (!success) {
        hid_peer.secured = false;
        ble_input_subscription(hid_peer.conn, false);
        return;
    }
    if (hid_peer.secured) return;
    hid_peer.secured = true;
    hid_peer.bonded = bonded;
    if (bonded) {
        memcpy(hid_peer.key, key, sizeof(key));
        uint8_t stored = 0;
        esp_err_t err = hid_peer.was_bonded ? peer_ccc_load(&stored) : ESP_ERR_NVS_NOT_FOUND;
        if (err == ESP_OK) hid_peer.ccc |= stored & ~hid_peer.written;
        hid_peer.ccc_known = !hid_peer.was_bonded || err == ESP_OK || (hid_peer.written & CCC_MOUSE);
        if (hid_peer.ccc_known) {
            (void)peer_ccc_save(hid_peer.ccc);
        }
        if (hid_peer.was_bonded && err != ESP_OK && !(hid_peer.written & CCC_MOUSE)) {
            /* Older firmware did not persist CCCDs. Ask cached hosts to discover
             * the services again instead of assuming notification consent. */
            ESP_LOGW(TAG, "No saved mouse CCCD; requesting service rediscovery");
            esp_err_t change = esp_ble_gatts_send_service_change_indication(hidd_le_env.gatt_if, hid_peer.peer);
            if (change != ESP_OK) ESP_LOGW(TAG, "Service rediscovery request failed: %s", esp_err_to_name(change));
        }
    }
    ESP_LOGI(TAG, "Authenticated conn=%u bonded=%u mouse_notify=%u", hid_peer.conn,
             hid_peer.bonded, !!(hid_peer.ccc & CCC_MOUSE));
    ble_input_subscription(hid_peer.conn, (hid_peer.ccc & CCC_MOUSE) != 0);
}

static uint8_t ccc_for_handle(uint16_t handle)
{
    if (!handle) return 0;
    if (handle == hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_MOUSE_IN_CCC]) return CCC_MOUSE;
    if (handle == hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_BOOT_MOUSE_IN_REPORT_NTF_CFG]) return CCC_BOOT;
    if (handle == bas_handle_table[BAS_IDX_BATT_LVL_NTF_CFG]) return CCC_BATTERY;
    return 0;
}

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

// static const uint8_t char_prop_notify = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t char_prop_read = ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t char_prop_write_nr = ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static const uint8_t char_prop_read_write = ESP_GATT_CHAR_PROP_BIT_WRITE|ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t char_prop_read_notify = ESP_GATT_CHAR_PROP_BIT_READ|ESP_GATT_CHAR_PROP_BIT_NOTIFY;

static uint8_t mouse_feature_report_data[] = {0x02, 0x05, 0x01};

/// battery Service
static const uint16_t battery_svc = ESP_GATT_UUID_BATTERY_SERVICE_SVC;

static const uint16_t bat_lev_uuid = ESP_GATT_UUID_BATTERY_LEVEL;
static const uint8_t   bat_lev_ccc[2] = {0x00, 0x00};
static const uint16_t char_format_uuid = ESP_GATT_UUID_CHAR_PRESENT_FORMAT;

static uint8_t ptp_haptic_intensity_data[] = {63};

static uint8_t ptp_haptic_intensity_ref[] = {REPORTID_HAPTIC_INTENSITY, 0x03};

static uint8_t battery_level = 100;

static esp_gatts_attr_db_t bas_att_db[BAS_IDX_NB] = {
    [BAS_IDX_SVC]               =  {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
                                            sizeof(uint16_t), sizeof(battery_svc), (uint8_t *)&battery_svc}},

    [BAS_IDX_BATT_LVL_CHAR]    = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
                                                   CHAR_DECLARATION_SIZE,CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_notify}},

    [BAS_IDX_BATT_LVL_VAL]             	= {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&bat_lev_uuid, ESP_GATT_PERM_READ,
                                                                sizeof(uint8_t),sizeof(uint8_t), &battery_level}},

    [BAS_IDX_BATT_LVL_NTF_CFG]     	=  {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE,
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

        [HIDD_LE_IDX_REPORT_MOUSE_IN_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_notify}},
        [HIDD_LE_IDX_REPORT_MOUSE_IN_VAL]  = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_uuid, ESP_GATT_PERM_READ, HIDD_LE_REPORT_MAX_LEN, 0, NULL}},
        [HIDD_LE_IDX_REPORT_MOUSE_IN_CCC]  = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, (ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE), sizeof(uint16_t), 0, NULL}},
        [HIDD_LE_IDX_REPORT_MOUSE_REP_REF] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_ref_descr_uuid, ESP_GATT_PERM_READ, sizeof(hidReportRefMouseIn), sizeof(hidReportRefMouseIn), (uint8_t *)&hidReportRefMouseIn}},

        [HIDD_LE_IDX_BOOT_MOUSE_IN_REPORT_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_notify}},
        [HIDD_LE_IDX_BOOT_MOUSE_IN_REPORT_VAL]  = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_mouse_input_uuid, ESP_GATT_PERM_READ, HIDD_LE_BOOT_REPORT_MAX_LEN, 0, NULL}},
        [HIDD_LE_IDX_BOOT_MOUSE_IN_REPORT_NTF_CFG] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, (ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE), sizeof(uint16_t), 0, NULL}},

        [HIDD_LE_IDX_REPORT_HAPTIC_INTENSITY_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&char_prop_read_write}},
        [HIDD_LE_IDX_REPORT_HAPTIC_INTENSITY_VAL]  = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(ptp_haptic_intensity_data), sizeof(ptp_haptic_intensity_data), (uint8_t *)&ptp_haptic_intensity_data}},
        [HIDD_LE_IDX_REPORT_HAPTIC_INTENSITY_REP_REF] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_ref_descr_uuid, ESP_GATT_PERM_READ, sizeof(ptp_haptic_intensity_ref), sizeof(ptp_haptic_intensity_ref), (uint8_t *)&ptp_haptic_intensity_ref}},

    [HIDD_LE_IDX_REPORT_CHAR]    = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write}},
    [HIDD_LE_IDX_REPORT_VAL]     = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_uuid, ESP_GATT_PERM_READ, HIDD_LE_REPORT_MAX_LEN, sizeof(mouse_feature_report_data), (uint8_t *)&mouse_feature_report_data}},
    [HIDD_LE_IDX_REPORT_REP_REF] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&hid_report_ref_descr_uuid, ESP_GATT_PERM_READ, sizeof(hidReportRefGenericFeature), sizeof(hidReportRefGenericFeature), (uint8_t *)&hidReportRefGenericFeature}},

};

void hidd_le_prepare_gatt_table() {
    ptp_haptic_intensity_data[0] = ptp_haptic_click_intensity_get();
    hidd_le_gatt_db[HIDD_LE_IDX_REPORT_MAP_VAL].att_desc.max_length = ble_mouse_hid_report_len;
    hidd_le_gatt_db[HIDD_LE_IDX_REPORT_MAP_VAL].att_desc.length = ble_mouse_hid_report_len;
    hidd_le_gatt_db[HIDD_LE_IDX_REPORT_MAP_VAL].att_desc.value = (uint8_t *)ble_mouse_hid_report_descriptor;
}

static void hid_add_id_tbl(void);

void esp_hidd_prf_cb_hdl(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
									esp_ble_gatts_cb_param_t *param) {
    switch(event) {
        case ESP_GATTS_MTU_EVT:
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
            ble_input_complete(param->conf.conn_id, param->conf.handle, param->conf.status == ESP_GATT_OK);
            break;
        }
        case ESP_GATTS_CREATE_EVT:
            break;
        case ESP_GATTS_CONNECT_EVT: {
            if (hid_peer.connected && hid_peer.conn == param->connect.conn_id &&
                !memcmp(hid_peer.peer, param->connect.remote_bda, ESP_BD_ADDR_LEN)) break;
            memset(&hid_peer, 0, sizeof(hid_peer));
            hid_peer.connected = true;
            hid_peer.conn = param->connect.conn_id;
            memcpy(hid_peer.peer, param->connect.remote_bda, ESP_BD_ADDR_LEN);
            hid_peer.was_bonded = peer_bond_key(hid_peer.peer, hid_peer.key);
            ble_hid_is_connected = true;
            ble_input_connection(true, param->connect.conn_id);

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
            if (!hid_peer.connected || hid_peer.conn != param->disconnect.conn_id ||
                memcmp(hid_peer.peer, param->disconnect.remote_bda, ESP_BD_ADDR_LEN)) break;
            memset(&hid_peer, 0, sizeof(hid_peer));
            ble_hid_is_connected = false;
            ble_input_connection(false, param->disconnect.conn_id);

            led_send_command(GPIO_LED_3, LED_CMD_BLINK, 100, 1000, 2, true);

			 if(hidd_le_env.hidd_cb != NULL) {
                    (hidd_le_env.hidd_cb)(ESP_HIDD_EVENT_BLE_DISCONNECT, NULL);
             }
            hidd_clcb_dealloc(param->disconnect.conn_id);
            break;
        }
        case ESP_GATTS_CONGEST_EVT:
            ble_input_congestion(param->congest.conn_id, param->congest.congested);
            break;
        case ESP_GATTS_CLOSE_EVT:
            break;
        case ESP_GATTS_WRITE_EVT: {
            if (!hid_peer.connected || param->write.conn_id != hid_peer.conn ||
                memcmp(param->write.bda, hid_peer.peer, ESP_BD_ADDR_LEN)) break;
            uint8_t ccc = ccc_for_handle(param->write.handle);
            if (ccc) {
                esp_gatt_status_t status = ESP_GATT_OK;
                if (param->write.is_prep) status = ESP_GATT_REQ_NOT_SUPPORTED;
                else if (param->write.offset) status = ESP_GATT_INVALID_OFFSET;
                else if (param->write.len != 2 || !param->write.value) status = ESP_GATT_INVALID_ATTR_LEN;
                else if (param->write.value[1] || param->write.value[0] > 1) status = ESP_GATT_CCC_CFG_ERR;
                else {
                    uint8_t updated = (hid_peer.ccc & ~ccc) | (param->write.value[0] ? ccc : 0);
                    bool known = hid_peer.ccc_known || ccc == CCC_MOUSE;
                    if (hid_peer.secured && hid_peer.bonded && known && peer_ccc_save(updated) != ESP_OK)
                        status = ESP_GATT_ERR_UNLIKELY;
                    else {
                        hid_peer.ccc = updated;
                        hid_peer.written |= ccc;
                        hid_peer.ccc_known = known;
                        ble_input_subscription(hid_peer.conn, hid_peer.secured && (updated & CCC_MOUSE));
                        ESP_LOGI(TAG, "CCCD conn=%u handle=%u value=%u", hid_peer.conn,
                                 param->write.handle, param->write.value[0]);
                    }
                }
                if (param->write.need_rsp) esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                    param->write.trans_id, status, NULL);
                break;
            }

                if (param->write.handle == hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_HAPTIC_INTENSITY_VAL]) {
                    esp_gatt_status_t status = ESP_GATT_OK;
                    if (param->write.is_prep) status = ESP_GATT_REQ_NOT_SUPPORTED;
                    else if (param->write.offset != 0) status = ESP_GATT_INVALID_OFFSET;
                    else if (param->write.len != 1) status = ESP_GATT_INVALID_ATTR_LEN;
                    else if (param->write.value[0] > 100) status = ESP_GATT_OUT_OF_RANGE;
                    else if (ptp_haptic_click_intensity_set_report(param->write.value, param->write.len, true) != ESP_OK)
                        status = ESP_GATT_ERR_UNLIKELY;
                    if (param->write.need_rsp) {
                        esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                                    param->write.trans_id, status, NULL);
                    }
                    break;
                }
            break;
        }
        case ESP_GATTS_READ_EVT: {
            if (!hid_peer.connected || param->read.conn_id != hid_peer.conn ||
                memcmp(param->read.bda, hid_peer.peer, ESP_BD_ADDR_LEN)) break;
            uint8_t ccc = ccc_for_handle(param->read.handle);
            if (ccc) {
                esp_gatt_rsp_t rsp = {0};
                uint8_t value[2] = {(hid_peer.ccc & ccc) ? 1 : 0, 0};
                rsp.attr_value.handle = param->read.handle;
                rsp.attr_value.offset = param->read.offset;
                if (param->read.offset <= 2) {
                    rsp.attr_value.len = 2 - param->read.offset;
                    memcpy(rsp.attr_value.value, value + param->read.offset, rsp.attr_value.len);
                }
                if (param->read.need_rsp) esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                    param->read.trans_id, param->read.offset > 2 ? ESP_GATT_INVALID_OFFSET : ESP_GATT_OK, &rsp);
                break;
            }
                if (param->read.handle == hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_HAPTIC_INTENSITY_VAL]) {
                    esp_gatt_rsp_t rsp = {0};
                    rsp.attr_value.handle = param->read.handle;
                    rsp.attr_value.offset = param->read.offset;
                    rsp.attr_value.len = param->read.offset == 0 ? 1 : 0;
                    rsp.attr_value.value[0] = ptp_haptic_click_intensity_get();
                    if (param->read.need_rsp) {
                        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                            param->read.offset > 1 ? ESP_GATT_INVALID_OFFSET : ESP_GATT_OK, &rsp);
                    }
                    break;
                }
            break;
        }
        case ESP_GATTS_CREAT_ATTR_TAB_EVT: {
            /* Bluedroid reports validation failures with handles == NULL. */
            if (param->add_attr_tab.status != ESP_GATT_OK) {
                ESP_LOGE(HID_LE_PRF_TAG, "Attribute table creation failed: status=0x%x, count=%u",
                         param->add_attr_tab.status, param->add_attr_tab.num_handle);
                break;
            }
            if (!param->add_attr_tab.handles || !param->add_attr_tab.num_handle ||
                param->add_attr_tab.svc_uuid.len != ESP_UUID_LEN_16) {
                ESP_LOGE(HID_LE_PRF_TAG, "Invalid attribute table creation result");
                break;
            }
            uint16_t service = param->add_attr_tab.svc_uuid.uuid.uuid16;
            uint16_t expected = service == ESP_GATT_UUID_BATTERY_SERVICE_SVC ? BAS_IDX_NB :
                                service == ATT_SVC_HID ? HIDD_LE_IDX_NB : 0;
            if (!expected || param->add_attr_tab.num_handle != expected) {
                ESP_LOGE(HID_LE_PRF_TAG, "Unexpected attribute table: uuid=0x%x, count=%u",
                         service, param->add_attr_tab.num_handle);
                break;
            }
            for (unsigned i = 0; i < expected; ++i) {
                if (!param->add_attr_tab.handles[i]) {
                    ESP_LOGE(HID_LE_PRF_TAG, "Invalid zero attribute handle at index %u", i);
                    return;
                }
            }
            if (service == ESP_GATT_UUID_BATTERY_SERVICE_SVC) {
                memcpy(bas_handle_table, param->add_attr_tab.handles, sizeof(bas_handle_table));
                incl_svc.start_hdl = bas_handle_table[BAS_IDX_SVC];
                incl_svc.end_hdl = bas_handle_table[BAS_IDX_NB - 1];
                esp_err_t err = esp_ble_gatts_start_service(bas_handle_table[BAS_IDX_SVC]);
                if (err != ESP_OK) {
                    ESP_LOGE(HID_LE_PRF_TAG, "Battery service start failed: %s", esp_err_to_name(err));
                    break;
                }
                ESP_LOGI(HID_LE_PRF_TAG, "%s(), start added the hid service to the stack database. incl_handle = %d",
                           __func__, incl_svc.start_hdl);
                err = esp_ble_gatts_create_attr_tab(hidd_le_gatt_db, gatts_if, HIDD_LE_IDX_NB, 0);
                if (err != ESP_OK) {
                    ESP_LOGE(HID_LE_PRF_TAG, "HID attribute table request failed: %s", esp_err_to_name(err));
                }
            } else {
                memcpy(hidd_le_env.hidd_inst.att_tbl, param->add_attr_tab.handles,
                            HIDD_LE_IDX_NB*sizeof(uint16_t));
                ESP_LOGI(HID_LE_PRF_TAG, "hid svc handle = %x",hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_SVC]);
                hid_add_id_tbl();
                esp_err_t err = esp_ble_gatts_start_service(hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_SVC]);
                if (err != ESP_OK) {
                    ESP_LOGE(HID_LE_PRF_TAG, "HID service start failed: %s", esp_err_to_name(err));
                }
            }
            break;
         }

        default:
            break;
    }
}

void hidd_le_create_service(esp_gatt_if_t gatts_if) {
    esp_err_t err = esp_ble_gatts_create_attr_tab(bas_att_db, gatts_if, BAS_IDX_NB, 0);
    if (err != ESP_OK) {
        ESP_LOGE(HID_LE_PRF_TAG, "Battery attribute table request failed: %s", esp_err_to_name(err));
    }
}

void hidd_le_init(void) {

    memset(&hidd_le_env, 0, sizeof(hidd_le_env_t));
    memset(&hid_peer, 0, sizeof(hid_peer));
}

void hidd_clcb_alloc (uint16_t conn_id, esp_bd_addr_t bda) {
    uint8_t                   i_clcb = 0;
    hidd_clcb_t      *p_clcb = NULL;

    for (i_clcb = 0, p_clcb= hidd_le_env.hidd_clcb; i_clcb < HID_MAX_APPS; i_clcb++, p_clcb++) {
        if (p_clcb->in_use && p_clcb->conn_id == conn_id) return;
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
        if (p_clcb->in_use && p_clcb->conn_id == conn_id) {
            memset(p_clcb, 0, sizeof(hidd_clcb_t));
            return true;
        }
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

    hid_rpt_map[0].id = HID_RPT_ID_MOUSE_IN;
    hid_rpt_map[0].type = HID_REPORT_TYPE_INPUT;
    hid_rpt_map[0].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_MOUSE_IN_VAL];
    hid_rpt_map[0].cccdHandle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_MOUSE_IN_CCC];
    hid_rpt_map[0].mode = HID_PROTOCOL_MODE_REPORT;

    hid_rpt_map[1].id = 0x02;
    hid_rpt_map[1].type = HID_REPORT_TYPE_FEATURE;
    hid_rpt_map[1].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_VAL];
    hid_rpt_map[1].cccdHandle = 0;
    hid_rpt_map[1].mode = HID_PROTOCOL_MODE_REPORT;

    hid_rpt_map[2] = (hid_report_map_t){
        .id = REPORTID_HAPTIC_INTENSITY, .type = HID_REPORT_TYPE_FEATURE,
        .handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_HAPTIC_INTENSITY_VAL],
        .mode = HID_PROTOCOL_MODE_REPORT,
    };
    hid_dev_register_reports(3, hid_rpt_map);
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

        if (ble_hid_is_connected && hid_peer.secured && (hid_peer.ccc & CCC_BATTERY)) {

            update_battery_level(hidd_le_env.gatt_if, ble_conn_id, current_battery_level);
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
