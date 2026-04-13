#include <string.h>
#include <stdbool.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_hid_gap.h"

#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "host/ble_gap.h"
#include "host/ble_hs_adv.h"
#include "nimble/ble.h"
#include "host/ble_sm.h"

static const char *TAG = "ESP_HID_GAP";

#define GAP_DBG_PRINTF(...) //printf(__VA_ARGS__)

static SemaphoreHandle_t bt_hidh_cb_semaphore = NULL;
#define WAIT_BT_CB() xSemaphoreTake(bt_hidh_cb_semaphore, portMAX_DELAY)
#define SEND_BT_CB() xSemaphoreGive(bt_hidh_cb_semaphore)

static SemaphoreHandle_t ble_hidh_cb_semaphore = NULL;
#define WAIT_BLE_CB() xSemaphoreTake(ble_hidh_cb_semaphore, portMAX_DELAY)
#define SEND_BLE_CB() xSemaphoreGive(ble_hidh_cb_semaphore)

#define SIZEOF_ARRAY(a) (sizeof(a)/sizeof(*a))

#define GATT_SVR_SVC_HID_UUID 0x1812

extern void ble_hid_task_start_up(void);
static struct ble_hs_adv_fields fields;

esp_err_t esp_hid_ble_gap_adv_init(uint16_t appearance, const char *device_name)
{
    ble_uuid16_t *uuid16, *uuid16_1;

    memset(&fields, 0, sizeof fields);

    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;

    fields.appearance = 0x03C9;
    fields.appearance_is_present = 1;

    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;

    uuid16 = (ble_uuid16_t *)malloc(sizeof(ble_uuid16_t));
    uuid16_1 = (ble_uuid16_t[]) {
        BLE_UUID16_INIT(GATT_SVR_SVC_HID_UUID)
    };
    memcpy(uuid16, uuid16_1, sizeof(ble_uuid16_t));
    fields.uuids16 = uuid16;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;

    return ESP_OK;

}

static int
nimble_hid_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        ESP_LOGI(TAG, "connection %s; status=%d",
                event->connect.status == 0 ? "established" : "failed",
                event->connect.status);
        return 0;
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect; reason=%d", event->disconnect.reason);

        return 0;
    case BLE_GAP_EVENT_CONN_UPDATE:
        /* The central has updated the connection parameters. */
        ESP_LOGI(TAG, "connection updated; status=%d",
                event->conn_update.status);
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "advertise complete; reason=%d",
                event->adv_complete.reason);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe event; conn_handle=%d attr_handle=%d "
                "reason=%d prevn=%d curn=%d previ=%d curi=%d\n",
                event->subscribe.conn_handle,
                event->subscribe.attr_handle,
                event->subscribe.reason,
                event->subscribe.prev_notify,
                event->subscribe.cur_notify,
                event->subscribe.prev_indicate,
                event->subscribe.cur_indicate);
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu update event; conn_handle=%d cid=%d mtu=%d",
                event->mtu.conn_handle,
                event->mtu.channel_id,
                event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        /* Encryption has been enabled or disabled for this connection. */
        MODLOG_DFLT(INFO, "encryption change event; status=%d ",
                event->enc_change.status);
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        assert(rc == 0);
        ble_hid_task_start_up();
        return 0;

    case BLE_GAP_EVENT_NOTIFY_TX:
        MODLOG_DFLT(INFO, "notify_tx event; conn_handle=%d attr_handle=%d "
                "status=%d is_indication=%d",
                event->notify_tx.conn_handle,
                event->notify_tx.attr_handle,
                event->notify_tx.status,
                event->notify_tx.indication);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        assert(rc == 0);
        ble_store_util_delete_peer(&desc.peer_id_addr);

        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ESP_LOGI(TAG, "PASSKEY_ACTION_EVENT started");
        struct ble_sm_io pkey = {0};
        int key = 0;

        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            pkey.action = event->passkey.params.action;
            pkey.passkey = 123456; // This is the passkey to be entered on peer
            ESP_LOGI(TAG, "Enter passkey %" PRIu32 "on the peer side", pkey.passkey);
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG, "ble_sm_inject_io result: %d", rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            ESP_LOGI(TAG, "Accepting passkey..");
            pkey.action = event->passkey.params.action;
            pkey.numcmp_accept = key;
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG, "ble_sm_inject_io result: %d", rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_OOB) {
            static uint8_t tem_oob[16] = {0};
            pkey.action = event->passkey.params.action;
            for (int i = 0; i < 16; i++) {
                pkey.oob[i] = tem_oob[i];
            }
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG, "ble_sm_inject_io result: %d", rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
            ESP_LOGI(TAG, "Input not supported passing -> 123456");
            pkey.action = event->passkey.params.action;
            pkey.passkey = 123456;
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG, "ble_sm_inject_io result: %d", rc);
        }
        return 0;
    }
    return 0;
}
esp_err_t esp_hid_ble_gap_adv_start(void)
{
    int rc;
    struct ble_gap_adv_params adv_params;
    int32_t adv_duration_ms = 180000;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error setting advertisement data; rc=%d\n", rc);
        return rc;
    }
    /* Begin advertising. */
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(30);/* Recommended interval 30ms to 50ms */
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(50);
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, adv_duration_ms,
                           &adv_params, nimble_hid_gap_event, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error enabling advertisement; rc=%d\n", rc);
        return rc;
    }
    return rc;
}

