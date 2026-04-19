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

#define REPORTID_TOUCHPAD         0x01
#define REPORTID_MOUSE            0x02
#define REPORTID_MAX_COUNT        0x03
#define REPORTID_PTPHQA           0x04
#define REPORTID_FEATURE          0x05
#define REPORTID_FUNCTION_SWITCH  0x06
#define REPORTID_HAPTIC_FEATURE    0x0C

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
    .idProduct          = 0x072A,
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
    }
    return 0;
}

static uint8_t ptp_input_mode = 0x00;

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
    (void)instance;

    uint8_t command = buffer[0];

    if (report_type == HID_REPORT_TYPE_FEATURE && report_id == REPORTID_FEATURE) {
        if (bufsize >= 1) {
            ptp_input_mode = buffer[0];
        }
    }

    if (command == REPORTID_DFU_CMD) {
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
