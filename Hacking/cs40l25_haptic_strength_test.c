#include <inttypes.h>
#include <stdio.h>

#include "esp_attr.h"
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

#define TAG "SurfaceHapticScan"

#define HAPTIC_BOOT_SETTLE_MS       100
#define HAPTIC_SCAN_GAP_MS          1800
#define SURFACE_VIBEGEN_WAVE_COUNT  28
#define HAPTIC_TEST_WAVEFORM        3
#define SURFACE_CLICK_PRESS_WAVE    3
#define SURFACE_CLICK_RELEASE_WAVE  4
#define HAPTIC_PROFILE_SETTLE_MS    2000
#define HAPTIC_LOOP_COOLDOWN_MS     6000
#define HAPTIC_STARTUP_IDLE_MS      3000

typedef struct {
    const char *name;
    uint32_t duration_ms;
} trigger_mode_t;

static RTC_DATA_ATTR uint32_t s_boot_count = 0;

static const char *reset_reason_to_string(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_UNKNOWN:
            return "unknown";
        case ESP_RST_POWERON:
            return "power_on";
        case ESP_RST_EXT:
            return "external_pin";
        case ESP_RST_SW:
            return "software";
        case ESP_RST_PANIC:
            return "panic";
        case ESP_RST_INT_WDT:
            return "interrupt_watchdog";
        case ESP_RST_TASK_WDT:
            return "task_watchdog";
        case ESP_RST_WDT:
            return "other_watchdog";
        case ESP_RST_DEEPSLEEP:
            return "deep_sleep_wakeup";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_SDIO:
            return "sdio";
        case ESP_RST_USB:
            return "usb";
        case ESP_RST_JTAG:
            return "jtag";
        case ESP_RST_EFUSE:
            return "efuse";
        case ESP_RST_PWR_GLITCH:
            return "power_glitch";
        case ESP_RST_CPU_LOCKUP:
            return "cpu_lockup";
        default:
            return "unmapped";
    }
}

static bool step_ok(const char *step, uint32_t ret)
{
    if (ret != BSP_STATUS_OK) {
        ESP_LOGE(TAG, "%s failed: 0x%08" PRIX32, step, ret);
        bsp_dut_dump_diagnostics();
        return false;
    }

    ESP_LOGI(TAG, "%s ok", step);
    return true;
}

static void pump_driver_events(uint32_t duration_ms)
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

static void log_heartbeat_progress(const char *path_name)
{
    bool has_processed = false;

    if (bsp_dut_has_processed(&has_processed) == BSP_STATUS_OK) {
        ESP_LOGI(TAG,
                 "%s heartbeat_advanced=%s",
                 path_name,
                 has_processed ? "yes" : "no");
    }
}

static void trigger_waveform_with_name(uint8_t waveform, uint32_t duration_ms, const char *label)
{
    uint32_t ret;

    ESP_LOGW(TAG,
             "trigger waveform=%02u mode=%s duration_ms=%" PRIu32,
             waveform,
             label,
             duration_ms);

    ret = bsp_dut_trigger_haptic(waveform, duration_ms);
    if (ret != BSP_STATUS_OK) {
        ESP_LOGE(TAG, "trigger waveform=%02u failed: 0x%08" PRIX32, waveform, ret);
        bsp_dut_dump_trigger_diagnostics(waveform, duration_ms);
    }

    pump_driver_events(HAPTIC_SCAN_GAP_MS);
    log_heartbeat_progress(label);
}

static void trigger_waveform(uint8_t waveform, const trigger_mode_t *mode)
{
    trigger_waveform_with_name(waveform, mode->duration_ms, mode->name);
}

static void compare_single_waveform_trigger_modes(void)
{
    static const trigger_mode_t modes[] = {
        {
            .name = "mbox1_full_wave",
            .duration_ms = 0,
        },
        {
            .name = "mbox2_cutoff_20ms",
            .duration_ms = 20,
        },
    };

    /*
     * Isolate the trigger-path variable by keeping the waveform fixed. This makes
     * it easier to tell whether feel changes come from MBOX1 vs MBOX2 timeout
     * handling rather than from the waveform content itself.
     */
    ESP_LOGW(TAG, "compare trigger modes on single waveform=%u", HAPTIC_TEST_WAVEFORM);

    for (size_t mode = 0; mode < (sizeof(modes) / sizeof(modes[0])); mode++) {
        trigger_waveform(HAPTIC_TEST_WAVEFORM, &modes[mode]);
    }
}

