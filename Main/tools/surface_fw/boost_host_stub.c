/* Inserted production functions are compiled with real driver types. */
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stddef.h>
#include "I2C/SUB_DEV/mcu-drivers/cs40l25/cs40l25.h"
#include "I2C/SUB_DEV/mcu-drivers/cs40l25/cs40l25_syscfg_regs.h"
#include "I2C/SUB_DEV/mcu-drivers/cs40l25/bsp/surface_fw_metadata.h"
#include "I2C/SUB_DEV/mcu-drivers/common/platform_bsp/platform_bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
int _fltused = 0;
#else
#define EXPORT
#endif

#pragma clang diagnostic ignored "-Wunused-parameter"
static cs40l25_t dut;
static fw_img_info_t info;
static uint32_t vbst, scenario, reg_reads, diag_mask, ticks, enabled;
static uint32_t bus_calls, fail_call, wseq_saved_vbst, hibernate_calls, array_failure;
static uint32_t missing_symbol;
#define WSEQ_BASE 0x02801000U

uint32_t regmap_write(regmap_cp_config_t *cp, uint32_t addr, uint32_t val) {
    ++bus_calls;
    if (addr == DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_4_REG) ++hibernate_calls;
    if (bus_calls == fail_call) return REGMAP_STATUS_FAIL;
    if (addr == BOOST_VBST_CTL_1_REG) vbst = val;
    return REGMAP_STATUS_OK;
}
uint32_t regmap_write_block(regmap_cp_config_t *cp, uint32_t addr, uint8_t *bytes, uint32_t len) {
    ++bus_calls;
    if (bus_calls == fail_call) return REGMAP_STATUS_FAIL;
    if (len != 8) return REGMAP_STATUS_FAIL;
    if (bytes[1] == 0x38 && bytes[2] == 0) {
        wseq_saved_vbst = ((uint32_t)bytes[3] << 24) | ((uint32_t)bytes[5] << 16) |
                          ((uint32_t)bytes[6] << 8) | bytes[7];
    }
    return REGMAP_STATUS_OK;
}
uint32_t regmap_write_array(regmap_cp_config_t *cp, uint32_t *array, uint32_t len) {
    if (array_failure) return REGMAP_STATUS_FAIL;
    for (uint32_t i = 0; i < len; i += 2) {
        if (regmap_write(cp, array[i], array[i + 1])) return REGMAP_STATUS_FAIL;
    }
    return REGMAP_STATUS_OK;
}
uint32_t regmap_write_fw_control(regmap_cp_config_t *cp, fw_img_info_t *f, uint32_t id, uint32_t val) { return 0; }
uint32_t regmap_write_fw_vals(regmap_cp_config_t *cp, fw_img_info_t *f, uint32_t id, uint32_t *val, uint32_t size) { return 0; }
uint32_t fw_img_find_symbol(fw_img_info_t *f, uint32_t id) { return missing_symbol ? 0 : WSEQ_BASE; }

/* PRODUCTION_SDK */

typedef int esp_err_t;
#define ESP_OK 0
#define EN_ON 1
#define EN_OFF 0
#define GPIO_HAPTIC_BUCK_BOOST_EN 14
#define GPIO_MODE_OUTPUT 1
#define MP28167_REG_VREF_L 0
#define MP28167_REG_VREF_H 1
#define MP28167_REG_VREF_GO 2
#define MP28167_ADDR 0x60
#define HAPTIC_MOTOR_ADDR 0x40
static const int dev_haptic_motor_handle = 1, sub_dev_mp28167_handle = 2;
static const int bus_handle = 3, sub_bus_handle = 4;
static const uint8_t cs40l25_fw_img[28] = {
    [8] = SURFACE_FW_SIZE_BYTES & 255, [9] = SURFACE_FW_SIZE_BYTES >> 8,
    [20] = SURFACE_FW_ID & 255, [21] = (SURFACE_FW_ID >> 8) & 255, [22] = SURFACE_FW_ID >> 16,
    [24] = SURFACE_FW_REVISION & 255, [25] = (SURFACE_FW_REVISION >> 8) & 255, [26] = SURFACE_FW_REVISION >> 16
};
static const char *esp_err_to_name(int err) { return "injected I2C failure"; }
static int gpio_set_level(int pin, int level) { enabled = level; return 0; }
static int gpio_set_direction(int pin, int dir) { return 0; }
static void vTaskDelay(TickType_t ms) { ticks += ms; }
static TickType_t xTaskGetTickCount(void) { return ticks; }
static int i2c_master_probe(int bus, int address, int timeout) { return 0; }
static int i2c_master_transmit(int dev, const uint8_t *data, size_t size, int timeout) { return 0; }
static int i2c_master_transmit_receive(int dev, const uint8_t *address, size_t addr_len,
                                       uint8_t *data, size_t size, int timeout) {
    if (dev == sub_dev_mp28167_handle) {
        data[0] = *address == MP28167_REG_VREF_H ? 81 : 4; /* 652 */
        return 0;
    }
    uint32_t reg = ((uint32_t)address[0] << 24) | ((uint32_t)address[1] << 16) |
                   ((uint32_t)address[2] << 8) | address[3];
    uint32_t value = 0;
    if (reg == CS40L25_SW_RESET_DEVID_REG) value = CS40L25_DEVID;
    if (reg == BOOST_VBST_CTL_1_REG) {
        ++reg_reads; diag_mask |= 1;
        if (scenario == 2 || scenario == 4) return -1;
        value = (scenario == 3 || scenario == 5) ? 0 : vbst;
        if (scenario == 13) value |= 0xA5000000U;
    }
    if (reg == BOOST_VBST_CTL_2_REG) diag_mask |= 2;
    if (reg == MSM_BLOCK_ENABLES_REG) diag_mask |= 4;
    if (reg == MSM_GLOBAL_ENABLES_REG) diag_mask |= 8;
    for (unsigned int i = 0; i < 4; ++i) data[i] = value >> (24 - 8 * i);
    return 0;
}
uint32_t bsp_initialize(bsp_app_callback_t cb, void *arg) { return 0; }
uint32_t bsp_dut_initialize(void) {
    dut.config.syscfg_regs = cs40l25_syscfg_regs;
    dut.config.syscfg_regs_total = CS40L25_SYSCFG_REGS_TOTAL;
    dut.config.ext_boost.use_ext_boost = true;
    return 0;
}
uint32_t bsp_dut_boot(bool cal) { info.header.fw_id = SURFACE_FW_ID; return cs40l25_boot(&dut, &info); }
uint32_t bsp_dut_power_up(void) { return 0; }
uint32_t bsp_dut_process(void) { return 0; }
uint32_t bsp_dut_has_processed(bool *changed) { *changed = true; return 0; }
uint32_t bsp_dut_get_num_waves(uint32_t *count) { *count = 78; return 0; }
uint32_t bsp_dut_update_haptic_config(uint8_t index) { return 0; }
uint32_t bsp_dut_enable_haptic_processing(bool enable) { return 0; }
void bsp_dut_dump_trigger_diagnostics(uint8_t waveform, uint32_t duration) { }

