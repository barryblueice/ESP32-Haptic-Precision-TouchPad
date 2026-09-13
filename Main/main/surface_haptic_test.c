#include "sdkconfig.h"
#if CONFIG_SURFACE_HAPTIC_TEST_MODE
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "GPIO/GPIO_handle.h"
#include "I2C/I2C_handle.h"
#include "I2C/TP/i2c_hid.h"
#include "I2C/SUB_DEV/sub_dev.h"
#include "I2C/SUB_DEV/surface_haptic_hw.h"
#include "I2C/SUB_DEV/surface_haptic_policy.h"
#include "I2C/SUB_DEV/mcu-drivers/common/platform_bsp/platform_bsp.h"
#include "I2C/SUB_DEV/mcu-drivers/cs40l25/bsp/bsp_dut.h"
#include "I2C/SUB_DEV/mcu-drivers/cs40l25/bsp/surface_fw_metadata.h"
#include "I2C/SUB_DEV/mcu-drivers/cs40l25/cs40l25_spec.h"
#include "I2C/SUB_DEV/mcu-drivers/fw_img/cs40l25_fw_img.h"

#define TAG "SURFACE_FW_TEST"
#define TEST_DURATION_MS             0U
#define TEST_INTERVAL_MS             2000U
#define TEST_I2C_TIMEOUT_MS          100
#define TEST_POWER_SETTLE_MS         250U
#define TEST_BOOT_SETTLE_MS          100U
#define TEST_PROCESS_INTERVAL_MS     10U
#define TEST_HEARTBEAT_TIMEOUT_MS    2000U
#define TEST_VREF_MV                 840U
#define TEST_TASK_STACK_BYTES        8192U

static bool s_boost_configured;
static uint8_t s_current_waveform;
static const uint8_t s_test_settings[] = {0, 25, 63, 75, 100};

static TickType_t test_ticks(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);
    return ticks ? ticks : 1;
}

static void test_halt(const char *stage) __attribute__((noreturn));

static void test_halt(const char *stage)
{
    ESP_LOGE(TAG, "HALTED at %s; automatic playback stopped", stage);
    surface_haptic_hw_diagnostics(s_current_waveform);
    if (s_boost_configured) {
        esp_err_t err = gpio_set_level(GPIO_HAPTIC_BUCK_BOOST_EN, EN_OFF);
        ESP_LOGE(TAG, "external boost off: %s", esp_err_to_name(err));
    }
    ESP_LOGE(TAG, "Reset to retry; no automatic restart, firmware reload or old-image fallback");
    while (true) {
        vTaskDelay(test_ticks(1000));
    }
}

static void test_check_esp(const char *stage, esp_err_t err)
{
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: %s (0x%x)", stage, esp_err_to_name(err), (unsigned int)err);
        test_halt(stage);
    }
    ESP_LOGI(TAG, "%s: OK", stage);
}

static void test_check_bsp(const char *stage, uint32_t status)
{
    if (status != BSP_STATUS_OK) {
        ESP_LOGE(TAG, "%s: status=0x%08" PRIX32, stage, status);
        test_halt(stage);
    }
    ESP_LOGI(TAG, "%s: OK", stage);
}

static void test_prepare_gpio(void)
{
    test_check_esp("GPIO33 high latch", gpio_set_level(TP_RESET_GPIO, 1));
    test_check_esp("boost off latch", gpio_set_level(GPIO_HAPTIC_BUCK_BOOST_EN, EN_OFF));
    const gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << TP_RESET_GPIO) | (1ULL << GPIO_HAPTIC_BUCK_BOOST_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    test_check_esp("GPIO33/GPIO14 outputs", gpio_config(&outputs));
    s_boost_configured = true;
    esp_err_t err = gpio_install_isr_service(0);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "GPIO ISR service already installed");
    } else {
        test_check_esp("GPIO ISR service", err);
    }
}

static void test_init_i2c(void)
{
    i2c_master_bus_config_t bus = {
        .i2c_port = TP_I2C_PORT,
        .sda_io_num = TP_I2C_SDA,
        .scl_io_num = TP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };
    test_check_esp("I2C0 SDA36/SCL35", i2c_new_master_bus(&bus, &bus_handle));
    i2c_device_config_t device = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = HAPTIC_MOTOR_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    test_check_esp("CS40L25 device 0x43",
                  i2c_master_bus_add_device(bus_handle, &device, &dev_haptic_motor_handle));
    bus.i2c_port = SUB_I2C_PORT;
    bus.sda_io_num = SUB_I2C_SDA;
    bus.scl_io_num = SUB_I2C_SCL;
    test_check_esp("I2C1 SDA17/SCL18", i2c_new_master_bus(&bus, &sub_bus_handle));
    device.device_address = MP28167_ADDR;
    test_check_esp("MP28167 device 0x60",
                  i2c_master_bus_add_device(sub_bus_handle, &device, &sub_dev_mp28167_handle));
}

