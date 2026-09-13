typedef int hid_report_type_t;
#define HID_REPORT_TYPE_FEATURE 3
#define HID_REPORT_TYPE_INPUT 1
#define HID_RPT_ID_PTP_IN 1
#define HID_RPT_ID_MOUSE_IN 1
static uint8_t ptp_input_mode;
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
    (void)id; (void)len; return record_send(instance == 1 ? PTP_MODE : MOUSE_MODE, data);
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