/* PRODUCTION_HW */

static void zero(void *ptr, size_t len) {
    volatile uint8_t *p = ptr;
    while (len--) *p++ = 0;
}
#define REQUIRE(expr) do { if (!(expr)) return __LINE__; } while (0)
EXPORT uint32_t boost_test_run(uint32_t which) {
    zero(&dut, sizeof(dut)); zero(&info, sizeof(info));
    vbst = reg_reads = diag_mask = ticks = enabled = 0;
    bus_calls = fail_call = wseq_saved_vbst = hibernate_calls = array_failure = missing_symbol = 0;
    scenario = which;
    if (which == 6) array_failure = 1;
    /* Wake-failure injection starts only after a successful initialization. */
    if (which == 4 || which == 5) scenario = 0;
    bool ok = surface_haptic_hw_initialize();
    if (which == 2 || which == 3 || which == 6) {
        REQUIRE(!ok);
        REQUIRE(reg_reads == (which == 6 ? 0U : 1U));
        return 0;
    }
    REQUIRE(ok && reg_reads == 1 && vbst == 0xAA);
    REQUIRE(CS40L25_SYSCFG_REGS_TOTAL == 34 && MP28167_TARGET_VREF_RAW == 652);
    uint32_t pos = 0;
    for (; pos < dut.wseq_num_entries; ++pos) {
        if (dut.wseq_table[pos].address_ms == 0x38 && dut.wseq_table[pos].address_ls == 0) break;
    }
    REQUIRE(pos < dut.wseq_num_entries && dut.wseq_table[pos].val_0 == 0xAA);
    if (which == 1 || which == 4 || which == 5) {
        scenario = which;
        REQUIRE(surface_haptic_hw_wake() == (which == 1));
        REQUIRE(reg_reads == 2);
    }
    if (which >= 7 && which <= 11) {
        bus_calls = 0;
        uint32_t count = dut.wseq_num_entries;
        if (which == 8) fail_call = pos + 1;
        if (which == 9) fail_call = count + 1;
        if (which == 10) fail_call = count + 2;
        if (which == 11) missing_symbol = 1;
        REQUIRE(cs40l25_hibernate(&dut) == (which == 7 ? CS40L25_STATUS_OK : CS40L25_STATUS_FAIL));
        REQUIRE(hibernate_calls == (which == 7 || which == 10 ? 1U : 0U));
        if (which == 8) {
            REQUIRE(bus_calls == fail_call);
            for (uint32_t i = 0; i < count; ++i) REQUIRE(dut.wseq_table[i].changed == (i >= pos));
        }
        if (which == 9 || which == 10) REQUIRE(bus_calls == fail_call);
        if (which == 11) REQUIRE(bus_calls == 0 && dut.wseq_table[0].changed == 1);
        if (which == 7) {
            REQUIRE(bus_calls == count + 2 && wseq_saved_vbst == 0xAA);
            for (uint32_t i = 0; i < count; ++i) REQUIRE(dut.wseq_table[i].changed == 0);
            vbst = wseq_saved_vbst; /* Emulate DSP replay of the recorded entry. */
            REQUIRE(surface_haptic_hw_wake() && reg_reads == 2);
        }
    }
    if (which == 12) {
        diag_mask = 0;
        surface_haptic_hw_diagnostics(21);
        REQUIRE(diag_mask == 15);
    }
    return 0;
}
