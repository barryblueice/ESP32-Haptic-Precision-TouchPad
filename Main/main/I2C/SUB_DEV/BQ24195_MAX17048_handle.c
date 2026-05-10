#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "I2C/SUB_DEV/sub_dev.h"

#include "GPIO/GPIO_handle.h"

#define TAG "BQ24195_MAX17048"

static int bq24195_decode_iinlim_ma(uint8_t code)
{
    static const int limits_ma[] = {100, 150, 500, 900, 1200, 1500, 2000, 3000};
    return limits_ma[code & 0x07];
}

static int bq24195_decode_ichg_ma(uint8_t reg02)
{
    return 512 + (((reg02 >> 2) & 0x3F) * 64);
}

static int bq24195_decode_iprechg_ma(uint8_t reg03)
{
    return 128 + (((reg03 >> 4) & 0x0F) * 128);
}

static int bq24195_decode_iterm_ma(uint8_t reg03)
{
    return 128 + ((reg03 & 0x0F) * 128);
}

uint8_t bq24195_read_reg(uint8_t reg_addr)
{
    uint8_t data = 0;
    ESP_ERROR_CHECK(i2c_master_transmit_receive(sub_dev_bq24195_handle, &reg_addr, 1, &data, 1, -1));
    return data;
}

esp_err_t bq24195_write_reg(uint8_t reg_addr, uint8_t value)
{
    uint8_t payload[2] = {reg_addr, value};
    return i2c_master_transmit(sub_dev_bq24195_handle, payload, sizeof(payload), -1);
}

esp_err_t bq24195_enable_charging(void)
{
    uint8_t reg01 = bq24195_read_reg(0x01);
    uint8_t new_reg01 = (reg01 & (uint8_t)~0x30U) | 0x10U | 0x01U;
    esp_err_t err = bq24195_write_reg(0x01, new_reg01);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "BQ24195 REG01: 0x%02X -> 0x%02X (CHG_CONFIG=01)", reg01, new_reg01);
    } else {
        ESP_LOGE(TAG, "BQ24195 REG01 write failed: 0x%x", err);
    }

    return err;
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

void bq24195_dump_charge_config(void)
{
    uint8_t reg00 = bq24195_read_reg(0x00);
    uint8_t reg01 = bq24195_read_reg(0x01);
    uint8_t reg02 = bq24195_read_reg(0x02);
    uint8_t reg03 = bq24195_read_reg(0x03);
    uint8_t reg08 = bq24195_read_reg(0x08);
    uint8_t reg09_latched = bq24195_read_reg(0x09);
    uint8_t reg09_current = bq24195_read_reg(0x09);

    ESP_LOGI(TAG,
             "BQ24195 regs: REG00=0x%02X REG01=0x%02X REG02=0x%02X REG03=0x%02X REG08=0x%02X REG09(latched)=0x%02X REG09(current)=0x%02X",
             reg00,
             reg01,
             reg02,
             reg03,
             reg08,
             reg09_latched,
             reg09_current);

    ESP_LOGI(TAG,
             "BQ24195 preset: EN_HIZ=%u CHG_CONFIG=%u SYS_MIN=%.1f V IINLIM=%d mA ICHG=%d mA IPRECHG=%d mA ITERM=%d mA",
             (reg00 >> 7) & 0x01,
             (reg01 >> 4) & 0x03,
             3.0f + (float)((reg01 >> 1) & 0x07) * 0.1f,
             bq24195_decode_iinlim_ma(reg00),
             bq24195_decode_ichg_ma(reg02),
             bq24195_decode_iprechg_ma(reg03),
             bq24195_decode_iterm_ma(reg03));

    ESP_LOGI(TAG,
             "BQ24195 status: VBUS_STAT=%u CHRG_STAT=%u DPM_STAT=%u PG_STAT=%u",
             (reg08 >> 6) & 0x03,
             (reg08 >> 4) & 0x03,
             (reg08 >> 3) & 0x01,
             (reg08 >> 2) & 0x01);

    ESP_LOGI(TAG,
             "BQ24195 fault: WATCHDOG=%u CHRG_FAULT=%u BAT_FAULT=%u NTC_FAULT=%u",
             (reg09_current >> 7) & 0x01,
             (reg09_current >> 4) & 0x03,
             (reg09_current >> 3) & 0x01,
             reg09_current & 0x07);
}

void charging_state_monitor_task(void *pvParameters)
{
    static uint8_t last_chg_state = 0xFF;
    static uint8_t last_vbus_det_level = 0xFF;

    while (1) {
        battery_percentage = get_battery_percentage();
        uint8_t vbus_det_level = gpio_get_level(VBUS_DET_GPIO);

        if (last_vbus_det_level != vbus_det_level) {
            last_chg_state = 0xFF;
            last_vbus_det_level = vbus_det_level;
        }

        if (vbus_det_level == 1) {
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
