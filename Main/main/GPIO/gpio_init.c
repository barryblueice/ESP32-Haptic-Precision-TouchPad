#include "driver/gpio.h"

#include "esp_log.h"
#include "esp_system.h"

#define TAG "GPIO_INIT"

void gpio_init(void) {
    
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = (1ULL << GPIO_NUM_14) | (1ULL << GPIO_NUM_5) | (1ULL << GPIO_NUM_6) | (1ULL << GPIO_NUM_7),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);

    gpio_set_level(GPIO_NUM_14, 0);
    gpio_set_level(GPIO_NUM_5, 0);
    gpio_set_level(GPIO_NUM_6, 0);
    gpio_set_level(GPIO_NUM_7, 0);

}