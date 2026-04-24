#include <inttypes.h>
#include <stdio.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "GPIO/GPIO_handle.h"
#include "I2C/TP/i2c_hid.h"
#include "I2C/SUB_DEV/mcu-drivers/cs40l25/bsp/bsp_dut.h"
#include "SYS/hid_msg.h"
#include "SYS/rtos_queue.h"

#define TAG "SurfaceHapticBringup"

#define HAPTIC_TRIGGER_DURATION_MS 50
#define HAPTIC_TRIGGER_GAP_MS 350
#define HAPTIC_BOOT_SETTLE_MS 100

static bool step_ok(const char *step, uint32_t ret)
{
    if (ret != BSP_STATUS_OK)
    {
        ESP_LOGE(TAG, "%s failed: 0x%08" PRIX32, step, ret);
        bsp_dut_dump_diagnostics();
        return false;
    }

    ESP_LOGI(TAG, "%s ok", step);
    return true;
}

static void pump_driver_events(uint32_t duration_ms)
{
    const uint32_t slices = duration_ms / 10U;

    for (uint32_t i = 0; i < slices; i++)
    {
        (void)bsp_dut_process();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void trigger_waveform(uint8_t waveform, uint32_t duration_ms)
{
    ESP_LOGW(TAG, "trigger waveform=%" PRIu8 " duration_ms=%" PRIu32, waveform, duration_ms);

    uint32_t ret = bsp_dut_trigger_haptic(waveform, duration_ms);
    if (ret != BSP_STATUS_OK)
    {
        ESP_LOGE(TAG, "trigger waveform=%" PRIu8 " failed: 0x%08" PRIX32, waveform, ret);
        bsp_dut_dump_trigger_diagnostics(waveform, duration_ms);
    }

    pump_driver_events(HAPTIC_TRIGGER_GAP_MS);

    bool has_processed = false;
    if (bsp_dut_has_processed(&has_processed) == BSP_STATUS_OK)
    {
        ESP_LOGI(TAG, "heartbeat advanced after waveform=%" PRIu8 ": %s",
                 waveform,
                 has_processed ? "yes" : "no");
    }
}

static void run_known_good_sequence(void)
{
    const uint8_t config_index = 0;

    ESP_LOGW(TAG, "apply haptic config index=%" PRIu8, config_index);

    if (!step_ok("update_haptic_config", bsp_dut_update_haptic_config(config_index)))
    {
        return;
    }

    (void)step_ok("enable_haptic_processing", bsp_dut_enable_haptic_processing(true));
    bsp_dut_dump_diagnostics();

    for (uint8_t waveform = 1; waveform <= 4; waveform++)
    {
        trigger_waveform(waveform, HAPTIC_TRIGGER_DURATION_MS);
    }
}

static void haptic_bringup_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_LOGW(TAG, "temporary Surface CS40L25 bring-up app started");
    ESP_LOGW(TAG, "main.c is intentionally reduced to haptic firmware validation only");

    if (!step_ok("bsp_dut_initialize", bsp_dut_initialize()))
    {
        goto idle;
    }

    (void)step_ok("bsp_dut_enable_vamp(true)", bsp_dut_enable_vamp(true));

    if (!step_ok("bsp_dut_reset", bsp_dut_reset()))
    {
        goto idle;
    }

    ESP_LOGW(TAG, "try ROM/BHM power-on buzz before RAM firmware boot");
    (void)bsp_dut_trigger_haptic(BSP_DUT_TRIGGER_HAPTIC_POWER_ON, 0);
    pump_driver_events(500);

    if (!step_ok("bsp_dut_boot(false)", bsp_dut_boot(false)))
    {
        goto idle;
    }

    vTaskDelay(pdMS_TO_TICKS(HAPTIC_BOOT_SETTLE_MS));
    ESP_LOGW(TAG, "diagnostics after RAM firmware boot, before power_up");
    bsp_dut_dump_diagnostics();

    if (!step_ok("bsp_dut_power_up", bsp_dut_power_up()))
    {
        goto idle;
    }

    pump_driver_events(100);
    ESP_LOGW(TAG, "diagnostics after power_up");
    bsp_dut_dump_diagnostics();

    run_known_good_sequence();

    ESP_LOGW(TAG, "repeat short loop on waveforms 1..4");
    while (1)
    {
        for (uint8_t waveform = 1; waveform <= 4; waveform++)
        {
            trigger_waveform(waveform, HAPTIC_TRIGGER_DURATION_MS);
        }
    }

idle:
    ESP_LOGE(TAG, "bring-up stopped; leaving task alive for log inspection");
    while (1)
    {
        bsp_dut_dump_diagnostics();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void create_minimal_queues(void)
{
    tp_queue = xQueueCreate(1, sizeof(tp_multi_msg_t));
    mouse_queue = xQueueCreate(1, sizeof(mouse_msg_t));
    tp_data_queue = xQueueCreate(1, 64);
    main_queue_set = xQueueCreateSet(2);

    if ((tp_queue != NULL) && (main_queue_set != NULL))
    {
        xQueueAddToSet(tp_queue, main_queue_set);
    }

    if ((mouse_queue != NULL) && (main_queue_set != NULL))
    {
        xQueueAddToSet(mouse_queue, main_queue_set);
    }
}

void app_main(void)
{
    ESP_LOGW(TAG, "reset reason=%d", esp_reset_reason());

    create_minimal_queues();
    gpio_init();
    touchpad_init();

    uint8_t probe[4] = {0};
    esp_err_t probe_ret = i2c_master_receive(dev_haptic_motor_handle, probe, sizeof(probe), 100);
    ESP_LOGW(TAG, "raw CS40L25 probe ret=%d bytes=%02X %02X %02X %02X",
             probe_ret,
             probe[0],
             probe[1],
             probe[2],
             probe[3]);

    xTaskCreatePinnedToCore(haptic_bringup_task, "haptic_bringup", 8192, NULL, 8, NULL, 1);
}
