#include "esp_log.h"
#include <inttypes.h>
#include "soc/rtc_cntl_reg.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tusb.h"
#include "class/hid/hid_device.h"

#include "esp_wifi.h"
#include "esp_now.h"

#include "esp_timer.h"

#include "math.h"

#include "I2C/TP/i2c_hid.h"
#include "I2C/SUB_DEV/cs40l25_surface.h"

#include "SYS/hid_msg.h"
#include "SYS/input_pipeline.h"

#include "USB/usbhid.h"
#include "USB/usb_config.h"
#include "USB/usb_aux.h"
#include "SYS/device_config.h"

// #include "wireless/wireless.h"

#include "sdkconfig.h"

#define TPD_REPORT_SIZE   6

#define TAG "USB_HID_TP"

#define REPORTID_TOUCHPAD               0x01
#define REPORTID_MOUSE                  0x02
#define REPORTID_MAX_COUNT              0x03
#define REPORTID_PTPHQA                 0x04
#define REPORTID_FEATURE                0x05
#define REPORTID_FUNCTION_SWITCH        0x06
#define REPORTID_BUTTON_PRESS_THRESHOLD 0x40
#define REPORTID_HAPTIC_INTENSITY       0x41
#define REPORTID_HAPTIC_FEATURE         0x0C

#define TPD_REPORT_ID 0x01
#define TPD_REPORT_SIZE_WITHOUT_ID (sizeof(touchpad_report_t) - 1)

#define REPORTID_DFU_CMD  0xFF
_Static_assert(CFG_TUD_HID_EP_BUFSIZE >= 257, "PTP Feature control buffer must include the Report ID");

void enter_dfu_mode(void) {

    ESP_LOGW(TAG, "Preparing to enter ROM DFU mode...");
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

// USB Device Descriptor
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x0D00,
    .idProduct          = 0x072C,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

// String Descriptors
char const* string_desc[] = {
    (const char[]){0x09, 0x04},                  // 0: Language
    CONFIG_TOUCHPAD_MANUFACTURER_STRING,         // 1: Manufacturer
    CONFIG_TOUCHPAD_PRODUCT_STRING,              // 2: Product
    CONFIG_TOUCHPAD_SERIAL_NUMBER_STRING,        // 3: Serial Number
    "Precision Touchpad HID Interface"           // 4: HID Interface
};

// TinyUSB callbacks
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    // return (instance == 0) ? ptp_hid_report_descriptor : mouse_hid_report_descriptor;
    switch (instance) {
    case 0:
        return generic_hid_report_descriptor;
    case 1:
        return ptp_hid_report_descriptor;
    case 2:
        return mouse_hid_report_descriptor;
    default:
        return NULL;
    }
    return NULL;
}

static uint8_t ptp_input_mode = 0x00;
static portMUX_TYPE usb_tx_lock = portMUX_INITIALIZER_UNLOCKED;
static input_report_t usb_flight[3];
static bool usb_busy[3];
static bool usb_aux_flight;
static usb_aux_report_t usb_aux_buffer;

