#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "I2C/SUB_DEV/sub_dev.h"

#include "GPIO/GPIO_handle.h"

#define TAG "BQ24195_MAX17048"

uint8_t bq24195_read_reg(uint8_t reg_addr)
{
    uint8_t data = 0;
    ESP_ERROR_CHECK(i2c_master_transmit_receive(sub_dev_bq24195_handle, &reg_addr, 1, &data, 1, -1));
    return data;
}

float bq24195_get_battery_voltage(void)
{
    return max17048_get_battery_voltage();
}

uint16_t max17048_read_reg16(uint8_t reg_addr)
{
    uint8_t data[2] = {0};
    ESP_ERROR_CHECK(i2c_master_transmit_receive(sub_dev_max17048_handle, &reg_addr, 1, data, 2, -1));
    return ((uint16_t)data[0] << 8) | data[1];
}

float max17048_get_battery_voltage(void)
{
    uint16_t raw = max17048_read_reg16(MAX17048_REG_VCELL);
    return (float)raw * 78.125e-6f;
}

float max17048_get_battery_soc(void)
{
    uint16_t raw = max17048_read_reg16(MAX17048_REG_SOC);
    return (float)raw / 256.0f;
}

int get_battery_percentage()
{
    float soc = max17048_get_battery_soc();

    if (soc <= 0.0f) {
        return 0;
    }

    if (soc >= 100.0f) {
        return 100;
    }

    return (int)(soc + 0.5f);
}

void charging_state_monitor_task(void *pvParameters)
{
    static uint8_t last_chg_state = 0xFF;

    while (1) {
        battery_percentage = get_battery_percentage();

        if (gpio_get_level(VBUS_DET_GPIO) == 1) {
            bq24195_status_reg_t status = {
                .val = bq24195_read_reg(0x08),
            };

            gpio_set_level(GPIO_LED_1, LED_OFF);
            led_send_command(GPIO_LED_1, LED_CMD_STOP, 1, 1, 0, false);

            if (last_chg_state != status.reg.chrg_stat) {
                switch (status.reg.chrg_stat) {
                    case BQ24195_STATE_CHARGE_DONE:
                        led_send_command(GPIO_LED_2, LED_CMD_STOP, 1, 1, 0, false);
                        gpio_set_level(GPIO_LED_2, LED_ON);
                        break;
                    case BQ24195_STATE_PRE_CHARGE:
                    case BQ24195_STATE_FAST_CHARGE:
                    default:
                        gpio_set_level(GPIO_LED_2, LED_OFF);
                        led_send_command(GPIO_LED_2, LED_CMD_STOP, 1, 1, 0, false);
                        led_send_command(GPIO_LED_2, LED_CMD_BLINK, 500, 500, 0, true);
                        break;
                }

                last_chg_state = status.reg.chrg_stat;
            }
        } else {
            gpio_set_level(GPIO_LED_2, LED_OFF);
            led_send_command(GPIO_LED_2, LED_CMD_STOP, 1, 1, 0, false);

            if (battery_percentage > 20) {
                led_send_command(GPIO_LED_1, LED_CMD_STOP, 1, 1, 0, false);
                gpio_set_level(GPIO_LED_1, LED_ON);
            } else {
                gpio_set_level(GPIO_LED_1, LED_OFF);
                led_send_command(GPIO_LED_1, LED_CMD_STOP, 1, 1, 0, false);
                led_send_command(GPIO_LED_1, LED_CMD_BLINK, 500, 500, 0, true);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
