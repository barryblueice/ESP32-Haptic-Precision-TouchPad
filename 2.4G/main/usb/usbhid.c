#include "usb/usbhid.h"
#include "input/input_pipeline.h"
#include "wireless/wireless.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "soc/rtc_cntl_reg.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tusb.h"
#include "class/hid/hid_device.h"
#include "sdkconfig.h"
#include <string.h>

/* Serializes submission with TinyUSB task callbacks; never held in an ISR.
 * Pipeline locks never acquire this mutex, so the lock order is one-way. */
static SemaphoreHandle_t usb_mutex;
static input_report_t usb_pending, usb_flight;
static bool usb_have_pending, usb_busy, dfu_requested;
static uint8_t button_press_threshold = 2, haptic_click_intensity = 2;

tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200, .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x0D00, .idProduct = 0x072D, .bcdDevice = 0x0100,
    .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1
};
static char const *string_desc[] = {
    (const char[]){0x09, 0x04},
    CONFIG_TOUCHPAD_MANUFACTURER_STRING,
    CONFIG_TOUCHPAD_PRODUCT_STRING,
    CONFIG_TOUCHPAD_SERIAL_NUMBER_STRING,
    "Precision Touchpad HID Interface"
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    switch (instance) {
    case 0: return generic_hid_report_descriptor;
    case REPORT_HAPTIC: return haptic_ptp_hid_report_descriptor;
    case REPORT_LEGACY: return legacy_ptp_hid_report_descriptor;
    case REPORT_MOUSE: return mouse_hid_report_descriptor;
    default: return NULL;
    }
}

static void usb_complete(uint8_t instance, bool success)
{
    if (instance < REPORT_HAPTIC || instance > REPORT_MOUSE) return;
    xSemaphoreTake(usb_mutex, portMAX_DELAY);
    if (usb_busy && instance == usb_flight.kind) {
        usb_busy = false;
        input_complete(&usb_flight, success);
    }
    xSemaphoreGive(usb_mutex);
    input_wake_sender();
}
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len)
{
    (void)report; (void)len;
    usb_complete(instance, true);
}
void tud_hid_report_failed_cb(uint8_t instance, hid_report_type_t type,
                              uint8_t const *report, uint16_t len)
{
    (void)report; (void)len;
    if (type == HID_REPORT_TYPE_INPUT) usb_complete(instance, false);
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t id, hid_report_type_t type,
                               uint8_t *buffer, uint16_t reqlen)
{
    if (!buffer || !reqlen || type != HID_REPORT_TYPE_FEATURE) return 0;
    bool haptic = instance == REPORT_HAPTIC, legacy = instance == REPORT_LEGACY;
    if (!haptic && !legacy) return 0;
    if ((haptic && id == REPORTID_HAPTIC_FEATURE) || (legacy && id == REPORTID_LEGACY_FEATURE)) {
        buffer[0] = 3;
        return 1;
    }
    if (id == REPORTID_MAX_COUNT) { buffer[0] = 0x15; return 1; }
    if ((haptic && id == REPORTID_HAPTIC_PTPHQA) || (legacy && id == REPORTID_LEGACY_PTPHQA)) {
        uint16_t count = reqlen < 256 ? reqlen : 256;
        memset(buffer, 0, count);
        return count;
    }
    if (!haptic) return 0;
    if (id == REPORTID_BUTTON_PRESS_THRESHOLD) { buffer[0] = button_press_threshold; return 1; }
    if (id == REPORTID_HAPTIC_INTENSITY) { buffer[0] = haptic_click_intensity; return 1; }
    if (id == REPORTID_HAPTIC_WAVEFORM_LIST) {
        static const uint8_t waveforms[15] = {1,16, 2,16, 3,16, 4,16, 5,16, 20,20,20,20,20};
        uint16_t count = reqlen < sizeof(waveforms) ? reqlen : sizeof(waveforms);
        memcpy(buffer, waveforms, count);
        return count;
    }
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t id, hid_report_type_t type,
                           uint8_t const *buffer, uint16_t size)
{
    if (!buffer || !size) return;
    /* The generic interface is the only DFU command endpoint. */
    if (instance == 0 && type == HID_REPORT_TYPE_OUTPUT &&
        (id == REPORTID_DFU_CMD || (id == 0 && buffer[0] == REPORTID_DFU_CMD))) {
        xSemaphoreTake(usb_mutex, portMAX_DELAY);
        dfu_requested = true;
        xSemaphoreGive(usb_mutex);
        input_wake_sender();
        return;
    }
    if (instance != REPORT_HAPTIC && instance != REPORT_LEGACY) return;
    if (id == 0) { id = *buffer++; --size; }
    if (!size) return;
    bool haptic = instance == REPORT_HAPTIC;
    if (type == HID_REPORT_TYPE_FEATURE) {
        if ((haptic && id == REPORTID_HAPTIC_FEATURE) ||
            (!haptic && id == REPORTID_LEGACY_FEATURE)) {
            input_set_mode(buffer[0] == 3 ? TP_PTP_MODE : TP_MOUSE_MODE);
            wireless_request_mode();
        } else if (haptic && id == REPORTID_BUTTON_PRESS_THRESHOLD) {
            button_press_threshold = buffer[0] < 1 ? 1 : (buffer[0] > 3 ? 3 : buffer[0]);
        } else if (haptic && id == REPORTID_HAPTIC_INTENSITY) {
            haptic_click_intensity = buffer[0] > 4 ? 4 : buffer[0];
        }
    } else if (haptic && type == HID_REPORT_TYPE_OUTPUT &&
               id == REPORTID_HAPTIC_MANUAL_TRIGGER && size >= 7) {
        /* Existing receiver-local command; the current radio ABI has no haptic downlink. */
        ESP_LOGD("USB", "Local haptic output, %u bytes", size);
    }
}

