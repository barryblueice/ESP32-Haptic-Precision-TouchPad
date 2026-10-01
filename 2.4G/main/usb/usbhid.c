#include "SYS/aux_output.h"
#include "wireless/receiver_extension.h"
#include "wireless/receiver_settings.h"
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
#include "device/usbd_pvt.h"
#include "sdkconfig.h"
#include <string.h>

/* Serializes submission with TinyUSB task callbacks; never held in an ISR.
 * Pipeline locks never acquire this mutex, so the lock order is one-way. */
static SemaphoreHandle_t usb_mutex;
static input_report_t usb_pending, usb_flight;
static bool usb_have_pending, usb_busy, dfu_requested;
static bool usb_aux_flight, prefer_aux = true;
static aux_output_report_t auxiliary;
static uint8_t surface_rotation;

/* Remote wakeup has its own spinlock: a request can arrive from the wireless
 * task while it holds the input pipeline lock, whereas the remaining USB state
 * is owned by usb_mutex. usb_mutex may nest this lock, never the reverse. */
static portMUX_TYPE wake_lock = portMUX_INITIALIZER_UNLOCKED;
static bool usb_remote_wakeup_enabled, usb_remote_wakeup_requested, usb_remote_wakeup_attempted;

void usbhid_remote_wakeup_request(void)
{
    taskENTER_CRITICAL(&wake_lock);
    if (!usb_remote_wakeup_attempted) usb_remote_wakeup_requested = true;
    taskEXIT_CRITICAL(&wake_lock);
    input_wake_sender();
}

static void usb_wakeup_clear(void)
{
    taskENTER_CRITICAL(&wake_lock);
    usb_remote_wakeup_enabled = usb_remote_wakeup_requested = usb_remote_wakeup_attempted = false;
    taskEXIT_CRITICAL(&wake_lock);
}

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
    if (usb_busy && usb_aux_flight && instance == REPORT_MOUSE) {
        usb_busy = usb_aux_flight = false; aux_output_complete(success);
    } else if (usb_busy && !usb_aux_flight && instance == usb_flight.kind) {
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
        buffer[0] = input_mode() == TP_PTP_MODE ? 3 : 0;
        return 1;
    }
    if (id == REPORTID_MAX_COUNT) { buffer[0] = 0x15; return 1; }
    if ((haptic && id == REPORTID_HAPTIC_PTPHQA) || (legacy && id == REPORTID_LEGACY_PTPHQA)) {
        uint16_t count = reqlen < 256 ? reqlen : 256;
        memset(buffer, 0, count);
        return count;
    }
    if (!haptic) return 0;
    if (id == REPORTID_BUTTON_PRESS_THRESHOLD) { buffer[0] = receiver_settings_get(WIRE_SETTING_LEVEL); return 1; }
    if (id == REPORTID_HAPTIC_INTENSITY) { buffer[0] = receiver_settings_get(WIRE_SETTING_INTENSITY); return 1; }
    if (id == REPORTID_HAPTIC_WAVEFORM_LIST) {
        static const uint8_t waveforms[15] = {1,16, 2,16, 3,16, 4,16, 5,16, 20,20,20,20,20};
        uint16_t count = reqlen < sizeof(waveforms) ? reqlen : sizeof(waveforms);
        memcpy(buffer, waveforms, count);
        return count;
    }
    return 0;
}

/* Report IDs this interface owns. Windows may deliver SET_REPORT with the
 * report ID folded into the payload's first byte instead of in wValue. */
