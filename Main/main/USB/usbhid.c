#include "esp_log.h"
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

#include "SYS/hid_msg.h"

#include "USB/usbhid.h"

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
#define REPORTID_HAPTIC_WAVEFORM_LIST   0x42
#define REPORTID_HAPTIC_MANUAL_TRIGGER  0x43
#define REPORTID_HAPTIC_FEATURE         0x0C

#define TPD_REPORT_ID 0x01
#define TPD_REPORT_SIZE_WITHOUT_ID (sizeof(touchpad_report_t) - 1)

#define REPORTID_DFU_CMD  0xFF

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
static uint8_t button_press_threshold = 0x02;
static uint8_t haptic_click_intensity = 0x02;

static uint16_t read_le16(uint8_t const *buffer) {
    return (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8);
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
    if (report_type == HID_REPORT_TYPE_FEATURE) {
        if (report_id == REPORTID_FEATURE) {
            buffer[0] = 0x03;
            return 1;
        }
        if (report_id == REPORTID_MAX_COUNT) {
            buffer[0] = 0x15;
            return 1;
        }
        if (report_id == REPORTID_PTPHQA) {
            memset(buffer, 0, 256);
            return 256;
        }
        if (report_id == REPORTID_BUTTON_PRESS_THRESHOLD) {
            buffer[0] = button_press_threshold;
            return 1;
        }
        if (report_id == REPORTID_HAPTIC_INTENSITY) {
            buffer[0] = haptic_click_intensity;
            return 1;
        }
        if (report_id == REPORTID_HAPTIC_WAVEFORM_LIST) {
            uint16_t *waveforms = (uint16_t *)&buffer[0];
            waveforms[0] = 4097; // Instance 3
            waveforms[1] = 4098; // Instance 4
            waveforms[2] = 4099; // Instance 5
            waveforms[3] = 4100; // Instance 6
            waveforms[4] = 4101; // Instance 7

            buffer[10] = 20; // Instance 3 duration
            buffer[11] = 20;
            buffer[12] = 20;
            buffer[13] = 20;
            buffer[14] = 20;

            return 15;
        }
    }
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
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
            case REPORTID_HAPTIC_MANUAL_TRIGGER:
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

    if (report_type == HID_REPORT_TYPE_FEATURE && effective_report_id == REPORTID_FEATURE) {
        if (payload_size >= 1) {
            ptp_input_mode = payload[0];
            ESP_LOGI(TAG, "PTP input mode SET_FEATURE: instance=%u mode=0x%02X", instance, ptp_input_mode);
        }
    }

    if (report_type == HID_REPORT_TYPE_FEATURE && effective_report_id == REPORTID_BUTTON_PRESS_THRESHOLD) {
        button_press_threshold = payload[0];
        if (button_press_threshold < 0x01) {
            button_press_threshold = 0x01;
        } else if (button_press_threshold > 0x03) {
            button_press_threshold = 0x03;
        }

        ESP_LOGI(TAG,
                 "Button press threshold SET_FEATURE: instance=%u raw=0x%02X threshold=%u",
                 instance,
                 payload[0],
                 button_press_threshold);
    }

    if (report_type == HID_REPORT_TYPE_FEATURE && effective_report_id == REPORTID_HAPTIC_INTENSITY) {
        haptic_click_intensity = payload[0];
        if (haptic_click_intensity > 0x04) {
            haptic_click_intensity = 0x04;
        }

        ESP_LOGI(TAG,
                 "Haptic click SET_FEATURE: instance=%u raw=0x%02X enabled=%s intensity=%u",
                 instance,
                 payload[0],
                 haptic_click_intensity > 0 ? "true" : "false",
                 haptic_click_intensity);
    }

    if (report_type == HID_REPORT_TYPE_OUTPUT && effective_report_id == REPORTID_HAPTIC_MANUAL_TRIGGER) {
        uint8_t waveform = (payload_size >= 1) ? payload[0] : 0;
        uint8_t intensity = (payload_size >= 2) ? payload[1] : 0;
        uint8_t repeat_count = (payload_size >= 3) ? payload[2] : 0;
        uint16_t retrigger_period_ms = (payload_size >= 5) ? read_le16(&payload[3]) : 0;
        uint16_t cutoff_time_ms = (payload_size >= 7) ? read_le16(&payload[5]) : 0;

        ESP_LOGI(TAG,
                 "Haptic signal OUTPUT: instance=%u waveform=%u intensity=%u repeat=%u retrigger_ms=%u cutoff_ms=%u len=%u",
                 instance,
                 waveform,
                 intensity,
                 repeat_count,
                 retrigger_period_ms,
                 cutoff_time_ms,
                 payload_size);
    }

    if (buffer[0] == REPORTID_DFU_CMD) {
        enter_dfu_mode();
    }
}
#define USB_CONNECTED BIT0

