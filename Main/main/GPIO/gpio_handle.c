#include "GPIO/gpio_handle.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

void led_all_handle(uint8_t _state) {
    gpio_set_level(GPIO_LED_1, _state);
    gpio_set_level(GPIO_LED_2, _state);
    gpio_set_level(GPIO_LED_3, _state);
}

/* LED commands arrive from app_main, the charging monitor, the FUNC button,
 * BLE callbacks and the timer service task. Context fields and the pin level
 * are only changed under led_lock; timer API calls happen outside it. */
static portMUX_TYPE led_lock = portMUX_INITIALIZER_UNLOCKED;
static led_msg_t led_ctxs[MAX_LED_SUPPORT];
static bool led_used[MAX_LED_SUPPORT];
static bool led_active[MAX_LED_SUPPORT];

static void vLedTimerCallback(TimerHandle_t xTimer) {
    led_msg_t *ctx = (led_msg_t *)pvTimerGetTimerID(xTimer);
    unsigned index = (unsigned)(ctx - led_ctxs);
    TickType_t period = 0;
    bool stop = false;

    taskENTER_CRITICAL(&led_lock);
    if (!led_active[index]) {
        /* A STOP/replace raced with this expiry; never touch the pin. */
        taskEXIT_CRITICAL(&led_lock);
        return;
    }
    ctx->is_on = !ctx->is_on;
    gpio_set_level(ctx->gpio, ctx->is_on ? LED_ON : LED_OFF);
    ctx->current_flips++;

    uint32_t total_flips_target = (ctx->max_counts == 0) ? UINT32_MAX : (ctx->max_counts * 2);

    if (ctx->current_flips >= total_flips_target) {
        /* Every blink group must end in a known OFF state. */
        ctx->is_on = false;
        gpio_set_level(ctx->gpio, LED_OFF);

        if (ctx->repeat) {
            ctx->current_flips = 0;
            period = pdMS_TO_TICKS(ctx->interval_ms_loop);
        } else {
            led_active[index] = false;
            stop = true;
        }
    } else {
        period = pdMS_TO_TICKS(ctx->interval_ms);
    }
    taskEXIT_CRITICAL(&led_lock);

    if (stop) xTimerStop(xTimer, 0);
    else if (period && xTimerGetPeriod(xTimer) != period) xTimerChangePeriod(xTimer, period, 0);
}

/**
 * @brief  Controls LED behavior. Thread-safe and non-blocking.
 * @param  pin               GPIO Number of the LED to control.
 * @param  mode              LED behavior mode (e.g., blink, always on, stop).
 * @param  interval_ms       Blink interval in milliseconds
 * @param  interval_ms_loop  Loop interval in milliseconds
 * @param  counts            Blink count (0 for infinite)
 * @param  loop              Whether to repeat the blinking pattern.
 * @return pdPASS on success, pdFAIL otherwise.
 */
BaseType_t led_send_command(gpio_num_t pin, led_mode_t mode, uint32_t interval_ms, uint32_t interval_ms_loop, uint32_t counts, bool loop) {
    bool blink = mode == LED_CMD_BLINK;
    if (blink && (interval_ms == 0 || (loop && interval_ms_loop == 0))) return pdFAIL;

    int index = -1;
    TimerHandle_t timer = NULL;
    bool was_active = false;

    taskENTER_CRITICAL(&led_lock);
    /* Prefer the slot that already owns this pin, then a free slot. */
    for (int i = 0; i < MAX_LED_SUPPORT; i++) {
        if (led_used[i] && led_ctxs[i].gpio == pin) { index = i; break; }
    }
    if (index < 0) {
        for (int i = 0; i < MAX_LED_SUPPORT; i++) {
            if (!led_used[i]) { index = i; break; }
        }
    }
    if (index < 0) {
        taskEXIT_CRITICAL(&led_lock);
        return pdFAIL;
    }
    led_msg_t *ctx = &led_ctxs[index];
    led_used[index] = true;
    was_active = led_active[index];
    led_active[index] = false;
    ctx->gpio = pin;
    ctx->mode = mode;
    ctx->current_flips = 0;
    ctx->is_on = mode == LED_CMD_ALWAYS;
    if (blink) {
        ctx->interval_ms = interval_ms;
        ctx->interval_ms_loop = interval_ms_loop;
        ctx->max_counts = counts;
        ctx->repeat = loop;
    }
    gpio_set_level(pin, ctx->is_on ? LED_ON : LED_OFF);
    timer = ctx->timer;
    taskEXIT_CRITICAL(&led_lock);

    if (timer && was_active) xTimerStop(timer, 0);
    if (!blink) return pdPASS;

    if (timer == NULL) {
        timer = xTimerCreate("LED_TMR", pdMS_TO_TICKS(interval_ms), pdTRUE, (void *)ctx, vLedTimerCallback);
        if (timer == NULL) return pdFAIL;
        taskENTER_CRITICAL(&led_lock);
        if (ctx->timer == NULL) ctx->timer = timer;
        TimerHandle_t owned = ctx->timer;
        taskEXIT_CRITICAL(&led_lock);
        if (owned != timer) { xTimerDelete(timer, 0); timer = owned; }
    }

    taskENTER_CRITICAL(&led_lock);
    led_active[index] = true;
    taskEXIT_CRITICAL(&led_lock);
    /* xTimerChangePeriod also (re)starts a dormant timer. */
    return xTimerChangePeriod(timer, pdMS_TO_TICKS(interval_ms), 0);
}
