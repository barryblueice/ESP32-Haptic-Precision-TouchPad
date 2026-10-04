#include "driver/gpio.h"
#include "esp_system.h"
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "SYS/rtos_queue.h"
#include "SYS/input_pipeline.h"
#include "SYS/input_diagnostics.h"
#include "I2C/TP/force_forward.h"

#include "I2C/TP/i2c_hid.h"

#include "I2C/I2C_handle.h"

#define TAG "IRQ_TP_INT"
#define TP_I2C_INT_TASK_STACK_SIZE 6144

static TaskHandle_t tp_task_handle = NULL;
static uint8_t s_tp_packet[64];
static esp_timer_handle_t drain_timer;
static void drain_timer_wake(void *arg)
{
    (void)arg;
    xTaskNotifyGive(tp_task_handle);
}


static void tp_drain_pending(void)
{
    uint32_t retry_us = 1000;
    /* INT is active-low. Notifications can coalesce, and an asserted line
     * produces no new falling edge until all queued reports are read. */
    for (unsigned reads = 0; reads < 8 && gpio_get_level(TP_INT_GPIO) == 0; ++reads) {
        uint32_t generation = input_source_generation();
        uint32_t output_generation = input_generation();
        uint32_t time_ms = (uint32_t)(esp_timer_get_time() / 1000);
        uint32_t read_start = input_diag_now();
        esp_err_t err = i2c_master_receive(dev_handle, s_tp_packet, sizeof(s_tp_packet), 100);
        input_diag_sample(INPUT_DIAG_READ, input_diag_now() - read_start);
        input_diag_read_result(s_tp_packet, err == ESP_OK);
        uint16_t length = err == ESP_OK ?
            (uint16_t)s_tp_packet[0] | ((uint16_t)s_tp_packet[1] << 8) : 0;
        /* Wake haptics before notifying the parser, but never for empty reads. */
        if (length >= 6 && length <= sizeof(s_tp_packet)) tp_modern_sleep_record_activity();
        if (input_capture(s_tp_packet, err == ESP_OK, generation, output_generation, time_ms)) {
            /* The higher-priority parser has been notified before pressure I/O.
             * Reader and forwarder share one task, so their bus calls cannot race. */
            input_frame_t frame = {.generation = generation,
                .output_generation = output_generation, .time_ms = time_ms};
            memcpy(frame.bytes, s_tp_packet, sizeof(frame.bytes));
            uint32_t force_start = input_diag_now();
            force_forward_report(&frame);
            input_diag_sample(INPUT_DIAG_FORCE, input_diag_now() - force_start);
        }
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            break;
        }
        if (length == 0 || length == 2 || length == UINT16_MAX) {
            /* RESET/no-data is not a queued touch report. A held-low INT must
             * not turn an empty controller into back-to-back 64-byte reads.
             * A real falling edge still wakes the task before this retry. */
            retry_us = 10000;
            break;
        }
    }
    if (gpio_get_level(TP_INT_GPIO) == 0) {
        /* Valid batches continue after 1 ms; empty reports retry after 10 ms.
         * The timer callback only notifies and never touches the I2C bus. */
        esp_timer_stop(drain_timer);
        ESP_ERROR_CHECK(esp_timer_start_once(drain_timer, retry_us));
    }
}

void tp_i2c_int_task(void *pvParameters) {
    (void)pvParameters;
    while (1) {
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) tp_drain_pending();
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
    const esp_timer_create_args_t drain_args = {
        .callback = drain_timer_wake, .name = "tp_drain"
    };
    ESP_ERROR_CHECK(esp_timer_create(&drain_args, &drain_timer));
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
    bool pending = gpio_get_level(TP_INT_GPIO) == 0;
    if (pending) xTaskNotifyGive(tp_task_handle);
    ESP_LOGI(TAG, "Touch reader enabled, pending=%u", (unsigned)pending);
}
