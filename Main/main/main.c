#include <stdio.h>
#include <string.h>
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"

#include "I2C/i2c_hid.h"

#include "SYS/rtos_queue.h"
#include "SYS/hid_msg.h"

#include "USB/usbhid.h"

#include "GPIO/GPIO_handle.h"

#define TAG "SurfaceTouch"

void app_main(void) {

    gpio_set_direction(GPIO_NUM_5, GPIO_MODE_OUTPUT_OD);
    gpio_set_direction(GPIO_NUM_6, GPIO_MODE_OUTPUT_OD);
    gpio_set_direction(GPIO_NUM_7, GPIO_MODE_OUTPUT_OD);

    gpio_set_level(GPIO_NUM_5, 0);
    gpio_set_level(GPIO_NUM_6, 0);
    gpio_set_level(GPIO_NUM_7, 0);

    irq_func_btn_init();

    touchpad_init();
    
    tp_queue = xQueueCreate(1, sizeof(tp_multi_msg_t));
    mouse_queue = xQueueCreate(1, sizeof(mouse_msg_t));

    main_queue_set = xQueueCreateSet(1 + 1);
    xQueueAddToSet(mouse_queue, main_queue_set);
    xQueueAddToSet(tp_queue, main_queue_set);

    usb_event_group = xEventGroupCreate();

    xTaskCreate(usb_mount_task, "mode_sel", 4096, NULL, 11, NULL);

    usbhid_init();

    // xTaskCreate(tp_i2c_task, "i2c_task", 4096, NULL, 10, NULL);

    xTaskCreate(usbhid_task, "hid", 4096, NULL, 12, NULL);

    // xTaskCreatePinnedToCore(read_touch_task, "touch_task", 4096, NULL, 10, NULL, 1);

    while (1) {
        tud_task(); 
        vTaskDelay(pdMS_TO_TICKS(1)); 
    }
}