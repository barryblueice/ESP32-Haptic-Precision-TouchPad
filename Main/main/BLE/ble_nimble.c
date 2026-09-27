#include "BLE/ble_hid.h"
#include "SYS/input_pipeline.h"
#include "GPIO/gpio_handle.h"
#include "I2C/SUB_DEV/sub_dev.h"
#include "I2C/TP/i2c_hid.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
#include <string.h>

#define TAG "BLE_NIMBLE"
#define DEVICE_NAME "R-SODIUM BLE"

/* The SDK's store/config header exposes read/write but not its initializer. */
void ble_store_config_init(void);

/* GAP state and GATT values are owned by the NimBLE host task. Only the two
 * mailboxes below cross task boundaries. Never call the host under a spinlock. */
static struct {
    uint16_t conn;
    uint32_t epoch;
    bool connected, encrypted, mouse_notify, battery_notify, consumer_notify, keyboard_notify;
} peer;
static bool host_synced, advertising_configured;
static uint16_t mouse_handle, boot_handle, battery_handle, consumer_handle, keyboard_handle;
static uint8_t protocol_mode = 1, battery_level = 100;
static uint8_t last_mouse[5], last_boot[3], last_consumer[2], last_keyboard[8];
static struct ble_npl_callout adv_retry;
static struct ble_npl_event mouse_event, battery_event;
static portMUX_TYPE mailbox_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    bool busy, auxiliary;
    uint16_t conn;
    uint32_t epoch;
    input_report_t report;
    aux_output_report_t aux;
} mouse_mailbox;
static uint8_t pending_battery = 100;

enum {
    VALUE_INFO, VALUE_MAP, VALUE_CONTROL, VALUE_PROTOCOL, VALUE_MOUSE,
    VALUE_BOOT, VALUE_STRENGTH, VALUE_GENERIC, VALUE_BATTERY,
    REF_MOUSE, REF_STRENGTH, REF_GENERIC, REF_BATTERY, FORMAT_BATTERY,
    VALUE_CONSUMER, VALUE_KEYBOARD, REF_CONSUMER, REF_KEYBOARD
};

static int append_value(struct ble_gatt_access_ctxt *ctxt, const void *data, uint16_t length)
{
    if (ctxt->offset > length) return BLE_ATT_ERR_INVALID_OFFSET;
    /* NimBLE slices the full value at ctxt->offset for Read Blob requests. */
    return os_mbuf_append(ctxt->om, data, length) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int gatt_access(uint16_t conn, uint16_t handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)handle;
    unsigned value = (uintptr_t)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR || ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
        static const uint8_t info[] = {0x11, 0x01, 0, 1};
        static const uint8_t mouse_ref[] = {HID_RPT_ID_MOUSE_IN, 1};
        static const uint8_t consumer_ref[] = {HID_RPT_ID_CONSUMER_IN, 1};
        static const uint8_t keyboard_ref[] = {HID_RPT_ID_KEYBOARD_IN, 1};
        static const uint8_t strength_ref[] = {REPORTID_HAPTIC_INTENSITY, 3};
        static const uint8_t generic_ref[] = {2, 3}, generic[] = {2, 5, 1};
        static const uint8_t external_ref[] = {0x19, 0x2a};
        static const uint8_t battery_format[] = {4, 0, 0xad, 0x27, 1, 0, 0};
        uint8_t strength = ptp_haptic_click_intensity_get();
        switch (value) {
        case VALUE_INFO: return append_value(ctxt, info, sizeof(info));
        case VALUE_MAP: return append_value(ctxt, ble_mouse_hid_report_descriptor, ble_mouse_hid_report_len);
        case VALUE_PROTOCOL: return append_value(ctxt, &protocol_mode, 1);
        case VALUE_MOUSE: return append_value(ctxt, last_mouse, sizeof(last_mouse));
        case VALUE_CONSUMER: return append_value(ctxt, last_consumer, sizeof(last_consumer));
        case VALUE_KEYBOARD: return append_value(ctxt, last_keyboard, sizeof(last_keyboard));
        case VALUE_BOOT: return append_value(ctxt, last_boot, sizeof(last_boot));
        case VALUE_STRENGTH: return append_value(ctxt, &strength, 1);
        case VALUE_GENERIC: return append_value(ctxt, generic, sizeof(generic));
        case VALUE_BATTERY: return append_value(ctxt, &battery_level, 1);
        case REF_MOUSE: return append_value(ctxt, mouse_ref, sizeof(mouse_ref));
        case REF_CONSUMER: return append_value(ctxt, consumer_ref, sizeof(consumer_ref));
        case REF_KEYBOARD: return append_value(ctxt, keyboard_ref, sizeof(keyboard_ref));
        case REF_STRENGTH: return append_value(ctxt, strength_ref, sizeof(strength_ref));
        case REF_GENERIC: return append_value(ctxt, generic_ref, sizeof(generic_ref));
        case REF_BATTERY: return append_value(ctxt, external_ref, sizeof(external_ref));
        case FORMAT_BATTERY: return append_value(ctxt, battery_format, sizeof(battery_format));
        default: return BLE_ATT_ERR_READ_NOT_PERMITTED;
        }
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR ||
        (value != VALUE_STRENGTH && value != VALUE_PROTOCOL && value != VALUE_CONTROL))
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    if (ctxt->offset) return BLE_ATT_ERR_INVALID_OFFSET;
    if (OS_MBUF_PKTLEN(ctxt->om) != 1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    uint8_t byte;
    if (os_mbuf_copydata(ctxt->om, 0, 1, &byte)) return BLE_ATT_ERR_UNLIKELY;
    if (byte > (value == VALUE_STRENGTH ? 100 : 1)) return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    if (value == VALUE_STRENGTH)
        return ptp_haptic_click_intensity_set_report(&byte, 1, true) == ESP_OK ? 0 : BLE_ATT_ERR_UNLIKELY;
    if (value == VALUE_PROTOCOL) protocol_mode = byte;
    return 0;
}

