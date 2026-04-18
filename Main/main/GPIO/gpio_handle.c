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

static led_msg_t led_ctxs[MAX_LED_SUPPORT];

static void vLedTimerCallback(TimerHandle_t xTimer) {
    led_msg_t *ctx = (led_msg_t *)pvTimerGetTimerID(xTimer);
    
    ctx->is_on = !ctx->is_on;
    gpio_set_level(ctx->gpio, ctx->is_on ? LED_ON : LED_OFF);
    ctx->current_flips++;

    uint32_t total_flips_target = (ctx->max_counts == 0) ? UINT32_MAX : (ctx->max_counts * 2);

    if (ctx->current_flips >= total_flips_target) {
        if (ctx->repeat) {
            ctx->current_flips = 0;
            xTimerChangePeriod(xTimer, pdMS_TO_TICKS(ctx->interval_ms_loop), 0);
        } else {
            gpio_set_level(ctx->gpio, LED_OFF);
            xTimerStop(xTimer, 0);
        }
    } else {
        if (xTimerGetPeriod(xTimer) != pdMS_TO_TICKS(ctx->interval_ms)) {
            xTimerChangePeriod(xTimer, pdMS_TO_TICKS(ctx->interval_ms), 0);
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
    led_msg_t *ctx = NULL;

    for(int i=0; i<3; i++) {
        if(led_ctxs[i].gpio == pin || led_ctxs[i].timer == NULL) {
            ctx = &led_ctxs[i];
            break;
        }
    }
    
    if (!ctx) return pdFAIL;

    if (mode == LED_CMD_STOP) {
        if (ctx->timer) xTimerStop(ctx->timer, 0);
        gpio_set_level(pin, LED_OFF);
        return pdPASS;
    }

    ctx->gpio = pin;
    ctx->mode = mode;
    ctx->interval_ms = interval_ms;
    ctx->interval_ms_loop = interval_ms_loop;
    ctx->max_counts = counts;
    ctx->repeat = loop;
    ctx->current_flips = 0;
    ctx->is_on = false;

    if (ctx->timer == NULL) {
        ctx->timer = xTimerCreate("LED_TMR", pdMS_TO_TICKS(interval_ms), pdTRUE, (void *)ctx, vLedTimerCallback);
    } else {
        xTimerChangePeriod(ctx->timer, pdMS_TO_TICKS(interval_ms), 0);
    }

    return xTimerStart(ctx->timer, 0);
}