static void compare_mbox2_cutoff_sweep(void)
{
    static const trigger_mode_t modes[] = {
        {
            .name = "mbox2_cutoff_48ms",
            .duration_ms = 48,
        },
        {
            .name = "mbox2_cutoff_64ms",
            .duration_ms = 64,
        },
        {
            .name = "mbox2_cutoff_80ms",
            .duration_ms = 80,
        },
        {
            .name = "mbox2_cutoff_96ms",
            .duration_ms = 96,
        },
    };

    ESP_LOGW(TAG,
             "compare aggressive single cutoff sweep on single waveform=%u",
             SURFACE_CLICK_PRESS_WAVE);

    for (size_t i = 0; i < (sizeof(modes) / sizeof(modes[0])); i++) {
        ESP_LOGW(TAG,
                 "cutoff case=%s settle %" PRIu32 " ms before trigger",
                 modes[i].name,
                 (uint32_t)HAPTIC_PROFILE_SETTLE_MS);
        pump_driver_events(HAPTIC_PROFILE_SETTLE_MS);
        trigger_waveform(SURFACE_CLICK_PRESS_WAVE, &modes[i]);
    }
}

static void haptic_scan_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_LOGW(TAG, "CS40L25 Surface waveform scan app started");
    ESP_LOGW(TAG,
             "single-waveform isolation mode: waveform=%u of %u Surface VIBEGEN waves",
             HAPTIC_TEST_WAVEFORM,
             SURFACE_VIBEGEN_WAVE_COUNT);

    if (!step_ok("bsp_initialize path", bsp_dut_initialize())) {
        goto idle;
    }

    (void)step_ok("bsp_dut_enable_vamp(true)", bsp_dut_enable_vamp(true));

    if (!step_ok("bsp_dut_reset", bsp_dut_reset())) {
        goto idle;
    }

    ESP_LOGW(TAG, "try ROM/BHM power-on buzz before RAM firmware boot");
    (void)bsp_dut_trigger_haptic(BSP_DUT_TRIGGER_HAPTIC_POWER_ON, 0);
    pump_driver_events(500);

    if (!step_ok("bsp_dut_boot(false)", bsp_dut_boot(false))) {
        goto idle;
    }

    vTaskDelay(pdMS_TO_TICKS(HAPTIC_BOOT_SETTLE_MS));
    bsp_dut_dump_diagnostics();

    if (!step_ok("bsp_dut_power_up", bsp_dut_power_up())) {
        goto idle;
    }

    pump_driver_events(100);
    bsp_dut_dump_diagnostics();

    if (!step_ok("update_haptic_config(0)", bsp_dut_update_haptic_config(0))) {
        goto idle;
    }

    if (!step_ok("enable_haptic_processing(true)", bsp_dut_enable_haptic_processing(true))) {
        goto idle;
    }

    ESP_LOGW(TAG, "startup idle %" PRIu32 " ms before first haptic trigger", (uint32_t)HAPTIC_STARTUP_IDLE_MS);
    pump_driver_events(HAPTIC_STARTUP_IDLE_MS);

    compare_mbox2_cutoff_sweep();

    ESP_LOGW(TAG, "repeat single cutoff comparison loop");
    while (1) {
        compare_mbox2_cutoff_sweep();
        ESP_LOGW(TAG, "loop cooldown %" PRIu32 " ms", (uint32_t)HAPTIC_LOOP_COOLDOWN_MS);
        pump_driver_events(HAPTIC_LOOP_COOLDOWN_MS);
    }

idle:
    ESP_LOGE(TAG, "haptic scan stopped; leaving task alive for log inspection");
    while (1) {
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

    if ((tp_queue != NULL) && (main_queue_set != NULL)) {
        xQueueAddToSet(tp_queue, main_queue_set);
    }

    if ((mouse_queue != NULL) && (main_queue_set != NULL)) {
        xQueueAddToSet(mouse_queue, main_queue_set);
    }
}

void app_main(void)
{
    uint8_t probe[4] = {0};
    esp_err_t probe_ret;
    esp_reset_reason_t reset_reason = esp_reset_reason();

    s_boot_count++;
    ESP_LOGW(TAG,
             "boot_count=%" PRIu32 " reset_reason=%d (%s)",
             s_boot_count,
             (int)reset_reason,
             reset_reason_to_string(reset_reason));

    create_minimal_queues();
    gpio_init();
    touchpad_init();

    probe_ret = i2c_master_receive(dev_haptic_motor_handle, probe, sizeof(probe), 100);
    ESP_LOGW(TAG,
             "raw CS40L25 probe ret=%d bytes=%02X %02X %02X %02X",
             probe_ret,
             probe[0],
             probe[1],
             probe[2],
             probe[3]);

    xTaskCreatePinnedToCore(haptic_scan_task, "haptic_scan", 8192, NULL, 8, NULL, 1);
}
