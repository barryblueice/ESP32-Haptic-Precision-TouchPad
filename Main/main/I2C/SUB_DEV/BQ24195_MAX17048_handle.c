#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "I2C/SUB_DEV/sub_dev.h"

#include "GPIO/GPIO_handle.h"

#define TAG "BQ24195_MAX17048"

/* Runtime power-chip polling must never abort the firmware or hang forever on
 * a stuck secondary bus; a failed sample is simply skipped. */
#define SUB_I2C_TIMEOUT_MS 50

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

esp_err_t bq24195_read_reg(uint8_t reg_addr, uint8_t *value)
{
    uint8_t data = 0;
    esp_err_t err = i2c_master_transmit_receive(sub_dev_bq24195_handle, &reg_addr, 1, &data, 1,
                                                SUB_I2C_TIMEOUT_MS);
    if (err == ESP_OK) *value = data;
    return err;
}

esp_err_t bq24195_write_reg(uint8_t reg_addr, uint8_t value)
{
    uint8_t payload[2] = {reg_addr, value};
    return i2c_master_transmit(sub_dev_bq24195_handle, payload, sizeof(payload), SUB_I2C_TIMEOUT_MS);
}

esp_err_t bq24195_enable_charging(void)
{
    uint8_t reg01;
    esp_err_t err = bq24195_read_reg(0x01, &reg01);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BQ24195 REG01 read failed: %s", esp_err_to_name(err));
        return err;
    }
    uint8_t new_reg01 = (reg01 & (uint8_t)~0x30U) | 0x10U | 0x01U;
    err = bq24195_write_reg(0x01, new_reg01);

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

esp_err_t max17048_read_reg16(uint8_t reg_addr, uint16_t *value)
{
    uint8_t data[2] = {0};
    esp_err_t err = i2c_master_transmit_receive(sub_dev_max17048_handle, &reg_addr, 1, data, 2,
                                                SUB_I2C_TIMEOUT_MS);
    if (err == ESP_OK) *value = ((uint16_t)data[0] << 8) | data[1];
    return err;
}

/* Returns a negative value when the gauge cannot be read. */
float max17048_get_battery_voltage(void)
{
    uint16_t raw;
    if (max17048_read_reg16(MAX17048_REG_VCELL, &raw) != ESP_OK) return -1.0f;
    return (float)raw * 78.125e-6f;
}

/* Returns a negative value when the gauge cannot be read. */
float max17048_get_battery_soc(void)
{
    uint16_t raw;
    if (max17048_read_reg16(MAX17048_REG_SOC, &raw) != ESP_OK) return -1.0f;
    return (float)raw / 256.0f;
}

/* 0..100, or -1 when the fuel gauge read failed. */
int get_battery_percentage()
{
    float soc = max17048_get_battery_soc();

    if (soc < 0.0f) {
        return -1;
    }

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
    uint8_t reg00, reg01, reg02, reg03, reg08, reg09_latched, reg09_current;
    esp_err_t err = bq24195_read_reg(0x00, &reg00);
    if (err == ESP_OK) err = bq24195_read_reg(0x01, &reg01);
    if (err == ESP_OK) err = bq24195_read_reg(0x02, &reg02);
    if (err == ESP_OK) err = bq24195_read_reg(0x03, &reg03);
    if (err == ESP_OK) err = bq24195_read_reg(0x08, &reg08);
    if (err == ESP_OK) err = bq24195_read_reg(0x09, &reg09_latched);
    if (err == ESP_OK) err = bq24195_read_reg(0x09, &reg09_current);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BQ24195 register dump failed: %s", esp_err_to_name(err));
        return;
    }

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

typedef enum { LED_STATE_UNKNOWN = 0, LED_STATE_OFF, LED_STATE_ON, LED_STATE_BLINK } led_state_t;

/* Issue LED commands only on change so a running blink keeps its phase and
 * the timer service is not churned every second. */
static void charging_led_set(gpio_num_t pin, led_state_t *current, led_state_t wanted)
{
    if (*current == wanted) return;
    *current = wanted;
    switch (wanted) {
        case LED_STATE_ON:    led_send_command(pin, LED_CMD_ALWAYS, 0, 0, 0, false); break;
        case LED_STATE_BLINK: led_send_command(pin, LED_CMD_BLINK, 500, 500, 0, true); break;
        default:              led_send_command(pin, LED_CMD_STOP, 0, 0, 0, false); break;
    }
}

void charging_state_monitor_task(void *pvParameters)
{
    (void)pvParameters;
    uint8_t last_chg_state = 0xFF;
    uint8_t pending_chg_state = 0xFF;
    uint8_t pending_chg_count = 0;
    uint8_t last_vbus_det_level = 0xFF;
    bool charge_done_latched = false;
    led_state_t led1 = LED_STATE_UNKNOWN, led2 = LED_STATE_UNKNOWN;

    while (1) {
        int level = get_battery_percentage();
        if (level >= 0) battery_percentage = level;
        uint8_t vbus_det_level = gpio_get_level(VBUS_DET_GPIO);

        if (last_vbus_det_level != vbus_det_level) {
            last_chg_state = 0xFF;
            pending_chg_state = 0xFF;
            pending_chg_count = 0;
            charge_done_latched = false;
            last_vbus_det_level = vbus_det_level;
        }

        bq24195_status_reg_t status = {0};
        if (vbus_det_level == 1 && bq24195_read_reg(0x08, &status.val) != ESP_OK) {
            /* Skip this sample; keep the current LED state. */
        } else if (vbus_det_level == 1) {
            uint8_t effective_chg_state = status.reg.chrg_stat;

            if (status.reg.chrg_stat == BQ24195_STATE_CHARGE_DONE ||
                battery_percentage >= 99) {
                charge_done_latched = true;
            } else if (battery_percentage <= 97) {
                charge_done_latched = false;
            }

            if (charge_done_latched) {
                effective_chg_state = BQ24195_STATE_CHARGE_DONE;
            }

            charging_led_set(GPIO_LED_1, &led1, LED_STATE_OFF);

            if (effective_chg_state == last_chg_state) {
                pending_chg_state = 0xFF;
                pending_chg_count = 0;
            } else if (effective_chg_state != pending_chg_state) {
                pending_chg_state = effective_chg_state;
                pending_chg_count = 1;
            } else if (++pending_chg_count >= 2) {
                charging_led_set(GPIO_LED_2, &led2, effective_chg_state == BQ24195_STATE_CHARGE_DONE ?
                                 LED_STATE_ON : LED_STATE_BLINK);
                last_chg_state = effective_chg_state;
                pending_chg_state = 0xFF;
                pending_chg_count = 0;
            }
        } else {
            charging_led_set(GPIO_LED_2, &led2, LED_STATE_OFF);
            charging_led_set(GPIO_LED_1, &led1, battery_percentage > 20 ? LED_STATE_ON : LED_STATE_BLINK);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