static void tinyusb_event_cb(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    xSemaphoreTake(usb_mutex, portMAX_DELAY);
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        /* A bus reset can reconfigure without a preceding DETACHED callback.
         * Mount means TinyUSB has reopened/reset the endpoint state. */
        usb_busy = false;
        usb_have_pending = false;
        input_set_mode(TP_MOUSE_MODE);
        input_set_usb(true);
        wireless_request_mode();
        break;
    case TINYUSB_EVENT_DETACHED:
        input_set_usb(false);
        input_set_mode(TP_MOUSE_MODE);
        /* TinyUSB has closed/reset endpoints; no transfer survives this event. */
        usb_busy = false;
        usb_have_pending = false;
        wireless_request_mode();
        break;
    case TINYUSB_EVENT_SUSPENDED:
        input_set_usb(false);
        break;
    case TINYUSB_EVENT_RESUMED:
        input_set_usb(true);
        wireless_request_mode();
        break;
    default: break;
    }
    xSemaphoreGive(usb_mutex);
}

static void enter_dfu_mode(void)
{
    ESP_LOGW("USB", "Entering ROM DFU");
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static void usbhid_step(void)
{
    xSemaphoreTake(usb_mutex, portMAX_DELAY);
    bool dfu = dfu_requested;
    dfu_requested = false;
    if (usb_have_pending && !input_current(&usb_pending)) usb_have_pending = false;
    /* One in-flight input globally preserves cross-interface recovery ordering. */
    if (!usb_busy && !usb_have_pending) usb_have_pending = input_take(&usb_pending);
    if (!dfu && !usb_busy && usb_have_pending && tud_mounted() && !tud_suspended()) {
        uint8_t instance = (uint8_t)usb_pending.kind;
        if (tud_hid_n_ready(instance) && input_current(&usb_pending)) {
            usb_flight = usb_pending;
            usb_busy = true;
            uint8_t id = instance == REPORT_HAPTIC ? REPORTID_HAPTIC_TOUCHPAD :
                         instance == REPORT_LEGACY ? REPORTID_LEGACY_TOUCHPAD : REPORTID_MOUSE;
            if (tud_hid_n_report(instance, id, &usb_flight.data, report_size(usb_flight.kind))) {
                input_submitted(&usb_flight);
                usb_have_pending = false;
            } else {
                usb_busy = false;
                input_submit_failed();
            }
        }
    }
    xSemaphoreGive(usb_mutex);
    if (dfu) enter_dfu_mode();
}

void usbhid_task(void *arg)
{
    (void)arg;
    input_register_sender();
    while (true) {
        usbhid_step();
        input_log_stats();
        /* Completion and input notify immediately; 1 ms bounds busy retries. */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
    }
}
void usbhid_init(void)
{
    usb_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(usb_mutex ? ESP_OK : ESP_ERR_NO_MEM);
    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG();
    cfg.descriptor.device = &desc_device;
    cfg.descriptor.full_speed_config = desc_configuration;
    cfg.descriptor.string = string_desc;
    cfg.descriptor.string_count = sizeof(string_desc) / sizeof(string_desc[0]);
    cfg.event_cb = tinyusb_event_cb;
    ESP_ERROR_CHECK(tinyusb_driver_install(&cfg));
    ESP_ERROR_CHECK(xTaskCreate(usbhid_task, "hid", 4096, NULL, 12, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
}
