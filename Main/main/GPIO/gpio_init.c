#include "driver/gpio.h"

#include "esp_log.h"
#include "esp_system.h"

#include "GPIO/GPIO_handle.h"

#define TAG "GPIO_INIT"

void gpio_init(void) {

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = (1ULL << GPIO_NUM_14) | (1ULL << GPIO_NUM_5) | (1ULL << GPIO_NUM_6) | (1ULL << GPIO_NUM_7),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);

    // led_all_handle(LED_OFF);
    gpio_set_level(GPIO_HAPTIC_BUCK_BOOST_EN, EN_ON);

    gpio_config_t vbus_det_conf = {
        .pin_bit_mask = (1ULL << VBUS_DET_GPIO),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&vbus_det_conf);

}