static void usb_complete(uint8_t instance, bool success)
{
    if (instance == 0) { usb_config_complete(success); return; }
    if (instance < 1 || instance > 2) return;
    taskENTER_CRITICAL(&usb_tx_lock);
    bool busy = usb_busy[instance];
    input_report_t report = usb_flight[instance];
    bool aux = instance == 2 && usb_aux_flight;
    if (aux) usb_aux_flight = false;
    usb_busy[instance] = false;
    taskEXIT_CRITICAL(&usb_tx_lock);
    if (busy && aux) { usb_aux_complete(success); if (!success) input_recover(); }
    else if (busy) {
        if (success) input_report_ack(&report);
        else {
            input_submit_failed();
            if (input_report_current(&report)) input_recover();
        }
    }
    input_wake_sender();
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len)
{
    (void)report; (void)len;
    usb_complete(instance, true);
}
void tud_hid_report_failed_cb(uint8_t instance, hid_report_type_t type, uint8_t const *report, uint16_t len)
{
    (void)type; (void)report; (void)len;
    usb_complete(instance, false);
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
    if (buffer == NULL || reqlen == 0) return 0;
    if (instance == 1 && report_type == HID_REPORT_TYPE_FEATURE) {
        if (report_id == REPORTID_FEATURE) {
            buffer[0] = 0x03;
            return 1;
        }
        if (report_id == REPORTID_MAX_COUNT) {
            buffer[0] = 0x15;
            return 1;
        }
        if (report_id == REPORTID_PTPHQA) {
            uint16_t count = reqlen < 256 ? reqlen : 256;
            memset(buffer, 0, count);
            return count;
        }
        if (report_id == REPORTID_BUTTON_PRESS_THRESHOLD) {
            buffer[0] = device_config_ready() ? device_config_value(CFG_LEVEL) : ptp_button_press_threshold;
            return 1;
        }
        if (report_id == REPORTID_HAPTIC_INTENSITY) {
            if (buffer == NULL || reqlen < 1) return 0;
            buffer[0] = ptp_haptic_click_intensity_get();
            return 1;
        }

    }
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
    if (instance == 0) {
        if (report_type == HID_REPORT_TYPE_OUTPUT && report_id == 0) usb_config_receive(buffer, bufsize);
        return;
    }
    if (instance != 1 || report_type != HID_REPORT_TYPE_FEATURE) return;
    if (bufsize == 0 || buffer == NULL) {
        ESP_LOGW(TAG, "SET_REPORT empty: instance=%u report_id=0x%02X type=%u", instance, report_id, report_type);
        return;
    }

    uint8_t effective_report_id = report_id;
    uint8_t const *payload = buffer;
    uint16_t payload_size = bufsize;

    if (report_id == 0 && bufsize > 1) {
        switch (buffer[0]) {
            case REPORTID_FEATURE:
            case REPORTID_BUTTON_PRESS_THRESHOLD:
            case REPORTID_HAPTIC_INTENSITY:
                effective_report_id = buffer[0];
                payload = &buffer[1];
                payload_size = bufsize - 1;
                break;

            default:
                break;
        }
    }

    if (payload_size == 0) {
        ESP_LOGW(TAG, "SET_REPORT empty payload: instance=%u report_id=0x%02X type=%u", instance, effective_report_id, report_type);
        return;
    }
    for (unsigned i = 1; i < payload_size; ++i) if (payload[i]) return;

    if (report_type == HID_REPORT_TYPE_FEATURE && effective_report_id == REPORTID_FEATURE) {
        if (payload_size >= 1) {
            if (payload[0] != 0 && payload[0] != 3) return;
            ptp_input_mode = payload[0];
            input_request_mode(ptp_input_mode == 0x03 ? PTP_MODE : MOUSE_MODE);
            ESP_LOGI(TAG, "PTP input mode SET_FEATURE: instance=%u mode=0x%02X", instance, ptp_input_mode);
        }
    }

    if (report_type == HID_REPORT_TYPE_FEATURE && effective_report_id == REPORTID_BUTTON_PRESS_THRESHOLD) {
        if (payload[0] < 1 || payload[0] > 3) return;
        usb_config_legacy(REPORTID_BUTTON_PRESS_THRESHOLD, payload[0]);

        ESP_LOGI(TAG,
                 "Button press threshold SET_FEATURE: instance=%u raw=0x%02X threshold=%u",
                 instance,
                 payload[0],
                 ptp_button_press_threshold);
    }

    if (report_type == HID_REPORT_TYPE_FEATURE && effective_report_id == REPORTID_HAPTIC_INTENSITY) {
        if (payload[0] > 100) return;
        usb_config_legacy(REPORTID_HAPTIC_INTENSITY, payload[0]);
    }

    if (report_id == REPORTID_DFU_CMD ||
        (report_id == 0 && buffer[0] == REPORTID_DFU_CMD)) {
        usb_config_dfu();
    }
}
static void tinyusb_event_cb(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        usb_aux_reset(true);
        input_set_link(3);
        ptp_input_mode = 0;
        input_request_mode(MOUSE_MODE);
        break;
    case TINYUSB_EVENT_DETACHED:
        usb_config_detach();
        usb_aux_reset(false);
        input_set_link(0);
        /* The stack has closed the endpoints; no transfer survives detach. */
        taskENTER_CRITICAL(&usb_tx_lock);
        usb_busy[1] = usb_busy[2] = false;
        usb_aux_flight = false;
        taskEXIT_CRITICAL(&usb_tx_lock);
        ptp_input_mode = 0;
        break;
    case TINYUSB_EVENT_SUSPENDED:
        usb_aux_cancel();
        input_set_link(0);
        break;
    case TINYUSB_EVENT_RESUMED:
        usb_aux_resume();
        input_set_link(3);
        break;
    default:
        break;
    }
}

