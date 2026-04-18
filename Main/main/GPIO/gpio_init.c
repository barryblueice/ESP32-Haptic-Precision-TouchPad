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
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);

    led_all_handle(LED_OFF);
    gpio_set_level(GPIO_HAPTIC_BUCK_BOOST_EN, EN_OFF);

    gpio_config_t vbus_det_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = (1ULL << VBUS_DET_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
    };
    gpio_config(&vbus_det_conf);

}