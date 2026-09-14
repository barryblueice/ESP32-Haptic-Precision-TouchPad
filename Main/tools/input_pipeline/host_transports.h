typedef int hid_report_type_t;
#define HID_REPORT_TYPE_FEATURE 3
#define HID_REPORT_TYPE_INPUT 1
#define HID_REPORT_TYPE_OUTPUT 2
#define HID_RPT_ID_PTP_IN 1
#define HID_RPT_ID_MOUSE_IN 1
static uint8_t ptp_input_mode;
static void usb_config_complete(bool success) { (void)success; }
static void usb_config_detach(void) { }
static void usb_config_send(void) { }
static bool usb_config_active(void) { return false; }
static bool device_config_ready(void) { return false; }
static uint8_t device_config_value(unsigned offset) { (void)offset; return 0; }
static unsigned config_receives, legacy_receives, dfu_requests;
static uint8_t legacy_id, legacy_setting;
static void usb_config_receive(const uint8_t *bytes, uint16_t size) { if (bytes && size >= 12) ++config_receives; }
static void usb_config_legacy(uint8_t id, uint8_t value) { ++legacy_receives; legacy_id=id; legacy_setting=value; }
static void usb_config_dfu(void) { ++dfu_requests; }
typedef struct { int id; } tinyusb_event_t;
enum { TINYUSB_EVENT_ATTACHED, TINYUSB_EVENT_DETACHED, TINYUSB_EVENT_SUSPENDED, TINYUSB_EVENT_RESUMED };
static struct { int gatt_if; } hidd_le_env;
typedef int esp_now_send_status_t;
typedef int esp_now_send_info_t;
typedef int esp_now_recv_info_t;
#define ESP_NOW_SEND_SUCCESS 0
#define ESP_NOW_SEND_FAIL 1
static uint8_t receiver_mac[6];
static uint8_t battery_percentage = 75;
static bool usb_ready = true, mounted = true;
static int submit_errors, attempts, sent_count;
static input_report_t sent[128];
static uint8_t sent_ids[128], sent_instances[128];
static wireless_msg_t wifi_packets[128];
static bool tud_mounted(void) { return mounted; }
static bool tud_suspended(void) { return false; }
static bool tud_hid_n_ready(uint8_t instance) { (void)instance; return usb_ready; }
static bool record_send(int mode, const void *data) {
    ++attempts;
    if (submit_errors > 0) { --submit_errors; return false; }
    input_report_t *r = &sent[sent_count++];
    r->mode = mode;
    if (mode == PTP_MODE) r->data.ptp = *(const ptp_report_t *)data;
    else r->data.mouse = *(const mouse_hid_report_t *)data;
    return true;
}
static bool tud_hid_n_report(uint8_t instance, uint8_t id, const void *data, unsigned len) {
    (void)len; unsigned index=sent_count;
    bool ok=record_send(instance == 1 ? PTP_MODE : MOUSE_MODE, data);
    if(ok) { sent_ids[index]=id; sent_instances[index]=instance; }
    return ok;
}
static esp_err_t hid_dev_send_report(int gatts, uint16_t conn, uint8_t id, uint8_t type, uint8_t len, uint8_t *data) {
    (void)gatts; (void)conn; (void)id; (void)type;
    return record_send(len == sizeof(ptp_report_t) ? PTP_MODE : MOUSE_MODE, data) ? ESP_OK : ESP_FAIL;
}
static esp_err_t esp_now_send(const uint8_t *mac, const uint8_t *data, unsigned len) {
    (void)mac; (void)len; ++attempts;
    if (submit_errors > 0) { --submit_errors; return ESP_ERR_NO_MEM; }
    wifi_packets[sent_count++] = *(const wireless_msg_t *)data;
    return ESP_OK;
}