#define READ_DSC(uuid16, value) { .uuid = BLE_UUID16_DECLARE(uuid16), .att_flags = BLE_ATT_F_READ, \
    .access_cb = gatt_access, .arg = (void *)(uintptr_t)(value) }
#define HID_CHR(uuid16, value, properties) .uuid = BLE_UUID16_DECLARE(uuid16), \
    .access_cb = gatt_access, .arg = (void *)(uintptr_t)(value), .flags = (properties)

static struct ble_gatt_dsc_def mouse_descriptors[] = {READ_DSC(0x2908, REF_MOUSE), {0}};
static struct ble_gatt_dsc_def consumer_descriptors[] = {READ_DSC(0x2908, REF_CONSUMER), {0}};
static struct ble_gatt_dsc_def keyboard_descriptors[] = {READ_DSC(0x2908, REF_KEYBOARD), {0}};
static struct ble_gatt_dsc_def strength_descriptors[] = {READ_DSC(0x2908, REF_STRENGTH), {0}};
static struct ble_gatt_dsc_def generic_descriptors[] = {READ_DSC(0x2908, REF_GENERIC), {0}};
static struct ble_gatt_dsc_def map_descriptors[] = {READ_DSC(0x2907, REF_BATTERY), {0}};
static struct ble_gatt_dsc_def battery_descriptors[] = {READ_DSC(0x2904, FORMAT_BATTERY), {0}};
static const struct ble_gatt_chr_def battery_chars[] = {
    {HID_CHR(0x2a19, VALUE_BATTERY, BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY),
        .val_handle = &battery_handle, .descriptors = battery_descriptors},
    {0}
};
static const struct ble_gatt_chr_def hid_chars[] = {
    {HID_CHR(0x2a4a, VALUE_INFO, BLE_GATT_CHR_F_READ)},
    {HID_CHR(0x2a4c, VALUE_CONTROL, BLE_GATT_CHR_F_WRITE_NO_RSP)},
    {HID_CHR(0x2a4b, VALUE_MAP, BLE_GATT_CHR_F_READ), .descriptors = map_descriptors},
    {HID_CHR(0x2a4e, VALUE_PROTOCOL, BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP)},
    {HID_CHR(0x2a4d, VALUE_MOUSE, BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY),
        .val_handle = &mouse_handle, .descriptors = mouse_descriptors},
    {HID_CHR(0x2a33, VALUE_BOOT, BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY), .val_handle = &boot_handle},
    {HID_CHR(0x2a4d, VALUE_STRENGTH, BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE), .descriptors = strength_descriptors},
    {HID_CHR(0x2a4d, VALUE_GENERIC, BLE_GATT_CHR_F_READ), .descriptors = generic_descriptors},
    {HID_CHR(0x2a4d, VALUE_CONSUMER, BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY),
        .val_handle = &consumer_handle, .descriptors = consumer_descriptors},
    {HID_CHR(0x2a4d, VALUE_KEYBOARD, BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY),
        .val_handle = &keyboard_handle, .descriptors = keyboard_descriptors},
    {0}
};
static const struct ble_gatt_svc_def services[];
static const struct ble_gatt_svc_def *hid_includes[] = {&services[0], NULL};
static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x180f), .characteristics = battery_chars},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x1812),
        .includes = hid_includes, .characteristics = hid_chars},
    {0}
};

