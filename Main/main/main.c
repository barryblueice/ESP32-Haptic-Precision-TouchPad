#include <stdio.h>
#include <string.h>
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"

#include "I2C/i2c_hid.h"

#include "SYS/rtos_queue.h"
#include "SYS/hid_msg.h"

#include "USB/usbhid.h"

#include "GPIO/GPIO_handle.h"

#include "NVS/nvs_handle.h"

#include "WIFI/wireless_wifi.h"

#include "BLE/ble_bluedroid.h"
#include "BLE/ble_hid_dev.h"

#define TAG "SurfaceTouch"

void app_main(void) {

    gpio_init();

    irq_func_btn_init();

    touchpad_init();

    nvs_init();

    esp_err_t nvs_err = nvs_read_int("current_mode", &current_mode);
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS, storing default mode.");
        nvs_write_int("current_mode", WIRED_MODE);
        current_mode = WIRED_MODE;
    } else {
        ESP_LOGI(TAG, "Current mode loaded from NVS: %d", current_mode);
    }

    tp_queue = xQueueCreate(1, sizeof(tp_multi_msg_t));
    mouse_queue = xQueueCreate(1, sizeof(mouse_msg_t));
    tp_data_queue = xQueueCreate(1, 64);

    main_queue_set = xQueueCreateSet(1 + 1);
    xQueueAddToSet(mouse_queue, main_queue_set);
    xQueueAddToSet(tp_queue, main_queue_set);

    irq_int_init();

    switch (current_mode) {

        case _2_4_MODE:
            ESP_LOGW(TAG, "Starting in 2.4G Mode...");

            wireless_wifi_init();

            xTaskCreate(wifi_send_task, "esp_now_task", 4096, NULL, 11, NULL);

            break;

        case BLE_PTP_MODE:
            ESP_LOGW(TAG, "Starting in BLE PTP Mode...");
            hidd_le_prepare_gatt_table(true);
            ble_bluedroid_init();
            break;

        case BLE_MOUSE_MODE:
            ESP_LOGW(TAG, "Starting in BLE Mouse Mode...");
            hidd_le_prepare_gatt_table(false);
            ble_bluedroid_init();
            break;

        default:
            ESP_LOGW(TAG, "Starting in USB Wired Mode...");

            usb_event_group = xEventGroupCreate();

            xTaskCreate(usb_mount_task, "mode_sel", 4096, NULL, 11, NULL);

            usbhid_init();

            xTaskCreate(usbhid_task, "hid", 4096, NULL, 12, NULL);

            while (1) {
                tud_task(); 
                vTaskDelay(pdMS_TO_TICKS(1)); 
            }
            break;
            
    }

}