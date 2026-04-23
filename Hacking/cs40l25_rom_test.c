#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "GPIO/GPIO_handle.h"
#include "I2C/SUB_DEV/sub_dev.h"
#include "I2C/SUB_DEV/mcu-drivers/common/platform_bsp/platform_bsp.h"
#include "I2C/SUB_DEV/mcu-drivers/cs40l25/bsp/bsp_dut.h"

// Exported from I2C/TP/i2c_cmd.c. We only need the CS40L25 I2C device handle.
void tp_i2c_init(void);
void sub_dev_i2c_init(void);

#define HAPTIC_TRIGGER_INTERVAL_MS 3000

static const char *TAG = "CS40L25_TEST";

static void log_bsp_step(const char *step, uint32_t ret)
{
    if (ret != BSP_STATUS_OK)
    {
        ESP_LOGE(TAG, "%s failed, ret=%" PRIu32, step, ret);
        while (true)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    ESP_LOGI(TAG, "%s ok", step);
}

static void cs40l25_driver_test_init(void)
{
    gpio_init();
    tp_i2c_init();
    sub_dev_i2c_init();

    if (sub_dev_mp28167_handle != NULL)
    {
        ESP_LOGI(TAG, "MP28167 VREF register reads %.2f mV", mp28167_get_vref_mv(sub_dev_mp28167_handle));
    }
    else
    {
        ESP_LOGW(TAG, "MP28167 handle is NULL");
    }

    log_bsp_step("bsp_initialize", bsp_initialize(NULL, NULL));
    log_bsp_step("bsp_dut_initialize", bsp_dut_initialize());
    log_bsp_step("bsp_dut_enable_vamp", bsp_dut_enable_vamp(true));
    ESP_LOGI(TAG, "BUCK_BOOST_EN gpio level=%d", gpio_get_level(GPIO_HAPTIC_BUCK_BOOST_EN));
    log_bsp_step("bsp_dut_reset", bsp_dut_reset());
    vTaskDelay(pdMS_TO_TICKS(20));
}

void app_main(void)
{
    cs40l25_driver_test_init();

    ESP_LOGI(TAG, "ROM mode only: starting periodic BHM power-on pulse trigger");

    while (true)
    {
        uint32_t ret = bsp_dut_trigger_haptic(BSP_DUT_TRIGGER_HAPTIC_POWER_ON, 0);

        if (ret == BSP_STATUS_OK)
        {
            ESP_LOGI(TAG, "BHM trigger sent");
        }
        else
        {
            ESP_LOGE(TAG, "BHM trigger failed, ret=%" PRIu32, ret);
        }

        vTaskDelay(pdMS_TO_TICKS(HAPTIC_TRIGGER_INTERVAL_MS));
    }
}
