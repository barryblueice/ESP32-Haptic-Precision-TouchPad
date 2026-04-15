#include "I2C/i2c_hid.h"
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "I2C_CMD"

void read_touch_task(void *pvParameters)
{
    uint8_t packet[64];
    ESP_LOGI(TAG, "Touch task started on Core 1");

    while (1) {
        if (gpio_get_level(TP_INT_GPIO) == 0) {

            esp_err_t ret = i2c_master_receive(dev_handle, packet, 64, 100);
            
            if (ret == ESP_OK) {
                if (ret == ESP_OK) {
                    printf("Raw Data: ");
                    for(int i=0; i<64; i++) printf("%02x ", packet[i]);
                    printf("\n");
                }
            }

        }
        
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}