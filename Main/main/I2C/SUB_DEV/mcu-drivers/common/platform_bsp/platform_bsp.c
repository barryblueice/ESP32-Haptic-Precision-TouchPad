#include "platform_bsp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

// static bsp_driver_if_t esp32_bsp_if = {
//     .set_gpio = &your_esp32_gpio_func,
//     .i2c_write = &your_esp32_i2c_write,
// };

bsp_driver_if_t *bsp_driver_if_g = &esp32_bsp_if;

static bsp_driver_if_t esp32_bsp_if =
{
    .set_gpio = &bsp_set_gpio,
    .set_supply = &bsp_set_supply,
    .register_gpio_cb = &bsp_register_gpio_cb,
    .set_timer = &bsp_set_timer,
    .i2c_read_repeated_start = &bsp_i2c_read_repeated_start,
    .i2c_write = &bsp_i2c_write,
    .i2c_db_write = &bsp_i2c_db_write,
    .i2c_reset = &bsp_i2c_reset,
    .enable_irq = &bsp_enable_irq,
};

uint32_t bsp_set_timer(uint32_t duration_ms, bsp_callback_t cb, void *cb_arg) {
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    if(cb) cb(0, cb_arg);
    return 0;
}

void bsp_notification_callback(uint32_t event_flags, void *arg) {
}