static bool usb_owns_report_id(uint8_t instance, uint8_t id)
{
    if (id == REPORTID_MOUSE || id == REPORTID_MAX_COUNT || id == REPORTID_FUNCTION_SWITCH) return true;
    if (instance == REPORT_HAPTIC)
        return id == REPORTID_HAPTIC_TOUCHPAD || id == REPORTID_HAPTIC_PTPHQA ||
               id == REPORTID_HAPTIC_FEATURE || id == REPORTID_BUTTON_PRESS_THRESHOLD ||
               id == REPORTID_HAPTIC_INTENSITY || id == REPORTID_HAPTIC_WAVEFORM_LIST ||
               id == REPORTID_HAPTIC_MANUAL_TRIGGER;
    if (instance == REPORT_LEGACY)
        return id == REPORTID_LEGACY_TOUCHPAD || id == REPORTID_LEGACY_PTPHQA ||
               id == REPORTID_LEGACY_FEATURE;
    return false;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t id, hid_report_type_t type,
                           uint8_t const *buffer, uint16_t size)
{
    if (!buffer) return;
    if (!size) {
        ESP_LOGW("USB", "Feature rejected: empty SET_REPORT instance=%u id=0x%02X", instance, id);
        return;
    }
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
    if (id == 0) {
        /* Only strip a leading byte that really is a report ID we own, so a
         * value byte is never mistaken for one. */
        if (size >= 2 && usb_owns_report_id(instance, buffer[0])) { id = *buffer++; --size; }
        else {
            ESP_LOGW("USB", "Feature rejected: missing report ID instance=%u size=%u", instance, size);
            return;
        }
    }
    if (!size) {
        ESP_LOGW("USB", "Feature rejected: empty payload id=0x%02X", id);
        return;
    }
    bool haptic = instance == REPORT_HAPTIC;
    bool trailing = false;
    for (unsigned i = 1; i < size; ++i) if (buffer[i]) trailing = true;
    if (type == HID_REPORT_TYPE_FEATURE) {
        if ((haptic && id == REPORTID_HAPTIC_FEATURE) ||
            (!haptic && id == REPORTID_LEGACY_FEATURE)) {
            if (trailing) { ESP_LOGW("USB", "Feature rejected: trailing data id=0x%02X", id); return; }
            input_set_mode(buffer[0] == 3 ? TP_PTP_MODE : TP_MOUSE_MODE);
            wireless_request_mode();
            ESP_LOGI("USB", "Feature applied: id=0x%02X mode=%u", id, input_mode());
        } else if (haptic && id == REPORTID_BUTTON_PRESS_THRESHOLD) {
            if (trailing) { ESP_LOGW("USB", "Feature rejected: trailing data id=0x%02X", id); return; }
            if (receiver_settings_set(WIRE_SETTING_LEVEL, buffer[0]))
                ESP_LOGI("USB", "Feature applied: id=0x%02X level=%u", id, buffer[0]);
            else
                ESP_LOGW("USB", "Feature rejected: id=0x%02X invalid level=%u", id, buffer[0]);
        } else if (haptic && id == REPORTID_HAPTIC_INTENSITY) {
            if (trailing) { ESP_LOGW("USB", "Feature rejected: trailing data id=0x%02X", id); return; }
            if (receiver_settings_set(WIRE_SETTING_INTENSITY, buffer[0]))
                ESP_LOGI("USB", "Feature applied: id=0x%02X intensity=%u", id, buffer[0]);
            else
                ESP_LOGW("USB", "Feature rejected: id=0x%02X invalid intensity=%u", id, buffer[0]);
        } else {
            ESP_LOGD("USB", "Feature ignored: id=0x%02X instance=%u", id, instance);
        }
    } else if (haptic && type == HID_REPORT_TYPE_OUTPUT &&
               id == REPORTID_HAPTIC_MANUAL_TRIGGER && size >= 7) {
        /* Manual waveform playback is not part of Windows Feature settings. */
        ESP_LOGD("USB", "Local haptic output, %u bytes", size);
    }
}

/* TinyUSB resets its class drivers on BUS_RESET, even without DETACHED.
 * Reset the host's mode here, before any subsequent mode negotiation. */
static void usb_session_reset(uint8_t rhport)
{
    (void)rhport;
    xSemaphoreTake(usb_mutex, portMAX_DELAY);
    usb_wakeup_clear();
    input_set_usb(false);
    input_set_mode(TP_MOUSE_MODE);
    usb_busy = usb_aux_flight = usb_have_pending = false;
    aux_output_reset(false);
    receiver_ext_usb_ready(false);
    xSemaphoreGive(usb_mutex);
    wireless_request_mode();
}

static void usb_session_init(void) { usb_session_reset(0); }
static uint16_t usb_session_open(uint8_t rhport, tusb_desc_interface_t const *desc, uint16_t len)
{
    (void)rhport; (void)desc; (void)len;
    return 0; /* Observe resets; the built-in HID driver owns every interface. */
}
usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *count)
{
    static const usbd_class_driver_t driver = {
        .name = "receiver-session", .init = usb_session_init,
        .reset = usb_session_reset, .open = usb_session_open,
    };
    *count = 1;
    return &driver;
}

