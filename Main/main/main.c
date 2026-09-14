#include <stdio.h>
#include "sdkconfig.h"
#include <string.h>
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"

#include "I2C/TP/i2c_hid.h"
#include "I2C/SUB_DEV/cs40l25_surface.h"
#include "I2C/SUB_DEV/sub_dev.h"
#include "I2C/I2C_handle.h"

#include "SYS/rtos_queue.h"
#include "SYS/hid_msg.h"
#include "SYS/input_pipeline.h"
#include "SYS/device_config.h"

#include "USB/usbhid.h"

#include "GPIO/GPIO_handle.h"

#include "NVS/nvs_handle.h"

#include "WIFI/wireless_wifi.h"

#include "BLE/ble_bluedroid.h"
#include "BLE/ble_hid_dev.h"

#define TAG "SurfaceTouch"

#if CONFIG_SURFACE_HAPTIC_TEST_MODE
void surface_haptic_test_start(void);
void app_main(void) { surface_haptic_test_start(); }
#else

void app_main(void) {

    input_pipeline_init();
    led_queue = xQueueCreate(10, sizeof(led_msg_t));
    ESP_ERROR_CHECK(led_queue ? ESP_OK : ESP_ERR_NO_MEM);

    gpio_init();

    ESP_ERROR_CHECK(nvs_init());
    ESP_ERROR_CHECK(device_config_init());

    click_thresholds_load_from_nvs();
    ptp_button_press_threshold_load_from_nvs();
    ptp_haptic_click_intensity_load_from_nvs();

    esp_err_t nvs_err = nvs_read_int("current_mode", &current_mode);
    if (nvs_err != ESP_OK || current_mode < WIRED_MODE || current_mode > BLE_MODE) {
        ESP_LOGE(TAG, "Failed to initialize NVS, storing default mode.");
        nvs_write_int("current_mode", WIRED_MODE);
        current_mode = WIRED_MODE;
    } else {
        ESP_LOGI(TAG, "Current mode loaded from NVS: %d", current_mode);
    }

    irq_func_btn_init();
    touchpad_init(); // I2C0 registration and the touchpad's GPIO33 reset precede haptics.
    sub_dev_init();  // Register I2C1 devices before the haptic worker can use MP28167.
    cs40l25_surface_init();
    tp_modern_sleep_init();


    switch (current_mode) {

        case _2_4_MODE:

            led_send_command(GPIO_LED_3, LED_CMD_BLINK, 500, 2000, 2, false);

            ESP_LOGW(TAG, "Starting in 2.4G Mode...");
            wireless_wifi_init();
            ESP_ERROR_CHECK(xTaskCreatePinnedToCore(wifi_send_task, "wifi_send_task", 4096, NULL, 12, NULL, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
            break;

        case BLE_MODE:

            ESP_LOGW(TAG, "Starting in BLE Mode...");
            hidd_le_prepare_gatt_table();
            ble_bluedroid_init();
            ESP_ERROR_CHECK(xTaskCreatePinnedToCore(ble_hid_task, "ble_hid_task", 4096, NULL, 12, NULL, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
            break;

        default:

            led_send_command(GPIO_LED_3, LED_CMD_BLINK, 2000, 2000, 1, false);

            ESP_LOGW(TAG, "Starting in USB Wired Mode...");
            usbhid_init();
            ESP_ERROR_CHECK(xTaskCreatePinnedToCore(usbhid_task, "usbhid_task", 4096, NULL, 13, NULL, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

            break;

    }
    irq_int_init();

}

#endif