static esp_err_t init_low_level(uint8_t mode)
{
    esp_err_t ret;
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_mem_release failed: %d", ret);
        return ret;
    }
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_init failed: %d", ret);
        return ret;
    }

    ret = esp_bt_controller_enable(mode);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_enable failed: %d", ret);
        return ret;
    }

    ret = esp_nimble_init();
    if (ret) {
        ESP_LOGE(TAG, "esp_nimble_init failed: %d", ret);
        return ret;
    }


    return ret;
}

static esp_err_t deinit_low_level(void)
{
    esp_err_t ret;

    ret = esp_nimble_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_nimble_deinit failed: %d", ret);
    }

    ret = esp_bt_controller_disable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_disable failed: %d", ret);
    }

    ret = esp_bt_controller_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_deinit failed: %d", ret);
    }

    return ret;
}

esp_err_t esp_hid_gap_deinit(void)
{
    esp_err_t ret;

    ret = deinit_low_level();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "deinit_low_level failed: %d", ret);
    }

    if (bt_hidh_cb_semaphore != NULL) {
        vSemaphoreDelete(bt_hidh_cb_semaphore);
        bt_hidh_cb_semaphore = NULL;
    }

    if (ble_hidh_cb_semaphore != NULL) {
        vSemaphoreDelete(ble_hidh_cb_semaphore);
        ble_hidh_cb_semaphore = NULL;
    }

    return ESP_OK;
}

esp_err_t esp_hid_gap_init(uint8_t mode)
{
    esp_err_t ret;
    if (!mode || mode > ESP_BT_MODE_BTDM) {
        ESP_LOGE(TAG, "Invalid mode given!");
        return ESP_FAIL;
    }

    if (bt_hidh_cb_semaphore != NULL) {
        ESP_LOGE(TAG, "Already initialised");
        return ESP_FAIL;
    }

    bt_hidh_cb_semaphore = xSemaphoreCreateBinary();
    if (bt_hidh_cb_semaphore == NULL) {
        ESP_LOGE(TAG, "xSemaphoreCreateMutex failed!");
        return ESP_FAIL;
    }

    ble_hidh_cb_semaphore = xSemaphoreCreateBinary();
    if (ble_hidh_cb_semaphore == NULL) {
        ESP_LOGE(TAG, "xSemaphoreCreateMutex failed!");
        vSemaphoreDelete(bt_hidh_cb_semaphore);
        bt_hidh_cb_semaphore = NULL;
        return ESP_FAIL;
    }

    ret = init_low_level(mode);
    if (ret != ESP_OK) {
        vSemaphoreDelete(bt_hidh_cb_semaphore);
        bt_hidh_cb_semaphore = NULL;
        vSemaphoreDelete(ble_hidh_cb_semaphore);
        ble_hidh_cb_semaphore = NULL;
        return ret;
    }

    return ESP_OK;
}