EventGroupHandle_t usb_event_group;

static uint8_t last_ptp_input_mode = 0xFF;

void usb_mount_task(void *arg) {
    while (1) {

        xEventGroupWaitBits(
            usb_event_group,
            USB_CONNECTED,
            pdTRUE,
            pdTRUE,
            portMAX_DELAY
        );

        ptp_input_mode = 0x00;
        last_ptp_input_mode = 0xFF;

        while (tud_mounted()) {

            if (ptp_input_mode != last_ptp_input_mode) {

                switch (ptp_input_mode) {

                    case 0x03:
                        ESP_LOGI(TAG, "Mode 0x03 detected: Activating PTP");
                        current_tp_mode = PTP_MODE;
                        touchpad_mode_set(true);
                        // activate_ptp();
                        break;

                    default:
                        ESP_LOGW(TAG, "Mode 0x%02X detected: Activating Default Mouse Mode", ptp_input_mode);
                        current_tp_mode = MOUSE_MODE;
                        touchpad_mode_set(false);
                        break;
                    }

                last_ptp_input_mode = ptp_input_mode;
            }

            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

static void tinyusb_event_cb(tinyusb_event_t *event, void *arg) {
    switch (event->id) {

        case TINYUSB_EVENT_ATTACHED:
            xEventGroupSetBits(usb_event_group, USB_CONNECTED);
            break;

        case TINYUSB_EVENT_DETACHED:
            xEventGroupClearBits(usb_event_group, USB_CONNECTED);
            ptp_input_mode = 0x00;
            break;

        case TINYUSB_EVENT_SUSPENDED:
            break;

        case TINYUSB_EVENT_RESUMED:
            break;

        default:
            break;
    }
}

void usbhid_init(void) {
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.device = &desc_device;
    tusb_cfg.descriptor.full_speed_config = desc_configuration;
    tusb_cfg.descriptor.string = string_desc;
    tusb_cfg.event_cb = tinyusb_event_cb;
    tusb_cfg.descriptor.string_count = sizeof(string_desc)/sizeof(string_desc[0]);

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
}

void usbhid_task(void *arg) {
    tp_multi_msg_t tp_msg;
    mouse_msg_t mouse_msg;

    while (1) {
        QueueSetMemberHandle_t xActivatedMember = xQueueSelectFromSet(main_queue_set, portMAX_DELAY);

        if (xActivatedMember == mouse_queue) {
            if (xQueueReceive(mouse_queue, &mouse_msg, 0)) {
                mouse_hid_report_t report = {0};

                parse_mouse_report(&mouse_msg, &report);

                if (tud_hid_n_ready(2)) {
                    tud_hid_n_report(2, REPORTID_MOUSE, &report, sizeof(report));
                }
            }
        }
        else if (xActivatedMember == tp_queue) {
            if (xQueueReceive(tp_queue, &tp_msg, 0)) {
                if (current_tp_mode == MOUSE_MODE) {
                    #if CONFIG_PTP_SIMULATED_MOUSE_MODE
                        mouse_hid_report_t report = {0};

                        parse_ptp_simulated_mouse_report(&tp_msg, &report);

                        if (tud_hid_n_ready(2)) {
                            tud_hid_n_report(2, REPORTID_MOUSE, &report, sizeof(report));
                        }

                        if (ptp_simulated_mouse_click_needs_release()) {
                            mouse_hid_report_t release_report = {0};

                            if (tud_hid_n_ready(2)) {
                                tud_hid_n_report(2, REPORTID_MOUSE, &release_report, sizeof(release_report));
                            }
                        }

                    #endif
                } else {
                    ptp_report_t report = {0};

                    parse_ptp_report(&tp_msg, &report);

                    if (tud_hid_n_ready(1)) {
                        tud_hid_n_report(1, REPORTID_TOUCHPAD, &report, sizeof(report));
                    }
                }
            }
        }
    }
}
