#include "driver/gpio.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "SYS/rtos_queue.h"
#include "SYS/input_pipeline.h"

#include "I2C/TP/i2c_hid.h"

#include "I2C/I2C_handle.h"

#define TAG "IRQ_TP_INT"
#define TP_I2C_INT_TASK_STACK_SIZE 6144

static TaskHandle_t tp_task_handle = NULL;
static uint8_t s_tp_packet[64];

void tp_i2c_int_task(void *pvParameters) {
    while (1) {
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) {
            tp_modern_sleep_record_activity();

            uint32_t generation = input_generation();
            uint32_t time_ms = (uint32_t)(esp_timer_get_time() / 1000);
            esp_err_t err = i2c_master_receive(dev_handle, s_tp_packet, sizeof(s_tp_packet), 100);
            input_capture(s_tp_packet, err == ESP_OK, generation, time_ms);
        }
    }
}

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    tp_modern_sleep_signal_activity_from_isr();
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(tp_task_handle, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void irq_int_init(void) {

    const esp_timer_create_args_t timer_args = {
        .callback = &watchdog_timeout_callback,
        .name = "touch_timeout_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timeout_watchdog_timer));

    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(tp_i2c_int_task,
                            "tp_i2c_int_task",
                            TP_I2C_INT_TASK_STACK_SIZE,
                            NULL,
                            11,
                            &tp_task_handle,
                            1) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    tp_modern_sleep_init();

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TP_INT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(gpio_isr_handler_add(TP_INT_GPIO, gpio_isr_handler, NULL));
    ESP_ERROR_CHECK(gpio_set_intr_type(TP_INT_GPIO, GPIO_INTR_NEGEDGE));
    ESP_ERROR_CHECK(gpio_intr_enable(TP_INT_GPIO));
}
