#include "driver/gpio.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "SYS/rtos_queue.h"

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

            if ((i2c_master_receive(dev_handle, s_tp_packet, sizeof(s_tp_packet), 100)) == ESP_OK) {
                xQueueSend(tp_data_queue, (void *)s_tp_packet, portMAX_DELAY);
            }
        }
    }
}

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    esp_timer_stop(timeout_watchdog_timer);
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
    esp_timer_create(&timer_args, &timeout_watchdog_timer);

    xTaskCreatePinnedToCore(tp_i2c_int_task,
                            "tp_i2c_int_task",
                            TP_I2C_INT_TASK_STACK_SIZE,
                            NULL,
                            11,
                            &tp_task_handle,
                            1);
    tp_modern_sleep_init();

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TP_INT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_NEGEDGE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&io_conf);
    gpio_isr_handler_add(TP_INT_GPIO, gpio_isr_handler, NULL);
}
