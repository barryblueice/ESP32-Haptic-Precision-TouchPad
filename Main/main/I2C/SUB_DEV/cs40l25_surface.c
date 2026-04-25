#include "I2C/SUB_DEV/cs40l25_surface.h"

#include <inttypes.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "I2C/SUB_DEV/mcu-drivers/common/platform_bsp/platform_bsp.h"
#include "I2C/SUB_DEV/mcu-drivers/cs40l25/bsp/bsp_dut.h"
#include "SYS/hid_msg.h"

#define TAG "CS40L25_SURFACE"

#define HAPTIC_BOOT_SETTLE_MS 100
#define HAPTIC_INIT_DELAY_MS  250
#define HAPTIC_CLICK_WAVEFORM  3
#define HAPTIC_MANUAL_DEFAULT_DURATION_MS 50
#define HAPTIC_DEFAULT_GAP_MS 50
#define HAPTIC_ROM_TEST_SETTLE_MS 500

typedef enum {
    HAPTIC_CMD_CLICK = 0,
    HAPTIC_CMD_MANUAL_TRIGGER,
} haptic_command_type_t;

typedef struct {
    haptic_command_type_t type;
    uint8_t waveform;
    uint8_t intensity;
    uint8_t repeat_count;
    uint16_t retrigger_period_ms;
    uint16_t duration_ms;
} haptic_command_t;

static QueueHandle_t s_haptic_queue = NULL;
static TaskHandle_t s_haptic_task = NULL;
static bool s_haptic_ready = false;

static bool haptic_step_ok(const char *step, uint32_t ret)
{
    if (ret != BSP_STATUS_OK) {
        ESP_LOGE(TAG, "%s failed: 0x%08" PRIX32, step, ret);
        bsp_dut_dump_diagnostics();
        return false;
    }

    ESP_LOGI(TAG, "%s ok", step);
    return true;
}

