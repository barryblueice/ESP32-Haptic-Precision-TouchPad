#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "I2C/SUB_DEV/sub_dev.h"

#include "GPIO/GPIO_handle.h"

#define TAG "IP5X09"

v_cap_map_t discharge_table[] = {
    {4.15, 100}, {4.05, 90}, {3.95, 80}, {3.87, 70}, {3.82, 60},
    {3.78, 50}, {3.74, 40}, {3.68, 30}, {3.60, 20}, {3.45, 10}, {3.20, 0}
};

uint8_t ip5x09_read_reg(uint8_t reg_addr) {
    uint8_t data = 0;
    ESP_ERROR_CHECK(i2c_master_transmit_receive(sub_dev_ip5x09_handle, &reg_addr, 1, &data, 1, -1));
    return data;
}

float ip5x09_get_battery_voltage() {
    uint8_t v_low = ip5x09_read_reg(0xA2);
    uint8_t v_high = ip5x09_read_reg(0xA3);

    uint32_t adc_val = (uint32_t)(v_high & 0x3F) << 8 | v_low;
    
    float voltage_mv = 2600.0f + (float)adc_val * 0.26855f;
    return voltage_mv / 1000.0f;
}

float get_battery_ocv() {
    uint8_t low = ip5x09_read_reg(0xA8);
    uint8_t high = ip5x09_read_reg(0xA9);
    uint32_t adc_val = ((uint32_t)(high & 0x3F) << 8) | low;
    return (2600.0f + (float)adc_val * 0.26855f) / 1000.0f;
}

float get_battery_current() {
    uint8_t low = ip5x09_read_reg(0xA4);
    uint8_t high = ip5x09_read_reg(0xA5);
    
    if ((high & 0x20) == 0x20) {
        int32_t c = (((~(high & 0x1F)) & 0x1F) << 8) + (~low) + 1;
        return -c * 0.745985f;
    } else { 
        uint32_t c = ((uint32_t)high << 8) | low;
        return c * 0.745985f;
    }
}

int get_battery_percentage() {
    uint8_t reg0x71 = ip5x09_read_reg(0x71);
    if (((reg0x71 >> 5) & 0x07) == 0x05) return 100;

    float ocv = get_battery_ocv(); 

    if (ocv >= discharge_table[0].voltage) return 100;
    if (ocv <= discharge_table[10].voltage) return 0;

    for (int i = 0; i < 10; i++) {
        if (ocv <= discharge_table[i].voltage && ocv > discharge_table[i+1].voltage) {
            float v_gap = discharge_table[i].voltage - discharge_table[i+1].voltage;
            float p_gap = discharge_table[i].percentage - discharge_table[i+1].percentage;
            return discharge_table[i+1].percentage + (int)((ocv - discharge_table[i+1].voltage) / v_gap * p_gap);
        }
    }
    return 0;
}

void charging_state_monitor_task(void *pvParameters) {

    static uint8_t ip5x09_last_reg_state = 9;

    while (1) {

        uint8_t vbus_det_level = gpio_get_level(VBUS_DET_GPIO);

        if (vbus_det_level == 1) {

            gpio_set_level(GPIO_LED_1, LED_OFF);
            
            uint8_t raw_data = ip5x09_read_reg(0x71);
            
            ip5x09_status_reg_t status;
            status.val = raw_data;
            
            if (ip5x09_last_reg_state != status.reg.state) {

                switch (status.reg.state) {
                    case IP5X09_STATE_FULL:
                        led_send_command(GPIO_LED_2, LED_CMD_STOP, 100, 1000, 0, false);
                        gpio_set_level(GPIO_LED_2, LED_ON);
                        break;
                    default:
                        led_send_command(GPIO_LED_2, LED_CMD_BLINK, 500, 500, 0, true);
                        break;
                }

                ip5x09_last_reg_state = status.reg.state;

            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));


        // const char* state_name = "UNKNOWN";
        // switch (status.reg.state) {
        //     case IP5X09_STATE_IDLE:             state_name = "Idle"; break;
        //     case IP5X09_STATE_TRICKLE:          state_name = "Trickle Charge"; break;
        //     case IP5X09_STATE_CONSTANT_CURRENT: state_name = "CC Charge"; break;
        //     case IP5X09_STATE_CONSTANT_VOLTAGE: state_name = "CV Charge"; break;
        //     case IP5X09_STATE_CV_STOP_DETECT:   state_name = "CV Stop Detect"; break;
        //     case IP5X09_STATE_FULL:             state_name = "Full"; break;
        //     case IP5X09_STATE_TIMEOUT:          state_name = "Timeout Error"; break;
        //     default:                          state_name = "Reserved"; break;
        // }
        // ESP_LOGI(TAG, "--- IP5209 Status [0x%02X] ---", raw_data);
        // ESP_LOGI(TAG, "State: %s", state_name);
        // ESP_LOGI(TAG, "Chg End: %s", status.reg.chg_end_flag ? "YES" : "NO");
        
        // if (status.reg.chg_timeout || status.reg.cv_timeout) {
        //     ESP_LOGW(TAG, "Warning: Charge Timeout! (CV:%d, Total:%d)", 
        //              status.reg.cv_timeout, status.reg.chg_timeout);
        // }
    }
}