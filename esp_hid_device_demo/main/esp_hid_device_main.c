#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"

#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "esp_hidd.h"
#include "esp_hid_gap.h"

#define REPORTID_MOUSE 0x01

static const char *TAG = "HID_DEV_DEMO";

typedef struct
{
    TaskHandle_t task_hdl;
    esp_hidd_dev_t *hid_dev;
    uint8_t protocol_mode;
    uint8_t *buffer;
} local_param_t;

static local_param_t s_ble_hid_param = {0};

const uint8_t hid_report_descriptor[] = {

    0x05, 0x01,                         // USAGE_PAGE (Generic Desktop)
    0x09, 0x02,                         // USAGE (Mouse)
    0xa1, 0x01,                         // COLLECTION (Application)
    0x85, REPORTID_MOUSE,               // REPORT_ID
    0x09, 0x01,                         // USAGE (Pointer)
    0xa1, 0x00,  

    0x05, 0x09,                         // USAGE_PAGE (Button)
    0x19, 0x01,                         // USAGE_MINIMUM (Button 1)
        0x29, 0x03,                     // USAGE_MAXIMUM (Button 3)
        0x15, 0x00,                     // LOGICAL_MINIMUM (0)
        0x25, 0x01,                     // LOGICAL_MAXIMUM (1)
        0x75, 0x01,                     // REPORT_SIZE (1)
        0x95, 0x03,                     // REPORT_COUNT (3)
        0x81, 0x02,                     // INPUT (Data,Var,Abs)
        0x95, 0x05,                     // REPORT_COUNT (5)
        0x81, 0x03,                     // INPUT (Cnst,Var,Abs)

        0x05, 0x01,                     // USAGE_PAGE (Generic Desktop)
        0x09, 0x30,                     // USAGE (X)
        0x09, 0x31,                     // USAGE (Y)
        0x15, 0x81,                     // LOGICAL_MINIMUM (-127)
        0x25, 0x7f,                     // LOGICAL_MAXIMUM (127)
        0x75, 0x08,                     // REPORT_SIZE (8)
        0x95, 0x02,                     // REPORT_COUNT (2)
        0x81, 0x06,                     // INPUT (Data,Var,Rel)
    0xC0,                               // END_COLLECTION (Physical)
    0xC0,                               // END_COLLECTION (Application)

};

static esp_hid_raw_report_map_t ble_report_maps[] = {
    {
        .data = hid_report_descriptor,
        .len = sizeof(hid_report_descriptor)
    }
};

static esp_hid_device_config_t ble_hid_config = {
    .vendor_id          = 0x0D00,
    .product_id         = 0x072D,
    .version            = 0x0100,
    .device_name        = "R-SODIUM PTP",
    .manufacturer_name  = "R-SODDIUM Technology",
    .serial_number      = "0D00072D0000",
    .report_maps        = ble_report_maps,
    .report_maps_len    = 1
};

void ble_hid_demo_task(void *pvParameters)
{
    while (1) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

void ble_hid_task_start_up(void)
{
    if (s_ble_hid_param.task_hdl) {
        return;
    }
    xTaskCreate(ble_hid_demo_task, "ble_hid_demo_task", 3 * 1024, NULL, configMAX_PRIORITIES - 3,
                &s_ble_hid_param.task_hdl);
}

void ble_hid_task_shut_down(void)
{
    if (s_ble_hid_param.task_hdl) {
        vTaskDelete(s_ble_hid_param.task_hdl);
        s_ble_hid_param.task_hdl = NULL;
    }
}

static void ble_hidd_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;
    static const char *TAG = "HID_DEV_BLE";

    switch (event) {
    case ESP_HIDD_START_EVENT: {
        ESP_LOGI(TAG, "START");
        esp_hid_ble_gap_adv_start();
        break;
    }
    case ESP_HIDD_CONNECT_EVENT: {
        ESP_LOGI(TAG, "CONNECT");
        break;
    }
    case ESP_HIDD_PROTOCOL_MODE_EVENT: {
        ESP_LOGI(TAG, "PROTOCOL MODE[%u]: %s", param->protocol_mode.map_index, param->protocol_mode.protocol_mode ? "REPORT" : "BOOT");
        break;
    }
    case ESP_HIDD_CONTROL_EVENT: {
        ESP_LOGI(TAG, "CONTROL[%u]: %sSUSPEND", param->control.map_index, param->control.control ? "EXIT_" : "");
        if (param->control.control)
        {
            // exit suspend
            ble_hid_task_start_up();
        } else {
            // suspend
            ble_hid_task_shut_down();
        }
    break;
    }
    case ESP_HIDD_OUTPUT_EVENT: {
        ESP_LOGI(TAG, "OUTPUT[%u]: %8s ID: %2u, Len: %d, Data:", param->output.map_index, esp_hid_usage_str(param->output.usage), param->output.report_id, param->output.length);
        ESP_LOG_BUFFER_HEX(TAG, param->output.data, param->output.length);
        break;
    }
    case ESP_HIDD_FEATURE_EVENT: {
        ESP_LOGI(TAG, "FEATURE[%u]: %8s ID: %2u, Len: %d, Data:", param->feature.map_index, esp_hid_usage_str(param->feature.usage), param->feature.report_id, param->feature.length);
        ESP_LOG_BUFFER_HEX(TAG, param->feature.data, param->feature.length);
        break;
    }
    case ESP_HIDD_DISCONNECT_EVENT: {
        ESP_LOGI(TAG, "DISCONNECT: %s", esp_hid_disconnect_reason_str(esp_hidd_dev_transport_get(param->disconnect.dev), param->disconnect.reason));
        ble_hid_task_shut_down();
        esp_hid_ble_gap_adv_start();
        break;
    }
    case ESP_HIDD_STOP_EVENT: {
        ESP_LOGI(TAG, "STOP");
        break;
    }
    default:
        break;
    }
    return;
}

void ble_hid_device_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run();

    nimble_port_freertos_deinit();
}
void ble_store_config_init(void);

void app_main(void)
{
    esp_err_t ret;
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    ESP_LOGI(TAG, "setting hid gap, mode:%d", HID_DEV_MODE);
    ret = esp_hid_gap_init(HID_DEV_MODE);
    ESP_ERROR_CHECK( ret );

    ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_GENERIC, ble_hid_config.device_name);
    ESP_ERROR_CHECK( ret );

    ESP_LOGI(TAG, "setting ble device");
    ESP_ERROR_CHECK(
        esp_hidd_dev_init(&ble_hid_config, ESP_HID_TRANSPORT_BLE, ble_hidd_event_callback, &s_ble_hid_param.hid_dev));

    ble_store_config_init();

    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ret = esp_nimble_enable(ble_hid_device_host_task);
    if (ret) {
        ESP_LOGE(TAG, "esp_nimble_enable failed: %d", ret);
    }
}