static int register_services(void)
{
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(services);
    if (!rc) rc = ble_gatts_add_svcs(services);
    return rc;
}

static void update_subscription(void)
{
    if (peer.connected) {
        ble_input_subscription(peer.conn, peer.encrypted && peer.mouse_notify);
        uint8_t mask = peer.encrypted ?
            (peer.mouse_notify ? AUX_OUTPUT_MOUSE : 0) |
            (peer.consumer_notify ? AUX_OUTPUT_CONSUMER : 0) |
            (peer.keyboard_notify ? AUX_OUTPUT_KEYBOARD : 0) : 0;
        ble_input_aux_subscription(peer.conn, mask);
    }
}

uint16_t hid_dev_report_handle(uint8_t id)
{
    switch (id) {
    case HID_RPT_ID_MOUSE_IN: return mouse_handle;
    case HID_RPT_ID_CONSUMER_IN: return consumer_handle;
    case HID_RPT_ID_KEYBOARD_IN: return keyboard_handle;
    default: return 0;
    }
}

esp_err_t ble_hid_send_mouse(uint16_t conn, uint32_t epoch, const input_report_t *report)
{
    if (!report || report->mode != MOUSE_MODE) return ESP_ERR_INVALID_ARG;
    taskENTER_CRITICAL(&mailbox_lock);
    if (mouse_mailbox.busy) {
        taskEXIT_CRITICAL(&mailbox_lock);
        return ESP_ERR_NO_MEM;
    }
    mouse_mailbox.busy = true;
    mouse_mailbox.auxiliary = false;
    mouse_mailbox.conn = conn;
    mouse_mailbox.epoch = epoch;
    mouse_mailbox.report = *report;
    taskEXIT_CRITICAL(&mailbox_lock);
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &mouse_event);
    return ESP_OK;
}

esp_err_t ble_hid_send_aux(uint16_t conn, uint32_t epoch, const aux_output_report_t *report)
{
    if (!report || !((report->id == 2 && report->length == 5) ||
        (report->id == HID_RPT_ID_CONSUMER_IN && report->length == 2) ||
        (report->id == HID_RPT_ID_KEYBOARD_IN && report->length == 8))) return ESP_ERR_INVALID_ARG;
    taskENTER_CRITICAL(&mailbox_lock);
    if (mouse_mailbox.busy) {
        taskEXIT_CRITICAL(&mailbox_lock);
        return ESP_ERR_NO_MEM;
    }
    mouse_mailbox.busy = mouse_mailbox.auxiliary = true;
    mouse_mailbox.conn = conn;
    mouse_mailbox.epoch = epoch;
    mouse_mailbox.aux = *report;
    taskEXIT_CRITICAL(&mailbox_lock);
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &mouse_event);
    return ESP_OK;
}