static void haptic_pump_driver_events(uint32_t duration_ms)
{
    uint32_t slices = duration_ms / 10U;

    if (slices == 0U) {
        slices = 1U;
    }

    for (uint32_t i = 0; i < slices; i++) {
        (void)bsp_dut_process();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void cs40l25_surface_run_rom_test(void)
{
    uint32_t ret;

    ESP_LOGW(TAG, "try ROM/BHM power-on buzz before RAM firmware boot");

    ret = bsp_dut_trigger_haptic(BSP_DUT_TRIGGER_HAPTIC_POWER_ON, 0);
    if (ret == BSP_STATUS_OK) {
        ESP_LOGI(TAG, "ROM/BHM trigger test complete");
    } else {
        ESP_LOGE(TAG, "ROM/BHM trigger test failed: 0x%08" PRIX32, ret);
    }

    haptic_pump_driver_events(HAPTIC_ROM_TEST_SETTLE_MS);
}

static bool cs40l25_surface_bringup(void)
{
    if (!haptic_step_ok("bsp_initialize", bsp_initialize(NULL, NULL))) {
        return false;
    }

    if (!haptic_step_ok("bsp_dut_initialize", bsp_dut_initialize())) {
        return false;
    }

    (void)haptic_step_ok("bsp_dut_enable_vamp(true)", bsp_dut_enable_vamp(true));

    // The current BSP maps the CS40L25 reset line to TP_RESET_GPIO.
    // In the integrated firmware that GPIO belongs to the touchpad, so
    // issuing bsp_dut_reset() here destabilizes touch tracking.
    ESP_LOGW(TAG, "skip bsp_dut_reset in integrated build: reset GPIO is shared with touchpad");

    cs40l25_surface_run_rom_test();

    if (!haptic_step_ok("bsp_dut_boot(false)", bsp_dut_boot(false))) {
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(HAPTIC_BOOT_SETTLE_MS));

    if (!haptic_step_ok("bsp_dut_power_up", bsp_dut_power_up())) {
        return false;
    }

    haptic_pump_driver_events(100);

    if (!haptic_step_ok("bsp_dut_update_haptic_config(0)", bsp_dut_update_haptic_config(0))) {
        return false;
    }

    if (!haptic_step_ok("bsp_dut_enable_haptic_processing(true)", bsp_dut_enable_haptic_processing(true))) {
        return false;
    }

    return true;
}

static void cs40l25_surface_handle_click(void)
{
    uint32_t duration_ms = ptp_haptic_click_duration_ms_from_intensity(ptp_haptic_click_intensity);
    uint32_t ret;

    if (duration_ms == 0U) {
        // ESP_LOGI(TAG, "skip click haptic: intensity=%u", ptp_haptic_click_intensity);
        return;
    }

    if (!s_haptic_ready) {
        ESP_LOGW(TAG, "skip click haptic: CS40L25 not ready");
        return;
    }

    ret = bsp_dut_trigger_haptic(HAPTIC_CLICK_WAVEFORM, duration_ms);
    if (ret != BSP_STATUS_OK) {
        ESP_LOGE(TAG,
                 "click trigger failed: intensity=%u waveform=%u duration_ms=%" PRIu32 " ret=0x%08" PRIX32,
                 ptp_haptic_click_intensity,
                 HAPTIC_CLICK_WAVEFORM,
                 duration_ms,
                 ret);
        bsp_dut_dump_trigger_diagnostics(HAPTIC_CLICK_WAVEFORM, duration_ms);
    }
}

static void cs40l25_surface_handle_manual(const haptic_command_t *cmd)
{
    uint8_t total_triggers;
    uint16_t gap_ms;

    if (!s_haptic_ready || (cmd->waveform == 0U) || (cmd->intensity == 0U)) {
        return;
    }

    total_triggers = (uint8_t)(cmd->repeat_count + 1U);
    gap_ms = (cmd->retrigger_period_ms > 0U) ? cmd->retrigger_period_ms : HAPTIC_DEFAULT_GAP_MS;

    for (uint8_t i = 0; i < total_triggers; i++) {
        uint32_t ret = bsp_dut_trigger_haptic(cmd->waveform, cmd->duration_ms);
        if (ret != BSP_STATUS_OK) {
            ESP_LOGE(TAG,
                     "manual trigger failed: waveform=%u intensity=%u ret=0x%08" PRIX32,
                     cmd->waveform,
                     cmd->intensity,
                     ret);
            bsp_dut_dump_trigger_diagnostics(cmd->waveform, cmd->duration_ms);
            break;
        }

        if ((i + 1U) < total_triggers) {
            haptic_pump_driver_events(gap_ms);
        }
    }
}

static void cs40l25_surface_task(void *arg)
{
    haptic_command_t cmd;

    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(HAPTIC_INIT_DELAY_MS));
    s_haptic_ready = cs40l25_surface_bringup();

    while (1) {
        if (xQueueReceive(s_haptic_queue, &cmd, pdMS_TO_TICKS(10)) == pdPASS) {
            switch (cmd.type) {
                case HAPTIC_CMD_CLICK:
                    cs40l25_surface_handle_click();
                    break;

                case HAPTIC_CMD_MANUAL_TRIGGER:
                    cs40l25_surface_handle_manual(&cmd);
                    break;

                default:
                    break;
            }
        }

        if (s_haptic_ready) {
            (void)bsp_dut_process();
        }
    }
}

void cs40l25_surface_init(void)
{
    if (s_haptic_task != NULL) {
        return;
    }

    s_haptic_queue = xQueueCreate(8, sizeof(haptic_command_t));
    if (s_haptic_queue == NULL) {
        ESP_LOGE(TAG, "failed to create haptic command queue");
        return;
    }

    xTaskCreatePinnedToCore(cs40l25_surface_task, "cs40l25_surface_task", 8192, NULL, 8, &s_haptic_task, 1);
}

void cs40l25_surface_trigger_click(void)
{
    haptic_command_t cmd = {
        .type = HAPTIC_CMD_CLICK,
    };

    if (s_haptic_queue == NULL) {
        return;
    }

    (void)xQueueSendToBack(s_haptic_queue, &cmd, 0);
}

void cs40l25_surface_trigger_manual(uint8_t waveform,
                                    uint8_t intensity,
                                    uint8_t repeat_count,
                                    uint16_t retrigger_period_ms,
                                    uint16_t cutoff_time_ms)
{
    haptic_command_t cmd = {
        .type = HAPTIC_CMD_MANUAL_TRIGGER,
        .waveform = waveform,
        .intensity = intensity,
        .repeat_count = repeat_count,
        .retrigger_period_ms = retrigger_period_ms,
        .duration_ms = HAPTIC_MANUAL_DEFAULT_DURATION_MS,
    };

    if (cutoff_time_ms > 0U && cutoff_time_ms < 1000U) {
        cmd.duration_ms = cutoff_time_ms;
    }

    if (s_haptic_queue == NULL) {
        return;
    }

    (void)xQueueSendToBack(s_haptic_queue, &cmd, 0);
}

bool cs40l25_surface_is_ready(void)
{
    return s_haptic_ready;
}
