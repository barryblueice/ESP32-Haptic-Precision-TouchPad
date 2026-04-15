#include <stdio.h>
#include <string.h>
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "I2C/i2c_hid.h"

#define TAG "SurfaceTouch"

void app_main(void) {

    touchpad_init();


    xTaskCreatePinnedToCore(
        read_touch_task,
        "touch_task",
        4096,
        NULL,
        10,
        NULL,
        1
    );
}