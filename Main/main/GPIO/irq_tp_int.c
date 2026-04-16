#include "driver/gpio.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "SYS/rtos_queue.h"

#include "I2C/i2c_hid.h"

#include "I2C/i2c_hid.h"

#define TAG "IRQ_TP_INT"

static TaskHandle_t tp_task_handle = NULL;

void tp_i2c_int_task(void *pvParameters) {
    uint8_t packet[64]; 

    while (1) {
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) {
            if ((i2c_master_receive(dev_handle, packet, 64, 100)) == ESP_OK) {
                // printf("Sending to Queue...\n");
                xQueueSend(tp_data_queue, (void *)packet, portMAX_DELAY);
            }
        }
    }
}

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(tp_task_handle, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void irq_int_init(void) {
    xTaskCreate(tp_i2c_int_task, "tp_task", 4096, NULL, 10, &tp_task_handle);

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,
        .pin_bit_mask = (1ULL << TP_INT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);
    gpio_isr_handler_add(TP_INT_GPIO, gpio_isr_handler, NULL);

    xTaskCreate(i2c_queue_task, "i2c_receive", 4096, NULL, 14, NULL);
}