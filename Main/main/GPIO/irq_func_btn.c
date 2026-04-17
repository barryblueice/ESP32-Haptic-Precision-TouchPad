#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "NVS/nvs_handle.h"
#include "SYS/hid_msg.h"

#define BOOT_BUTTON_GPIO    0
#define TAG "BUTTON_PROC"

static TaskHandle_t button_task_handle = NULL;

void button_handler_task(void* arg) {
    while (1) {
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) > 0) {
            
            vTaskDelay(pdMS_TO_TICKS(20)); 
            if (gpio_get_level(BOOT_BUTTON_GPIO) != 0) continue;

            uint64_t start_time = esp_timer_get_time() / 1000;
            bool reset_triggered = false;

            while (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
                uint64_t current_time = esp_timer_get_time() / 1000;
                uint64_t duration = current_time - start_time;

                if (duration >= CONFIG_FUNC_RESET_MS) {
                    ESP_LOGW(TAG, "BTN time out! Reset to default mode..", duration);
                    nvs_write_int("current_mode", WIRED_MODE);
                    reset_triggered = true;
                    esp_restart(); 
                } else if (duration == CONFIG_FUNC_TIMEOUT_MS) {
                    ESP_LOGW(TAG, "BTN Pressed: %d ms. Reaching FUNC_TIMEOUT_MS", duration);
                }

                vTaskDelay(pdMS_TO_TICKS(10));
            }

            if (!reset_triggered) {
                uint64_t final_duration = (esp_timer_get_time() / 1000) - start_time;
                
                if (final_duration >= CONFIG_FUNC_TIMEOUT_MS) {
                    current_mode = (current_mode == BLE_MODE) ? 0 : current_mode + 1;
                    ESP_LOGW(TAG, "BTN Pressed: %d ms. Switching mode %d", final_duration, current_mode);
                    nvs_write_int("current_mode", current_mode);
                    esp_restart();
                }
            }
        }
    }
}

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
        vTaskNotifyGiveFromISR(button_task_handle, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void irq_func_btn_init(void) {
    xTaskCreate(button_handler_task, "btn_task", 4096, NULL, 10, &button_task_handle);

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE, 
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BOOT_BUTTON_GPIO, gpio_isr_handler, NULL);
}