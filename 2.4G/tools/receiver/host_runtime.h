/* Deterministic SDK/RTOS boundaries. Production algorithms are compiled unchanged. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <inttypes.h>
#define EXPORT __declspec(dllexport)
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
void *memset(void *dst, int value, size_t n) {
    volatile unsigned char *d = dst; while (n--) *d++ = (unsigned char)value; return dst;
}
void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst; const unsigned char *s = src;
    while (n--) *d++ = *s++; return dst;
}
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    while (n--) { if (*x != *y) return *x - *y; ++x; ++y; } return 0;
}
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_NO_FREE_PAGES 0x110d
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1110
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
static int init_error, sdk_calls, fail_at, test_steps, lock_error, mutex_depth;
/* Real ESP_ERROR_CHECK aborts. Keep subsequent nested startup calls failed too. */
static int sdk(void) {
    if (init_error) return ESP_FAIL;
    return ++sdk_calls == fail_at ? ESP_FAIL : ESP_OK;
}
#define ESP_ERROR_CHECK(e) do { int err_ = (e); if (err_) { init_error = err_; return; } } while (0)
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
static uint32_t fake_now, notifications;
static int64_t esp_timer_get_time(void) { return (int64_t)fake_now * 1000; }
static TickType_t xTaskGetTickCount(void) { return fake_now; }
static TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (void *)1; }
static void xTaskNotifyGive(TaskHandle_t task) { (void)task; ++notifications; }
static void ulTaskNotifyTake(int clear, uint32_t ticks) { (void)clear; fake_now += ticks; }
static void vTaskDelay(uint32_t ticks) { fake_now += ticks; }
static int xTaskCreate(void (*fn)(void *), const char *name, int stack, void *arg, int priority, TaskHandle_t *out) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority; (void)out;
    return sdk() == ESP_OK ? pdPASS : 0;
}
static SemaphoreHandle_t xSemaphoreCreateMutex(void) { return sdk() == ESP_OK ? (void *)1 : NULL; }
static void xSemaphoreTake(SemaphoreHandle_t m, uint32_t timeout) {
    (void)timeout; if (!m || mutex_depth++) ++lock_error;
}
static void xSemaphoreGive(SemaphoreHandle_t m) { (void)m; if (--mutex_depth) ++lock_error; }
static unsigned qhead, qcount, qcapacity, qsize;
static unsigned char qbytes[16][128];
static QueueHandle_t xQueueCreate(unsigned capacity, unsigned size) {
    if (sdk() != ESP_OK || capacity > 16 || size > 128) return NULL;
    qhead = qcount = 0; qcapacity = capacity; qsize = size; return (void *)1;
}
static int xQueueSend(QueueHandle_t q, const void *data, int timeout) {
    (void)timeout; if (!q || qcount == qcapacity) return 0;
    memcpy(qbytes[(qhead + qcount++) % qcapacity], data, qsize); return pdPASS;
}
static int xQueueReceive(QueueHandle_t q, void *data, int timeout) {
    (void)timeout; if (!q || !qcount) return 0;
    memcpy(data, qbytes[qhead], qsize); qhead = (qhead + 1) % qcapacity; --qcount; return pdPASS;
}
typedef struct { int intr_type, mode; uint64_t pin_bit_mask; int pull_up_en, pull_down_en; } gpio_config_t;
#define GPIO_NUM_9 9
#define GPIO_INTR_DISABLE 0
#define GPIO_MODE_OUTPUT_OD 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
static int gpio_level, gpio_writes;
static int gpio_config(const gpio_config_t *cfg) { (void)cfg; return sdk(); }
static int gpio_set_level(int pin, int level) { (void)pin; gpio_level = !!level; ++gpio_writes; return sdk(); }
typedef struct { int unused; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define WIFI_MODE_STA 1
#define WIFI_IF_STA 0
#define WIFI_SECOND_CHAN_NONE 0
#define WIFI_PS_NONE 0
static int esp_netif_init(void) { return sdk(); }
static int esp_event_loop_create_default(void) { return sdk(); }
static int esp_wifi_init(const wifi_init_config_t *cfg) { (void)cfg; return sdk(); }
static int esp_wifi_set_mode(int value) { (void)value; return sdk(); }
static int esp_wifi_start(void) { return sdk(); }
static int esp_wifi_set_channel(int ch, int second) { (void)ch; (void)second; return sdk(); }
static int esp_wifi_set_ps(int ps) { (void)ps; return sdk(); }
typedef struct { int unused; } esp_now_recv_info_t;
typedef struct { int unused; } esp_now_send_info_t;
typedef int esp_now_send_status_t;
#define ESP_NOW_SEND_SUCCESS 0
typedef struct { uint8_t peer_addr[6]; int channel, ifidx; bool encrypt; } esp_now_peer_info_t;
static bool peer_added, send_registered, recv_registered;
static int esp_now_init(void) { return sdk(); }
static bool esp_now_is_peer_exist(const uint8_t *addr) { (void)addr; return false; }
static int esp_now_add_peer(const esp_now_peer_info_t *p) {
    (void)p; int result = sdk(); if (!result) peer_added = true; return result;
}
static int esp_now_register_send_cb(void (*cb)(const esp_now_send_info_t *, esp_now_send_status_t)) {
    (void)cb; int result = sdk(); if (!result) send_registered = true; return result;
}
static int esp_now_register_recv_cb(void (*cb)(const esp_now_recv_info_t *, const uint8_t *, int)) {
    (void)cb; int result = sdk();
    if (!result) { if (!peer_added || !send_registered) ++lock_error; recv_registered = true; } return result;
}
static int radio_result, radio_count;
static uint8_t radio_byte;
static const uint8_t *radio_pointer;
static int esp_now_send(const uint8_t *addr, const uint8_t *data, size_t size) {
    (void)addr; if (size != 1) ++lock_error;
    ++radio_count; radio_byte = *data; radio_pointer = data; return radio_result;
}
typedef int hid_report_type_t;
#define HID_REPORT_TYPE_INPUT 1
#define HID_REPORT_TYPE_OUTPUT 2
#define HID_REPORT_TYPE_FEATURE 3
enum { TINYUSB_EVENT_ATTACHED, TINYUSB_EVENT_DETACHED, TINYUSB_EVENT_SUSPENDED, TINYUSB_EVENT_RESUMED };
typedef struct { int id; } tinyusb_event_t;
typedef struct {
    int bLength,bDescriptorType,bcdUSB,bMaxPacketSize0,idVendor,idProduct,bcdDevice;
    int iManufacturer,iProduct,iSerialNumber,bNumConfigurations;
} tusb_desc_device_t;
#define TUSB_DESC_DEVICE 1
#define CFG_TUD_ENDPOINT0_SIZE 64
#define CONFIG_TOUCHPAD_MANUFACTURER_STRING "manufacturer"
#define CONFIG_TOUCHPAD_PRODUCT_STRING "receiver"
#define CONFIG_TOUCHPAD_SERIAL_NUMBER_STRING "serial"
typedef struct {
    struct { const void *device, *full_speed_config; const char **string; unsigned string_count; } descriptor;
    void (*event_cb)(tinyusb_event_t *, void *);
} tinyusb_config_t;
#define TINYUSB_DEFAULT_CONFIG() ((tinyusb_config_t){0})
static int tinyusb_driver_install(const tinyusb_config_t *cfg) { (void)cfg; return sdk(); }
static bool mounted, suspended, endpoint_ready, usb_accept;
static int usb_count;
static uint8_t usb_instance, usb_id, usb_bytes[64];
static uint16_t usb_size;
static bool tud_mounted(void) { return mounted; }
static bool tud_suspended(void) { return suspended; }
static bool tud_hid_n_ready(uint8_t instance) { (void)instance; return endpoint_ready; }
static bool tud_hid_n_report(uint8_t instance, uint8_t id, const void *data, uint16_t size) {
    ++usb_count; usb_instance = instance; usb_id = id; usb_size = size; memcpy(usb_bytes, data, size);
    return usb_accept;
}
const uint8_t haptic_ptp_hid_report_descriptor[] = {0};
const uint8_t legacy_ptp_hid_report_descriptor[] = {0};
const uint8_t mouse_hid_report_descriptor[] = {0};
const uint8_t generic_hid_report_descriptor[] = {0};
const uint8_t desc_configuration[] = {0};
static int dfu_writes, restarts;
#define RTC_CNTL_OPTION1_REG 0
#define RTC_CNTL_FORCE_DOWNLOAD_BOOT 1
#define REG_WRITE(a,b) (++dfu_writes)
static void esp_restart(void) { ++restarts; }
#define ESP_MAC_WIFI_STA 0
static int esp_read_mac(uint8_t *mac, int type) { (void)type; memset(mac, 0, 6); return sdk(); }
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
static int nvs_init_result, nvs_erase_result, nvs_open_result, nvs_get_result, nvs_set_result, nvs_commit_result;
static int nvs_inits, nvs_erases, nvs_opens, nvs_sets, nvs_commits, nvs_closes;
static int nvs_flash_init(void) { return ++nvs_inits == 1 ? nvs_init_result : ESP_OK; }
static int nvs_flash_erase(void) { ++nvs_erases; return nvs_erase_result; }
static int nvs_open(const char *space, int rw, nvs_handle_t *h) {
    (void)space; *h = 1; ++nvs_opens;
    return rw == NVS_READONLY ? nvs_open_result : ESP_OK;
}
static int nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *out) {
    (void)h; (void)key; *out = 0; return nvs_get_result;
}
static int nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value) {
    (void)h; (void)key; (void)value; ++nvs_sets; return nvs_set_result;
}
static int nvs_commit(nvs_handle_t h) { (void)h; ++nvs_commits; return nvs_commit_result; }
static void nvs_close(nvs_handle_t h) { (void)h; ++nvs_closes; }