void usbhid_init(void) {
    usb_descriptor_init();
    ESP_ERROR_CHECK(usb_config_init());
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.device = &desc_device;
    tusb_cfg.descriptor.full_speed_config = desc_configuration;
    tusb_cfg.descriptor.string = string_desc;
    tusb_cfg.event_cb = tinyusb_event_cb;
    tusb_cfg.descriptor.string_count = sizeof(string_desc)/sizeof(string_desc[0]);

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
}

void usbhid_task(void *arg)
{
    (void)arg;
    input_register_sender();
    input_report_t pending;
    bool have_pending = false;
    bool prefer_aux = true;
    while (true) {
        input_log_stats();
        if (tud_mounted() && !tud_suspended()) usb_config_send();
        if (have_pending && !input_report_current(&pending)) have_pending = false;
        taskENTER_CRITICAL(&usb_tx_lock);
        bool busy = usb_busy[1] || (usb_busy[2] && !usb_aux_flight);
        bool aux_busy = usb_busy[2];
        taskEXIT_CRITICAL(&usb_tx_lock);
        if (!have_pending && !busy) have_pending = input_take_report(&pending);
        if (!aux_busy && tud_mounted() && !tud_suspended() && tud_hid_n_ready(2) &&
            (usb_aux_release_pending() || prefer_aux || !have_pending) && usb_aux_take(&usb_aux_buffer, input_generation(), (uint32_t)(esp_timer_get_time() / 1000))) {
            taskENTER_CRITICAL(&usb_tx_lock);
            usb_busy[2] = true; usb_aux_flight = true;
            taskEXIT_CRITICAL(&usb_tx_lock);
            if (!usb_aux_report_current(&usb_aux_buffer) ||
                !tud_hid_n_report(2, usb_aux_buffer.id, usb_aux_buffer.data, usb_aux_buffer.length)) {
                taskENTER_CRITICAL(&usb_tx_lock); usb_busy[2] = false; usb_aux_flight = false; taskEXIT_CRITICAL(&usb_tx_lock);
                usb_aux_unsubmitted();
            } else prefer_aux = false;
            aux_busy = true;
        }
        if (!busy && have_pending && !usb_aux_neutral_pending() && !(pending.mode == MOUSE_MODE && aux_busy) && tud_mounted() && !tud_suspended()) {
            uint8_t instance = pending.mode == PTP_MODE ? 1 : 2;
            if (tud_hid_n_ready(instance) && input_report_current(&pending)) {
                taskENTER_CRITICAL(&usb_tx_lock);
                usb_flight[instance] = pending;
                usb_busy[instance] = true;
                taskEXIT_CRITICAL(&usb_tx_lock);
                bool accepted = pending.mode == PTP_MODE ?
                    tud_hid_n_report(instance, REPORTID_TOUCHPAD, &pending.data.ptp, sizeof(ptp_report_t)) :
                    tud_hid_n_report(instance, REPORTID_MOUSE, &pending.data.mouse, sizeof(mouse_hid_report_t));
                if (accepted) { have_pending = false; prefer_aux = true; }
                else {
                    taskENTER_CRITICAL(&usb_tx_lock);
                    usb_busy[instance] = false;
                    taskEXIT_CRITICAL(&usb_tx_lock);
                    input_submit_failed();
                    vTaskDelay(1);
                }
            }
        }
        /* New input and completion callbacks wake this task immediately. */
        ulTaskNotifyTake(pdTRUE, (have_pending || busy || aux_busy || usb_aux_active() || usb_config_active()) ? 1 : portMAX_DELAY);
    }
}