static void test_process_wait(uint32_t duration_ms)
{
    const TickType_t start = xTaskGetTickCount();
    do {
        uint32_t status = surface_haptic_hw_process();
        if (status != BSP_STATUS_OK) {
            test_check_bsp("driver event processing", status);
        }
        vTaskDelay(test_ticks(TEST_PROCESS_INTERVAL_MS));
    } while ((TickType_t)(xTaskGetTickCount() - start) < test_ticks(duration_ms));
}

static void test_heartbeat_baseline(void)
{
    bool changed;
    // Discard this result; establish the baseline BEFORE a short playback command.
    uint32_t status = bsp_dut_has_processed(&changed);
    if (status != BSP_STATUS_OK) {
        test_check_bsp("DSP heartbeat baseline", status);
    }
}

static void test_verify_heartbeat(void)
{
    const TickType_t start = xTaskGetTickCount();
    do {
        test_process_wait(TEST_PROCESS_INTERVAL_MS);
        bool changed;
        uint32_t status = bsp_dut_has_processed(&changed);
        if (status != BSP_STATUS_OK) {
            test_check_bsp("DSP heartbeat read", status);
        }
        if (changed) {
            ESP_LOGI(TAG, "DSP heartbeat advanced: activity confirmed");
            return;
        }
    } while ((TickType_t)(xTaskGetTickCount() - start) < test_ticks(TEST_HEARTBEAT_TIMEOUT_MS));
    test_halt("DSP heartbeat unchanged for 2 seconds");
}

static void test_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Surface press/release policy test; reset reason=%d", (int)esp_reset_reason());
    test_prepare_gpio();
    test_init_i2c();
    if (!surface_haptic_hw_initialize()) {
        test_halt("shared hardware initialization");
    }
    ESP_LOGI(TAG, "READY: settings=0,25,63,75,100; PRESS then RELEASE; MBOX1 index playback; slot~%u ms",
             TEST_INTERVAL_MS);
    ESP_LOGI(TAG, "CP/GPI attenuation=0; GPIO triggers disabled; duration argument=0 selects MBOX1");
    ESP_LOGI(TAG, "Command ACK and heartbeat do not prove physical vibration; observe hardware");

    uint32_t round = 1;
    while (true) {
        for (size_t i = 0; i < sizeof(s_test_settings) / sizeof(s_test_settings[0]); ++i) {
            const uint8_t setting = s_test_settings[i];
            surface_haptic_pair_t pair;
            if (!surface_haptic_resolve(setting, &pair)) {
                test_halt("invalid Surface setting");
            }
            for (unsigned int event = 0; event < 2; ++event) {
                const bool release = event != 0;
                const char *phase = release ? "RELEASE" : "PRESS";
                const TickType_t cycle_start = xTaskGetTickCount();
                s_current_waveform = release ? pair.release_index : pair.press_index;
                if (pair.enabled) {
                    const bool is_xm = s_current_waveform < SURFACE_FW_XM_WAVES;
                    ESP_LOGI(TAG, "PLAY round=%" PRIu32 " setting=%u phase=%s waveform=%u bank=%s local_index=%u mode=MBOX1",
                             round, (unsigned int)setting, phase, (unsigned int)s_current_waveform,
                             is_xm ? "XM" : "YM", is_xm ? (unsigned int)s_current_waveform :
                             (unsigned int)(s_current_waveform - SURFACE_FW_XM_WAVES));
                    test_heartbeat_baseline();
                }
                uint32_t status = surface_haptic_play_event(&pair, release);
                ESP_LOGI(TAG, "%s round=%" PRIu32 " setting=%u phase=%s waveform=%u result=0x%08" PRIX32,
                         pair.enabled ? "PLAY" : "SKIP", round, (unsigned int)setting,
                         phase, (unsigned int)s_current_waveform, status);
                test_check_bsp("Surface policy event", status);
                if (pair.enabled) {
                    test_verify_heartbeat();
                }
                // Disabled slots and idle standby do not require heartbeat advancement.
                const TickType_t elapsed = (TickType_t)(xTaskGetTickCount() - cycle_start);
                if (elapsed < test_ticks(TEST_INTERVAL_MS)) {
                    test_process_wait((test_ticks(TEST_INTERVAL_MS) - elapsed) * portTICK_PERIOD_MS);
                }
            }
        }
        ++round;
    }
}

void surface_haptic_test_start(void)
{
    if (xTaskCreatePinnedToCore(test_task, "surface_fw_test", TEST_TASK_STACK_BYTES,
                               NULL, 8, NULL, 1) != pdPASS) {
        esp_err_t latch = gpio_set_level(GPIO_HAPTIC_BUCK_BOOST_EN, EN_OFF);
        esp_err_t mode = gpio_set_direction(GPIO_HAPTIC_BUCK_BOOST_EN, GPIO_MODE_OUTPUT);
        s_boost_configured = latch == ESP_OK && mode == ESP_OK;
        ESP_LOGE(TAG, "task creation failed; boost latch=%s mode=%s",
                 esp_err_to_name(latch), esp_err_to_name(mode));
        test_halt("test task allocation");
    }
}

#endif
