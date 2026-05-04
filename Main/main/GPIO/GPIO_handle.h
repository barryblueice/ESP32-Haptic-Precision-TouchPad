#ifndef GPIO_HANDLE_H
#define GPIO_HANDLE_H

#define LED_ON 0
#define LED_OFF 1

#define EN_ON 1
#define EN_OFF 0

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define MAX_LED_SUPPORT 3

#define VBUS_DET_GPIO GPIO_NUM_9
#define BOOT_BUTTON_GPIO GPIO_NUM_0
#define GPIO_LED_1 GPIO_NUM_5 // LED1: Blinking if battery is low. Keep solid on wehn USB is disconnected. Turn dark when charging.
#define GPIO_LED_2 GPIO_NUM_6 // LED2: Charging when blink. Keep solid on when fully charged. Turn dark when USB is disconnected.
#define GPIO_LED_3 GPIO_NUM_7 // LED3: Function LED.
#define GPIO_HAPTIC_BUCK_BOOST_EN GPIO_NUM_14
#define GPIO_HAPTIC_FUNC_FOR_TP_EN GPIO_NUM_38
#define GPIO_HAPTIC_ALERT_N GPIO_NUM_37

typedef enum {
    LED_CMD_BLINK,
    LED_CMD_STOP,
    LED_CMD_ALWAYS
} led_mode_t;

typedef struct {
    gpio_num_t gpio;
    led_mode_t mode;
    uint32_t interval_ms;
    uint32_t interval_ms_loop;
    uint32_t max_counts;
    bool repeat;

    TimerHandle_t timer;
    uint32_t current_flips;
    bool is_on;

} led_msg_t;

void irq_func_btn_init(void);
void irq_int_init(void);
void gpio_init(void);

void led_all_handle(uint8_t _state);
BaseType_t led_send_command(gpio_num_t pin, led_mode_t mode, uint32_t interval_ms, uint32_t interval_ms_loop, uint32_t counts, bool loop);

extern QueueHandle_t led_queue;

#endif // GPIO_HANDLE_H
