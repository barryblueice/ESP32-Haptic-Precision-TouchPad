#include "GPIO/gpio_handle.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

void led_all_handle(uint8_t _state) {
    gpio_set_level(GPIO_LED_1, _state);
    gpio_set_level(GPIO_LED_2, _state);
    gpio_set_level(GPIO_LED_3, _state);
}

QueueHandle_t led_queue = NULL;

void led_manager_task(void *param) {
    led_msg_t msg;
    while (1) {
        if (xQueueReceive(led_queue, &msg, portMAX_DELAY)) {
            
            if (msg.mode == LED_CMD_STOP) {
                gpio_set_level(msg.gpio, LED_OFF);
                continue;
            }

            bool force_stopped = false;
            do {
                uint32_t total_flips = (msg.max_counts == 0) ? UINT32_MAX : (msg.max_counts * 2);

                for (uint32_t i = 0; i < total_flips; i++) {
                    gpio_set_level(msg.gpio, (i % 2 == 0) ? LED_ON : LED_OFF);

                    led_msg_t next_msg;
                    if (xQueuePeek(led_queue, &next_msg, 0) == pdPASS) {
                        if (next_msg.gpio == msg.gpio) {
                            force_stopped = true;
                            break;
                        }
                    }

                    vTaskDelay(pdMS_TO_TICKS(msg.interval_ms));
                }

                gpio_set_level(msg.gpio, LED_OFF);

                if (force_stopped) break;

                if (msg.repeat) {
                    led_msg_t next_msg;
                    if (xQueuePeek(led_queue, &next_msg, 0) == pdPASS && next_msg.gpio == msg.gpio) {
                        break; 
                    }
                    vTaskDelay(pdMS_TO_TICKS(msg.interval_ms_loop));
                }

            } while (msg.repeat);

            gpio_set_level(msg.gpio, LED_OFF); 
        }
    }
}

/**
 * @brief  Sends a command to the LED manager task to control LED behavior.
 * * @note   This function is non-blocking and will return immediately after sending the command to the queue. The LED
 * Task will process the command asynchronously.
 * * @param  pin          GPIO Number of the LED to control.
 * @param  mode          LED behavior mode (e.g., blink, always on, stop).
 * @param  interval_ms           Blink interval in milliseconds
 * @param  interval_ms_loop       Loop interval in milliseconds
 * @param  counts       Blink count (0 for infinite)
 * @param  loop         Whether to repeat the blinking pattern indefinitely (true) or just for the specified count (false).
 * * @return 
 * - pdPASS:    Task successfully sent the command to the queue.
 * - pdFAIL:    Queue is full, command sending failed.
 */
BaseType_t led_send_command(gpio_num_t pin, led_mode_t mode, uint32_t interval_ms, uint32_t interval_ms_loop, uint32_t counts, bool loop) {
    led_msg_t msg = {
        .gpio = pin,
        .mode = mode,
        .interval_ms = interval_ms,
        .interval_ms_loop = interval_ms_loop,
        .max_counts = counts,
        .repeat = loop
    };
    
    return xQueueSend(led_queue, &msg, 0);
}