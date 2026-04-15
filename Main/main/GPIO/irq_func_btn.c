#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h" // 软件定时器
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"

#define BOOT_BUTTON_GPIO    0
#define REBOOT_TIME_MS      3000

static const char *TAG = "IRQ_BUTTON";
TimerHandle_t reboot_timer;

void reboot_timer_callback(TimerHandle_t xTimer) {
    // ESP_LOGW(TAG, "Button held for 3s! Restarting to Download Mode...");
    // esp_restart();
}

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    int level = gpio_get_level(BOOT_BUTTON_GPIO);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (level == 0) {
        xTimerStartFromISR(reboot_timer, &xHigherPriorityTaskWoken);
    } else {
        xTimerStopFromISR(reboot_timer, &xHigherPriorityTaskWoken);
    }

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void irq_func_btn_init(void) {
    reboot_timer = xTimerCreate("RebootTimer", pdMS_TO_TICKS(REBOOT_TIME_MS), pdFALSE, (void*)0, reboot_timer_callback);

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BOOT_BUTTON_GPIO, gpio_isr_handler, (void*) BOOT_BUTTON_GPIO);

    ESP_LOGI(TAG, "Interrupt system initialized.");
}