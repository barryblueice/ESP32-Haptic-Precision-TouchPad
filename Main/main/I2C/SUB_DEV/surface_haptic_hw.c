#include "surface_haptic_hw.h"
#include <inttypes.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "GPIO/GPIO_handle.h"
#include "I2C/I2C_handle.h"
#include "I2C/TP/i2c_hid.h"
#include "sub_dev.h"
#include "mcu-drivers/common/platform_bsp/platform_bsp.h"
#include "mcu-drivers/cs40l25/bsp/surface_fw_metadata.h"
#include "mcu-drivers/cs40l25/cs40l25_spec.h"
#include "mcu-drivers/cs40l25/cs40l25_syscfg_regs.h"
#include "mcu-drivers/cs40l25/cs40l25.h"
#include "mcu-drivers/fw_img/cs40l25_fw_img.h"

#define TAG "SURFACE_HW"
#define IO_TIMEOUT_MS 100
// MP28167GQ-A-Z: 0.8 mV/LSB. Board measurement: RAW 652 -> VOUT 6.75 V.
// Assuming normal regulation, round(652 * 13.0 / 6.75) = 1256 -> 1004.8 mV VREF.
// Estimated VOUT is 13.003 V; verify on hardware (see MP28167_VOLTAGE.md).
#define MP28167_TARGET_VREF_RAW 1256U
#define CHECK_ESP(call) do { esp_err_t e_ = (call); if (e_ != ESP_OK) { \
    ESP_LOGE(TAG, "%s: %s", #call, esp_err_to_name(e_)); return false; } } while (0)
#define CHECK_BSP(call) do { uint32_t s_ = (call); if (s_ != BSP_STATUS_OK) { \
    ESP_LOGE(TAG, "%s: 0x%08" PRIX32, #call, s_); return false; } } while (0)

static bool firmware_loaded;
static bool chip_identified;
static uint32_t hardware_errors;

static void notification(uint32_t flags, void *arg)
{
    (void)arg;
    uint32_t errors = flags & (CS40L25_EVENT_FLAG_DSP_ERROR | CS40L25_EVENT_FLAG_AMP_SHORT |
        CS40L25_EVENT_FLAG_OVERTEMP_ERROR | CS40L25_EVENT_FLAG_OVERTEMP_WARNING |
        CS40L25_EVENT_FLAG_BOOST_INDUCTOR_SHORT | CS40L25_EVENT_FLAG_BOOST_UNDERVOLTAGE |
        CS40L25_EVENT_FLAG_BOOST_OVERVOLTAGE | CS40L25_EVENT_FLAG_STATE_ERROR);
    // The alert ISR also sends BSP_STATUS_DUT_EVENTS (2); it must not touch this latch.
    // Error flags are delivered by cs40l25_process in the owning task.
    if (errors != 0) hardware_errors |= errors;
}

uint32_t surface_haptic_hw_process(void)
{
    uint32_t status = bsp_dut_process();
    if (hardware_errors != 0) {
        ESP_LOGE(TAG, "Latched hardware events=0x%08" PRIX32, hardware_errors);
        return BSP_STATUS_FAIL;
    }
    return status;
}

static void wait_ms(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks ? ticks : 1);
}

static uint32_t le32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool read_reg(uint32_t reg, uint32_t *value)
{
    uint8_t address[] = {reg >> 24, reg >> 16, reg >> 8, reg};
    uint8_t data[4];
    CHECK_ESP(i2c_master_transmit_receive(dev_haptic_motor_handle, address, 4, data, 4, IO_TIMEOUT_MS));
    *value = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
    return true;
}

static bool check_vbst(const char *phase)
{
    uint32_t value;
    if (!read_reg(BOOST_VBST_CTL_1_REG, &value)) return false;
    ESP_LOGI(TAG, "%s VBST parameter=11000 mV target=0x%02X actual=0x%08" PRIX32
             " external_boost=true", phase, (unsigned int)CS40L25_SURFACE_VBST_CTL, value);
    if ((value & 0xFFU) != CS40L25_SURFACE_VBST_CTL) {
        ESP_LOGE(TAG, "%s VBST parameter mismatch", phase);
        return false;
    }
    return true;
}

static bool read_vref(uint16_t *raw)
{
    uint8_t hi, lo, reg = MP28167_REG_VREF_H;
    CHECK_ESP(i2c_master_transmit_receive(sub_dev_mp28167_handle, &reg, 1, &hi, 1, IO_TIMEOUT_MS));
    reg = MP28167_REG_VREF_L;
    CHECK_ESP(i2c_master_transmit_receive(sub_dev_mp28167_handle, &reg, 1, &lo, 1, IO_TIMEOUT_MS));
    *raw = ((uint16_t)hi << 3) | (lo & 7U);
    return true;
}

static bool prepare_power(void)
{
    CHECK_ESP(gpio_set_level(GPIO_HAPTIC_BUCK_BOOST_EN, EN_ON));
    wait_ms(250);
    CHECK_ESP(i2c_master_probe(sub_bus_handle, MP28167_ADDR, IO_TIMEOUT_MS));
    uint16_t raw;
    if (!read_vref(&raw)) return false;
    if (raw != MP28167_TARGET_VREF_RAW) {
        const uint8_t writes[][2] = {{MP28167_REG_VREF_L, MP28167_TARGET_VREF_RAW & 7U},
                                    {MP28167_REG_VREF_H, MP28167_TARGET_VREF_RAW >> 3},
                                    {MP28167_REG_VREF_GO, 1}};
        for (unsigned int i = 0; i < 3; ++i) {
            CHECK_ESP(i2c_master_transmit(sub_dev_mp28167_handle, writes[i], 2, IO_TIMEOUT_MS));
        }
        wait_ms(250);
        if (!read_vref(&raw)) return false;
    }
    ESP_LOGI(TAG, "MP28167 VREF raw=%u reference=%.1f mV; VOUT target=13000 mV (not measured)",
             (unsigned int)raw, (double)raw * 0.8);
    return raw == MP28167_TARGET_VREF_RAW;
}

static bool identify(void)
{
    CHECK_ESP(i2c_master_probe(bus_handle, HAPTIC_MOTOR_ADDR, IO_TIMEOUT_MS));
    uint32_t id, rev;
    if (!read_reg(CS40L25_SW_RESET_DEVID_REG, &id) || !read_reg(CS40L25_SW_RESET_REVID_REG, &rev)) return false;
    ESP_LOGI(TAG, "DEVID=0x%08" PRIX32 " REVID=0x%08" PRIX32, id, rev);
    chip_identified = id == CS40L25_DEVID || id == CS40L25B_DEVID;
    return chip_identified;
}

static bool check_wave_count(void)
{
    uint32_t count = 0;
    CHECK_BSP(bsp_dut_get_num_waves(&count));
    ESP_LOGI(TAG, "DSP waves=%" PRIu32 " expected=%u", count, SURFACE_FW_NUM_WAVES);
    return count == SURFACE_FW_NUM_WAVES;
}

bool surface_haptic_hw_initialize(void)
{
    firmware_loaded = chip_identified = false;
    hardware_errors = 0;
    CHECK_ESP(gpio_set_level(GPIO_HAPTIC_BUCK_BOOST_EN, EN_OFF));
    CHECK_ESP(gpio_set_direction(GPIO_HAPTIC_BUCK_BOOST_EN, GPIO_MODE_OUTPUT));
    ESP_LOGI(TAG, "Surface ID=0x%06" PRIX32 " revision=0x%06" PRIX32 " size=%" PRIu32,
             le32(cs40l25_fw_img + 20), le32(cs40l25_fw_img + 24), le32(cs40l25_fw_img + 8));
    ESP_LOGI(TAG, "Expected SHA256=%s (verified offline)", SURFACE_FW_SHA256);
    if (le32(cs40l25_fw_img + 8) != SURFACE_FW_SIZE_BYTES ||
        le32(cs40l25_fw_img + 20) != SURFACE_FW_ID ||
        le32(cs40l25_fw_img + 24) != SURFACE_FW_REVISION) return false;
    if (!prepare_power() || !identify()) return false;
    CHECK_BSP(bsp_initialize(notification, NULL));
    CHECK_BSP(bsp_dut_initialize());
    ESP_LOGI(TAG, "Skip shared GPIO33 reset and ROM/BHM playback");
    CHECK_BSP(bsp_dut_boot(false));
    firmware_loaded = true;
    bsp_dut_log_gain("post-boot");
    for (unsigned int i = 0; i < 10; ++i) {
        CHECK_BSP(surface_haptic_hw_process());
        wait_ms(10);
    }
    CHECK_BSP(bsp_dut_power_up());
    bool changed;
    CHECK_BSP(bsp_dut_has_processed(&changed));
    TickType_t start = xTaskGetTickCount();
    do {
        CHECK_BSP(surface_haptic_hw_process());
        wait_ms(10);
        CHECK_BSP(bsp_dut_has_processed(&changed));
        if (changed) break;
    } while ((TickType_t)(xTaskGetTickCount() - start) < pdMS_TO_TICKS(2000));
    if (!changed || !check_wave_count()) return false;
    CHECK_BSP(bsp_dut_update_haptic_config(0));
    CHECK_BSP(bsp_dut_enable_haptic_processing(true));
    bsp_dut_log_gain("initialize");
    return check_vbst("initialize");
}

bool surface_haptic_hw_wake(void)
{
    if (!firmware_loaded || !prepare_power() || !identify()) return false;
    CHECK_BSP(bsp_dut_power_up());
    if (!check_wave_count()) return false;
    bool changed;
    // Idle standby need not advance heartbeat; activity is checked on next playback.
    CHECK_BSP(bsp_dut_has_processed(&changed));
    CHECK_BSP(surface_haptic_hw_process());
    bsp_dut_log_gain("wake");
    return check_vbst("wake");
}

bool surface_haptic_hw_power_off(void)
{
    CHECK_ESP(gpio_set_level(GPIO_HAPTIC_BUCK_BOOST_EN, EN_OFF));
    return true;
}

void surface_haptic_hw_diagnostics(uint8_t waveform)
{
    if (chip_identified) {
        const uint32_t regs[] = {BOOST_VBST_CTL_1_REG, BOOST_VBST_CTL_2_REG,
                                 MSM_BLOCK_ENABLES_REG, MSM_GLOBAL_ENABLES_REG};
        for (unsigned int i = 0; i < sizeof(regs) / sizeof(regs[0]); ++i) {
            uint32_t value;
            if (read_reg(regs[i], &value)) {
                ESP_LOGE(TAG, "Boost diagnostic reg=0x%08" PRIX32 " value=0x%08" PRIX32,
                         regs[i], value);
            }
        }
    }
    if (firmware_loaded) {
        bsp_dut_dump_trigger_diagnostics(waveform, 0);
    } else if (chip_identified) {
        uint32_t scratch;
        if (read_reg(XM_UNPACKED24_DSP1_SCRATCH_REG, &scratch)) {
            ESP_LOGE(TAG, "DSP SCRATCH=0x%08" PRIX32, scratch);
        }
    }
}