static void tinyusb_event_cb(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    xSemaphoreTake(usb_mutex, portMAX_DELAY);
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        /* The reset observer retired the previous host session. Preserve any
         * mode request already received by the new session. */
        usb_busy = usb_aux_flight = false;
        aux_output_reset(true);
        usb_have_pending = false;
        input_set_usb(true);
        wireless_request_mode();
        ESP_LOGI("USB", "USB configured: mode=%u", input_mode());
        break;
    case TINYUSB_EVENT_DETACHED:
        input_set_usb(false);
        input_set_mode(TP_MOUSE_MODE);
        /* TinyUSB has closed/reset endpoints; no transfer survives this event. */
        usb_busy = usb_aux_flight = false;
        aux_output_reset(false);
        usb_have_pending = false;
        usb_wakeup_clear();
        wireless_request_mode();
        break;
    case TINYUSB_EVENT_SUSPENDED:
        aux_output_cancel();
        receiver_ext_usb_ready(false);
        input_set_usb(false);
        /* Record whether the host authorized this device to wake it, and arm a
         * fresh single attempt for this suspend. */
        taskENTER_CRITICAL(&wake_lock);
        usb_remote_wakeup_enabled = event->suspended.remote_wakeup;
        usb_remote_wakeup_requested = false;
        usb_remote_wakeup_attempted = false;
        taskEXIT_CRITICAL(&wake_lock);
        ESP_LOGI("USB", "USB suspended, remote wakeup %s",
                 event->suspended.remote_wakeup ? "authorized" : "not authorized");
        break;
    case TINYUSB_EVENT_RESUMED:
        aux_output_resume();
        input_set_usb(true);
        usb_wakeup_clear();
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

/* At most one remote-wakeup attempt per host suspend, issued in task context. */
static void usb_wakeup_step(void)
{
    if (!tud_suspended()) return;
    bool attempt = false, authorized = false;
    taskENTER_CRITICAL(&wake_lock);
    if (usb_remote_wakeup_requested && !usb_remote_wakeup_attempted) {
        usb_remote_wakeup_requested = false;
        usb_remote_wakeup_attempted = true;
        attempt = true;
        authorized = usb_remote_wakeup_enabled;
    }
    taskEXIT_CRITICAL(&wake_lock);
    if (!attempt) return;
    if (authorized && tud_remote_wakeup())
        ESP_LOGI("USB", "Remote wake requested by touchpad input");
    else
        ESP_LOGI("USB", "Remote wake unavailable: host did not authorize it");
}

static void usbhid_step(void)
{
    usb_wakeup_step();
    wire_surface_t surface;
    if (receiver_ext_apply(&surface)) {
        input_recover(); aux_output_cancel();
        if ((surface.rotation & 1) != (surface_rotation & 1)) {
            receiver_ext_usb_ready(false);
            tud_disconnect(); vTaskDelay(pdMS_TO_TICKS(100));
            xSemaphoreTake(usb_mutex,portMAX_DELAY);
            usb_busy = usb_aux_flight = usb_have_pending = false;
            aux_output_reset(false);
            receiver_descriptor_rotation(surface.rotation);
            xSemaphoreGive(usb_mutex);
            tud_connect();
        }
        surface_rotation = surface.rotation;
        receiver_ext_applied(&surface);
        /* Main starts each radio session in PTP mode. Replay the host's
         * selection even when USB remained attached throughout the switch. */
        wireless_request_mode();
    }
    receiver_ext_usb_ready(tud_mounted() && !tud_suspended());
    xSemaphoreTake(usb_mutex, portMAX_DELAY);
    bool dfu = dfu_requested;
    dfu_requested = false;
    if (usb_have_pending && !input_current(&usb_pending)) usb_have_pending = false;
    /* One in-flight input globally preserves cross-interface recovery ordering. */
    if (!usb_busy && !usb_have_pending) usb_have_pending = input_take(&usb_pending);
    if (!dfu && !usb_busy && tud_mounted() && !tud_suspended() && tud_hid_n_ready(REPORT_MOUSE) &&
        (!usb_have_pending || !usb_pending.release) &&
        (aux_output_release_pending() || prefer_aux || !usb_have_pending) &&
        aux_output_take(&auxiliary,input_generation(),input_now_ms())) {
        usb_busy = usb_aux_flight = true;
        if (!aux_output_report_current(&auxiliary) ||
            !tud_hid_n_report(REPORT_MOUSE,auxiliary.id,auxiliary.data,auxiliary.length)) {
            usb_busy = usb_aux_flight = false; aux_output_unsubmitted();
        } else prefer_aux = false;
    }
    if (!dfu && !usb_busy && usb_have_pending && tud_mounted() && !tud_suspended()) {
        uint8_t instance = (uint8_t)usb_pending.kind;
        if (tud_hid_n_ready(instance) && input_current(&usb_pending)) {
            usb_flight = usb_pending;
            usb_busy = true;
            uint8_t id = instance == REPORT_HAPTIC ? REPORTID_HAPTIC_TOUCHPAD :
                         instance == REPORT_LEGACY ? REPORTID_LEGACY_TOUCHPAD : REPORTID_MOUSE;
            if (tud_hid_n_report(instance, id, &usb_flight.data, report_size(usb_flight.kind))) {
                prefer_aux = true;
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
