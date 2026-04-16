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
    
    static uint16_t last_raw_x[5] = {0};
    static uint16_t last_raw_y[5] = {0};

    uint8_t tp_packet[64];

    while (1) {

        tp_multi_msg_t tp_msg = {0}; 
        mouse_msg_t mouse_msg = {0};

        if (xQueueReceive(tp_data_queue, tp_packet, portMAX_DELAY) == pdPASS) {
            if (current_tp_mode == PTP_MODE) {

                // printf("Raw Data: ");
                // for(int i=0; i<64; i++) printf("%02x ", tp_packet[i]);
                // printf("\n");

                int active_finger_count = 0;

                for (int id = 0; id < 5; id++) {
                    int offset = 5 + (id * 8);
                    
                    uint16_t rx = tp_packet[offset]     | (tp_packet[offset + 1] << 8);
                    uint16_t ry_raw = tp_packet[offset + 2] | (tp_packet[offset + 3] << 8);
                    uint16_t ry = (ry_raw > 1533) ? 0 : (1533 - ry_raw);
                    uint8_t pressure = tp_packet[offset + 4];

                    if (pressure > 0) {
                        tp_msg.fingers[id].tip_switch = 1;
                        tp_msg.fingers[id].x = rx;
                        tp_msg.fingers[id].y = ry;
                        tp_msg.fingers[id].contact_id = id;
                        tp_msg.fingers[id].confidence = 1;
                        
                        active_finger_count++;
                        
                    } else {
                        tp_msg.fingers[id].tip_switch = 0;
                        last_raw_x[id] = 0;
                        last_raw_y[id] = 0;
                    }
                }

                tp_msg.actual_count = active_finger_count;
                
                xQueueOverwrite(tp_queue, &tp_msg);

            } else {
                mouse_msg.x = (int8_t)tp_packet[4];
                mouse_msg.y = (int8_t)tp_packet[5];
                mouse_msg.buttons = tp_packet[3];

                xQueueOverwrite(mouse_queue, &mouse_msg);
            }
        }
    }
}