static void send_mouse_event(struct ble_npl_event *event)
{
    (void)event;
    taskENTER_CRITICAL(&mailbox_lock);
    uint16_t conn = mouse_mailbox.conn;
    uint32_t epoch = mouse_mailbox.epoch;
    input_report_t report = mouse_mailbox.report;
    aux_output_report_t aux = mouse_mailbox.aux;
    bool auxiliary = mouse_mailbox.auxiliary;
    mouse_mailbox.busy = false;
    taskEXIT_CRITICAL(&mailbox_lock);
    if (!peer.connected || peer.conn != conn || peer.epoch != epoch) return;
    uint8_t id = auxiliary && aux.id != 2 ? aux.id : HID_RPT_ID_MOUSE_IN;
    uint16_t handle = hid_dev_report_handle(id);
    bool subscribed = id == HID_RPT_ID_MOUSE_IN ? peer.mouse_notify :
        id == HID_RPT_ID_CONSUMER_IN ? peer.consumer_notify : peer.keyboard_notify;
    bool current = auxiliary ? aux_output_report_current(&aux) : input_report_current(&report);
    if (!peer.encrypted || !subscribed || !current) {
        ble_input_complete(conn, epoch, handle, BLE_TX_RETRY);
        return;
    }
    /* Wheel/pan reports must preserve the buttons last delivered to this peer. */
    if (auxiliary && id == HID_RPT_ID_MOUSE_IN) aux.data[0] = last_mouse[0];
    const void *data = auxiliary ? (const void *)aux.data : (const void *)&report.data.mouse;
    uint8_t length = auxiliary ? aux.length : sizeof(report.data.mouse);
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, length);
    int rc = om ? ble_gatts_notify_custom(conn, handle, om) : BLE_HS_ENOMEM;
    /* notify_custom owns om on every return path. In this SDK NOTIFY_TX is
     * synchronous, including failures; settle exactly once using the return
     * code, not both the event and the return. This is not a host-side ACK. */
    ble_tx_result_t status = rc == 0 ? BLE_TX_OK :
        (rc == BLE_HS_ENOMEM || rc == BLE_HS_EBUSY || rc == BLE_HS_EAGAIN) ? BLE_TX_RETRY : BLE_TX_FAILED;
    if (!rc) {
        uint8_t *last = id == HID_RPT_ID_MOUSE_IN ? last_mouse :
            id == HID_RPT_ID_CONSUMER_IN ? last_consumer : last_keyboard;
        memcpy(last, data, length);
    }
    ble_input_complete(conn, epoch, handle, status);
}

static void send_battery_event(struct ble_npl_event *event)
{
    (void)event;
    taskENTER_CRITICAL(&mailbox_lock);
    battery_level = pending_battery;
    taskEXIT_CRITICAL(&mailbox_lock);
    if (!peer.connected || !peer.encrypted || !peer.battery_notify) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&battery_level, 1);
    if (om) (void)ble_gatts_notify_custom(peer.conn, battery_handle, om);
}

static void battery_task(void *arg)
{
    (void)arg;
    while (true) {
        int level = get_battery_percentage();
        if (level >= 0 && level <= 100) {
            taskENTER_CRITICAL(&mailbox_lock);
            pending_battery = level;
            taskEXIT_CRITICAL(&mailbox_lock);
            ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &battery_event);
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

static void disconnected(void)
{
    if (peer.connected) (void)ble_input_connection(false, peer.conn);
    memset(&peer, 0, sizeof(peer));
    protocol_mode = 1;
    memset(last_mouse, 0, sizeof(last_mouse));
    memset(last_consumer, 0, sizeof(last_consumer));
    memset(last_keyboard, 0, sizeof(last_keyboard));
    led_send_command(GPIO_LED_3, LED_CMD_BLINK, 100, 1000, 2, true);
}

static int gap_event(struct ble_gap_event *event, void *arg);
static int configure_advertising(void);
static void advertise(void)
{
    if (!host_synced || peer.connected || ble_gap_adv_active()) return;
    int rc = advertising_configured ? 0 : configure_advertising();
    advertising_configured = rc == 0;
    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = 32, .itvl_max = 48,
    };
    if (!rc) rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc) {
        ESP_LOGW(TAG, "Advertising failed: %d; retrying", rc);
        ble_npl_callout_reset(&adv_retry, ble_npl_time_ms_to_ticks32(250));
    }
}

