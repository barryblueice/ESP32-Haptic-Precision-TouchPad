#include "I2C/i2c_hid.h"
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "SYS/rtos_queue.h"
#include "SYS/hid_msg.h"

#define TAG "I2C_QUENE"

void i2c_queue_task(void *arg) {

    uint8_t tp_packet[64];

    while (1) {

        tp_multi_msg_t tp_msg = {0}; 
        mouse_msg_t mouse_msg = {0};

        if (xQueueReceive(tp_data_queue, tp_packet, portMAX_DELAY) == pdPASS) {
            if (current_tp_mode == PTP_MODE) {
                tp_multi_msg_t msg;
            } else {
                mouse_msg.x = (int8_t)tp_packet[4];
                mouse_msg.y = (int8_t)tp_packet[5];
                mouse_msg.buttons = tp_packet[3];

                // printf("Raw Data: ");
                // for(int i=0; i<32; i++) printf("%02x ", tp_packet[i]);
                // printf("\n");

                xQueueOverwrite(mouse_queue, &mouse_msg);
            }
        }
    }
}