static void retry_advertising(struct ble_npl_event *event)
{
    (void)event;
    advertise();
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    struct ble_gap_conn_desc desc;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status) { advertise(); break; }
        ble_npl_callout_stop(&adv_retry);
        peer.connected = true;
        peer.conn = event->connect.conn_handle;
        peer.encrypted = peer.mouse_notify = peer.battery_notify = false;
        peer.consumer_notify = peer.keyboard_notify = false;
        peer.epoch = ble_input_connection(true, peer.conn);
        led_send_command(GPIO_LED_3, LED_CMD_STOP, 100, 1000, 0, false);
        led_send_command(GPIO_LED_3, LED_CMD_BLINK, 500, 2000, 3, false);
        ESP_LOGI(TAG, "Connected: handle=%u", peer.conn);
        int rc = ble_gap_security_initiate(peer.conn);
        if (rc && rc != BLE_HS_EALREADY) {
            ESP_LOGW(TAG, "Security initiation failed: %d", rc);
            (void)ble_gap_terminate(peer.conn, BLE_ERR_REM_USER_CONN_TERM);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        if (peer.connected && peer.conn == event->disconnect.conn.conn_handle) {
            disconnected();
            advertise();
        }
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (!peer.connected || peer.conn != event->enc_change.conn_handle) break;
        peer.encrypted = !event->enc_change.status && !ble_gap_conn_find(peer.conn, &desc) && desc.sec_state.encrypted;
        update_subscription();
        ESP_LOGI(TAG, "Encryption: handle=%u enabled=%u status=%d", peer.conn, peer.encrypted, event->enc_change.status);
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (!peer.connected || peer.conn != event->subscribe.conn_handle) break;
        if (event->subscribe.attr_handle == mouse_handle) peer.mouse_notify = event->subscribe.cur_notify;
        if (event->subscribe.attr_handle == consumer_handle) peer.consumer_notify = event->subscribe.cur_notify;
        if (event->subscribe.attr_handle == keyboard_handle) peer.keyboard_notify = event->subscribe.cur_notify;
        if (event->subscribe.attr_handle == battery_handle) peer.battery_notify = event->subscribe.cur_notify;
        update_subscription();
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        if (!peer.connected || peer.conn != event->repeat_pairing.conn_handle || ble_gap_conn_find(peer.conn, &desc))
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        if (ble_store_util_delete_peer(&desc.peer_id_addr)) return BLE_GAP_REPEAT_PAIRING_IGNORE;
        peer.encrypted = false;
        update_subscription();
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    case BLE_GAP_EVENT_NOTIFY_TX:
        /* Mouse completion is settled by send_mouse_event; battery and service
         * changed notifications must never complete a pending mouse report. */
        break;
    default:
        break;
    }
    return 0;
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "Host reset: %d", reason);
    host_synced = advertising_configured = false;
    ble_npl_callout_stop(&adv_retry);
    disconnected();
}

static int configure_advertising(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    static const ble_uuid16_t hid_uuid = BLE_UUID16_INIT(0x1812);
    struct ble_hs_adv_fields fields = {
        .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .uuids16 = &hid_uuid, .num_uuids16 = 1, .uuids16_is_complete = 1,
        .appearance = 0x03c9, .appearance_is_present = 1,
        .tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO, .tx_pwr_lvl_is_present = 1,
    };
    struct ble_hs_adv_fields response = {
        .name = (const uint8_t *)DEVICE_NAME, .name_len = sizeof(DEVICE_NAME) - 1, .name_is_complete = 1,
    };
    if (!rc) rc = ble_gap_adv_set_fields(&fields);
    if (!rc) rc = ble_gap_adv_rsp_set_fields(&response);
    return rc;
}

static void on_sync(void)
{
    advertising_configured = false;
    host_synced = true;
    advertise();
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_hid_init(void)
{
    input_request_mode(MOUSE_MODE);
    disconnected();
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) return err;
    ble_npl_event_init(&mouse_event, send_mouse_event, NULL);
    ble_npl_event_init(&battery_event, send_battery_event, NULL);
    ble_npl_callout_init(&adv_retry, nimble_port_get_dflt_eventq(), retry_advertising, NULL);
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();
    int rc = register_services();
    if (!rc) rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (!rc) rc = ble_svc_gap_device_appearance_set(0x03c9);
    if (!rc) rc = ble_att_set_preferred_mtu(64);
    if (rc) {
        ESP_LOGE(TAG, "HID initialization failed: %d", rc);
        (void)nimble_port_deinit();
        return ESP_FAIL;
    }
    nimble_port_freertos_init(host_task);
    if (xTaskCreatePinnedToCore(battery_task, "ble_battery", 2048, NULL, 5, NULL, 0) != pdPASS)
        ESP_LOGW(TAG, "Battery notifications disabled: task allocation failed");
    return ESP_OK;
}
