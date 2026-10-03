/* Temporary sensor bench test; restore main.c from 7bab7a9.
 * Protocol: mcu-drivers/tools/surface_firmware/SAM_BUSES.zh-CN.md sections 4/4.1.
 * C1-only Force test: initialize Haptic first, query ID, set/verify/commit mode 1, enter EC=C1 once.
 * During acquisition no EC=10, no ED clear/poll, and no Force mode-register writes.
 * Force mode-1 write/commit only; no Force firmware/calibration writes or playback.
 * Starts automatically; MP28167 power setup uses the existing local Haptic driver.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "I2C/I2C_handle.h"
#include "I2C/TP/i2c_hid.h"
#include "I2C/SUB_DEV/sub_dev.h"
#include "I2C/SUB_DEV/surface_haptic_hw.h"
#include "I2C/SUB_DEV/mcu-drivers/common/platform_bsp/platform_bsp.h"

#define TAG "SENSOR_TEST"
#define IO_TIMEOUT_MS 100
#define BOOST_EN_GPIO GPIO_NUM_14
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_temp, s_force;
typedef struct { uint32_t attempts, ok, errors, timeouts, consecutive; } stats_t;
static stats_t s_ts, s_fs;
static uint32_t s_id_n, s_id_errors;
static bool s_haptic_ready;
static uint32_t s_c1_checksum_errors;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static void wait_ms(uint32_t ms) {
    TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks ? ticks : 1);
}
static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint16_t be16(const uint8_t *p) { return ((uint16_t)p[0] << 8) | p[1]; }
static int32_t temperature_counts(uint16_t raw) {
    /* Portable equivalent of int16(raw) arithmetic shift right by 4. */
    return (raw & 0x8000) ? (int32_t)(raw >> 4) - 4096 : raw >> 4;
}
static bool id_checksum_ok(const uint8_t *p, size_t len) {
    if (len != 7) return false;
    uint16_t sum = (uint16_t)p[1] + p[2] + p[3] + p[4];
    return (uint16_t)(0U - sum) == le16(p + 5);
}
static void hex_bytes(const uint8_t *p, size_t n, char *out) {
    static const char digits[] = "0123456789ABCDEF";
    for (size_t i = 0; i < n; ++i) {
        out[i*2] = digits[p[i] >> 4]; out[i*2+1] = digits[p[i] & 15];
    }
    out[n*2] = 0;
}
static void record_result(stats_t *s, esp_err_t err) {
    ++s->attempts;
    if (err == ESP_OK) { ++s->ok; s->consecutive = 0; }
    else { ++s->errors; ++s->consecutive; if (err == ESP_ERR_TIMEOUT) ++s->timeouts; }
}
/* The IDF synchronous master API exposes one combined return code and no
 * actual RX length. Never turn requested length or changed-byte count into
 * a claimed hardware byte count. A5 retention is ambiguous (valid data may
 * equal A5); changed-byte count is diagnostic only, never a validity gate.
 */
static uint32_t s_trace_seq;
static esp_err_t traced_write(const uint8_t *tx, size_t len) {
    char hex[33];
    if (len > 16) return ESP_ERR_INVALID_SIZE;
    hex_bytes(tx, len, hex);
    esp_err_t err = i2c_master_transmit(s_force, tx, len, IO_TIMEOUT_MS);
    ESP_LOGI(TAG, "I2C_TX n=%" PRIu32 " addr7=0x49 tx=%s tx_req=%u write_ret=%s(0x%x)",
             ++s_trace_seq, hex, (unsigned)len, esp_err_to_name(err), (unsigned)err);
    return err;
}
static esp_err_t traced_write_read(const uint8_t *tx, size_t txlen,
                                  uint8_t *rx, size_t rxlen, int timeout_ms) {
    char txhex[33], rxhex[65];
    if (txlen > 16 || rxlen > 32) return ESP_ERR_INVALID_SIZE;
    memset(rx, 0xA5, rxlen);
    hex_bytes(tx, txlen, txhex);
    esp_err_t err = i2c_master_transmit_receive(s_force, tx, txlen, rx, rxlen, timeout_ms);
    hex_bytes(rx, rxlen, rxhex);
    size_t changed = 0;
    for (size_t i = 0; i < rxlen; ++i) if (rx[i] != 0xA5) ++changed;
    ESP_LOGI(TAG, "I2C_WR n=%" PRIu32 " addr7=0x49 tx=%s tx_req=%u rx_req=%u combined_ret=%s(0x%x) rx_actual=UNAVAILABLE changed_from_A5=%u rx_buffer=%s",
             ++s_trace_seq, txhex, (unsigned)txlen, (unsigned)rxlen,
             esp_err_to_name(err), (unsigned)err, (unsigned)changed, rxhex);
    return err;
}
static esp_err_t force_read(uint8_t reg, uint8_t *out, size_t len) {
    const uint8_t request[] = {0xD0, reg, 0};
    return traced_write_read(request, sizeof(request), out, len, IO_TIMEOUT_MS);
}
static void read_identity(void) {
    uint8_t raw[7]; char hex[15];
    esp_err_t err = force_read(0x10, raw, sizeof(raw));
    ++s_id_n;
    if (err == ESP_OK) {
        hex_bytes(raw, sizeof(raw), hex);
        bool checksum = id_checksum_ok(raw, sizeof(raw));
        bool all_zero = true;
        for (size_t i = 0; i < sizeof(raw); ++i) if (raw[i] != 0) all_zero = false;
        bool valid = checksum && !all_zero;
        if (!valid) ++s_id_errors;
        ESP_LOGI(TAG, "ID ms=%" PRId64 " raw=%s checksum=%s nonzero=%d valid=%d attempts=%" PRIu32 " errors=%" PRIu32,
                 now_ms(), hex, checksum ? "PASS" : "FAIL", !all_zero, valid, s_id_n, s_id_errors);
    } else {
        ++s_id_errors;
        ESP_LOGW(TAG, "ID ms=%" PRId64 " valid=0 err=%s attempts=%" PRIu32 " errors=%" PRIu32,
                 now_ms(), esp_err_to_name(err), s_id_n, s_id_errors);
    }
}
static void read_temperature(void) {
    uint8_t reg = 0, raw[2] = {0xA5, 0xA5};
    esp_err_t err = i2c_master_transmit_receive(s_temp, &reg, 1, raw, 2, IO_TIMEOUT_MS);
    record_result(&s_ts, err);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "TEMP ms=%" PRId64 " seq=%" PRIu32 " raw=%02X%02X u16=%u counts=%" PRId32 " errors=%" PRIu32,
                 now_ms(), s_ts.attempts, raw[0], raw[1], (unsigned)be16(raw),
                 temperature_counts(be16(raw)), s_ts.errors);
    } else {
        ESP_LOGW(TAG, "TEMP ms=%" PRId64 " seq=%" PRIu32 " valid=0 err=%s errors=%" PRIu32,
                 now_ms(), s_ts.attempts, esp_err_to_name(err), s_ts.errors);
    }
}
static void setup_bus(void) {
    ESP_ERROR_CHECK(gpio_set_level(BOOST_EN_GPIO, 0));
    ESP_ERROR_CHECK(gpio_set_level(TP_RESET_GPIO, 1));
    const gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << BOOST_EN_GPIO) | (1ULL << TP_RESET_GPIO),
        .mode = GPIO_MODE_OUTPUT, .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&outputs));
    ESP_ERROR_CHECK(gpio_set_level(TP_RESET_GPIO, 0)); wait_ms(50);
    ESP_ERROR_CHECK(gpio_set_level(TP_RESET_GPIO, 1)); wait_ms(150);
    const i2c_master_bus_config_t bus = {
        .i2c_port = TP_I2C_PORT, .sda_io_num = TP_I2C_SDA, .scl_io_num = TP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &s_bus));
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x48, .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev, &s_temp));
    dev.device_address = 0x49;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev, &s_force));
    ESP_LOGI(TAG, "PROBE address=0x48 result=%s", esp_err_to_name(i2c_master_probe(s_bus, 0x48, IO_TIMEOUT_MS)));
    ESP_LOGI(TAG, "PROBE address=0x49 result=%s", esp_err_to_name(i2c_master_probe(s_bus, 0x49, IO_TIMEOUT_MS)));
}
static void log_haptic_version(const char *phase) {
    const uint32_t regs[] = {0x0280000C, 0x02800010};
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t reg = regs[i];
        uint8_t tx[] = {reg >> 24, reg >> 16, reg >> 8, reg}, rx[4];
        esp_err_t err = i2c_master_transmit_receive(dev_haptic_motor_handle, tx, 4, rx, 4, IO_TIMEOUT_MS);
        if (err == ESP_OK) {
            uint32_t value = ((uint32_t)rx[0] << 24) | ((uint32_t)rx[1] << 16) | ((uint32_t)rx[2] << 8) | rx[3];
            ESP_LOGI(TAG, "HAPTIC_VERSION phase=%s reg=0x%08" PRIX32 " value=0x%08" PRIX32, phase, reg, value);
        } else ESP_LOGW(TAG, "HAPTIC_VERSION phase=%s err=%s", phase, esp_err_to_name(err));
    }
}
static void start_haptic_comparison(void) {
    ESP_LOGI(TAG, "HAPTIC_INIT_BEGIN ms=%" PRId64 "; before C1 sampling", now_ms());
    log_haptic_version("before");
    s_haptic_ready = surface_haptic_hw_initialize();
    log_haptic_version("after");
    if (!s_haptic_ready) {
        surface_haptic_hw_diagnostics(0);
        ESP_ERROR_CHECK(gpio_set_level(BOOST_EN_GPIO, 0));
        ESP_LOGE(TAG, "HAPTIC_INIT_FAILED ms=%" PRId64 "; boost OFF; comparison inconclusive", now_ms());
    } else {
        ESP_LOGI(TAG, "HAPTIC_INIT_OK ms=%" PRId64 "; startup heartbeat and wave count verified; no playback commands", now_ms());
    }
}
static int32_t signed_le16(const uint8_t *p) {
    uint16_t u = le16(p);
    return (u & 0x8000U) ? (int32_t)u - 65536 : (int32_t)u;
}
/* Exact 0xDD9BE..0xDD9EE semantics: NO truncation of expected to uint16_t. */
static bool c1_checksum(const uint8_t *raw, size_t len, int32_t *sum, uint32_t *expected) {
    if (len != 17) return false;
    *sum = le16(raw + 1);
    for (unsigned i = 0; i < 6; ++i) *sum += signed_le16(raw + 3 + 2*i);
    *expected = 0x10000U - (uint32_t)*sum;
    return *expected == (uint32_t)le16(raw + 15);
}
static bool mode1_readback_ok(const uint8_t *raw, size_t len) {
    return len == 5 && raw[1] == 1 && le16(raw + 3) == 0xFFFF;
}
static bool mode_commit_ok(const uint8_t *raw, size_t len) {
    return len == 3 && ((raw[1] == 0 && raw[2] == 0) ||
                       (raw[1] == 0x56 && raw[2] == 0xAA));
}
/* SAM 0xDE1AC: max three complete attempts; no other sampling in this function. */
static esp_err_t set_force_mode1(void) {
    const uint8_t write_mode[] = {0xD0,0x56,0x00,0x01,0x00,0xFF,0xFF};
    const uint8_t commit[] = {0xD0,0x01,0x00,0x56,0xAA};
    esp_err_t err = ESP_FAIL;
    for (unsigned attempt = 1; attempt <= 3; ++attempt) {
        uint8_t before[5], after[5], ack[3];
        const char *stage = "before";
        ESP_LOGI(TAG, "MODE1_BEGIN ms=%" PRId64 " attempt=%u", now_ms(), attempt);
        ESP_LOGI(TAG, "MODE1_READ stage=before attempt=%u", attempt);
        err = force_read(0x56,before,sizeof(before));
        if (err != ESP_OK) goto retry;
        stage = "write";
        err = traced_write(write_mode,sizeof(write_mode));
        if (err != ESP_OK) goto retry;
        stage = "readback";
        ESP_LOGI(TAG, "MODE1_READ stage=readback attempt=%u", attempt);
        err = force_read(0x56,after,sizeof(after));
        if (err != ESP_OK) goto retry;
        if (!mode1_readback_ok(after,sizeof(after))) { err = ESP_ERR_INVALID_RESPONSE; goto retry; }
        stage = "commit_write";
        err = traced_write(commit,sizeof(commit));
        if (err != ESP_OK) goto retry;
        stage = "commit_read";
        ESP_LOGI(TAG, "MODE1_READ stage=commit_read attempt=%u", attempt);
        err = force_read(0x01,ack,sizeof(ack));
        if (err != ESP_OK) goto retry;
        if (!mode_commit_ok(ack,sizeof(ack))) { err = ESP_ERR_INVALID_RESPONSE; goto retry; }
        ESP_LOGI(TAG, "MODE1_CONFIRMED ms=%" PRId64 " attempt=%u b1=1 checksum=FFFF ack=%02X%02X%02X",
                 now_ms(),attempt,ack[0],ack[1],ack[2]);
        return ESP_OK;
retry:
        ESP_LOGW(TAG, "MODE1_REJECT ms=%" PRId64 " attempt=%u stage=%s err=%s",
                 now_ms(),attempt,stage,esp_err_to_name(err));
        if (attempt < 3) wait_ms(2);
    }
    return err;
}
static esp_err_t decoder_selftest(void) {
    uint8_t raw[17] = {0};
    int32_t sum; uint32_t expected;
    if (c1_checksum(raw, 17, &sum, &expected) || expected != 65536 ||
        c1_checksum(raw, 15, &sum, &expected)) return ESP_FAIL;
    /* extra=2, channel0=-1, sum=1, expected=65535: valid. */
    raw[1]=2; raw[3]=0xFF; raw[4]=0xFF; raw[15]=0xFF; raw[16]=0xFF;
    if (!c1_checksum(raw,17,&sum,&expected) || sum!=1 || signed_le16(raw+3)!=-1) return ESP_FAIL;
    raw[15]^=1;
    if (c1_checksum(raw,17,&sum,&expected)) return ESP_FAIL;
    /* sum=65536 valid with zero checksum; sum=65537 must NOT wrap to FFFF. */
    memset(raw,0,sizeof(raw)); raw[1]=0xFF; raw[2]=0xFF; raw[3]=1;
    if (!c1_checksum(raw,17,&sum,&expected) || expected!=0) return ESP_FAIL;
    raw[3]=2; raw[15]=0xFF; raw[16]=0xFF;
    if (c1_checksum(raw,17,&sum,&expected) || expected!=UINT32_MAX) return ESP_FAIL;
    memset(raw,0,sizeof(raw)); raw[3]=0xFF; raw[4]=0xFF; raw[15]=1;
    if (c1_checksum(raw,17,&sum,&expected) || expected!=65537) return ESP_FAIL;
    const uint8_t mode_good[]={0,1,0,0xFF,0xFF}, mode_zero[5]={0};
    const uint8_t ack_zero[3]={0}, ack_echo[]={0,0x56,0xAA}, ack_bad[]={0,0x56,0};
    if (!mode1_readback_ok(mode_good,5) || mode1_readback_ok(mode_good,4) ||
        mode1_readback_ok(mode_zero,5) || !mode_commit_ok(ack_zero,3) ||
        !mode_commit_ok(ack_echo,3) || mode_commit_ok(ack_bad,3) ||
        mode_commit_ok(ack_echo,2)) return ESP_FAIL;
    const uint8_t id[] = {0,0,0x0A,0,0x96,0x60,0xFF};
    return id_checksum_ok(id,7) && temperature_counts(0xFFF0)==-1 ? ESP_OK : ESP_FAIL;
}
static void sample_c1(void) {
    uint8_t raw[17]; char hex[35];
    esp_err_t err=force_read(0xEE,raw,sizeof(raw));
    record_result(&s_fs,err);
    if (err!=ESP_OK) {
        ESP_LOGW(TAG,"C1 ms=%" PRId64 " seq=%" PRIu32 " valid=0 err=%s io_errors=%" PRIu32,
                 now_ms(),s_fs.attempts,esp_err_to_name(err),s_fs.errors);
        return;
    }
    hex_bytes(raw,sizeof(raw),hex);
    int32_t sum; uint32_t expected;
    bool valid=c1_checksum(raw,sizeof(raw),&sum,&expected);
    if (!valid) ++s_c1_checksum_errors;
    ESP_LOGI(TAG,"C1 ms=%" PRId64 " seq=%" PRIu32 " raw=%s checksum=%s sum=%" PRId32
             " expected=0x%08" PRIX32 " actual=0x%04X valid=%d",
             now_ms(),s_fs.attempts,hex,valid?"PASS":"FAIL",sum,expected,(unsigned)le16(raw+15),valid);
    if (valid) {
        ESP_LOGI(TAG,"C1_VALID extra=%u ch=%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32,
                 (unsigned)le16(raw+1),signed_le16(raw+3),signed_le16(raw+5),signed_le16(raw+7),
                 signed_le16(raw+9),signed_le16(raw+11),signed_le16(raw+13));
    }
}
static void sensor_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "AUTO Mode1 + C1 Force test; local MP28167 power setup; restore baseline=7bab7a9; raw counts only");
    ESP_ERROR_CHECK(decoder_selftest());
    ESP_LOGI(TAG, "TRACE: WR uses repeated START; combined return only; actual RX length unavailable; A5 changes are not a byte count");
    ESP_LOGI(TAG, "SELFTEST PASS: C1 checksum/bounds; mode1 readback/commit zero-or-echo/rejection");
    setup_bus();
    /* The existing BSP uses these global transport handles. One task owns all I/O. */
    bus_handle = s_bus;
    i2c_device_config_t haptic = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = HAPTIC_MOTOR_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &haptic, &dev_haptic_motor_handle));
    const i2c_master_bus_config_t sub = {
        .i2c_port = SUB_I2C_PORT, .sda_io_num = SUB_I2C_SDA, .scl_io_num = SUB_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = false,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&sub, &sub_bus_handle));
    haptic.device_address = MP28167_ADDR;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(sub_bus_handle, &haptic, &sub_dev_mp28167_handle));
    esp_err_t isr = gpio_install_isr_service(0);
    ESP_ERROR_CHECK(isr == ESP_ERR_INVALID_STATE ? ESP_OK : isr);
    uint8_t reg = 1, raw[2] = {0xA5, 0xA5};
    esp_err_t err = i2c_master_transmit_receive(s_temp, &reg, 1, raw, 2, IO_TIMEOUT_MS);
    if (err == ESP_OK) ESP_LOGI(TAG, "TEMP_CONFIG raw=%02X%02X", raw[0], raw[1]);
    else ESP_LOGW(TAG, "TEMP_CONFIG valid=0 err=%s", esp_err_to_name(err));
    start_haptic_comparison();
    if (!s_haptic_ready) {
        ESP_LOGE(TAG,"C1 test halted: Haptic precondition failed; no automatic retry");
        vTaskDelete(NULL);
        return;
    }
    read_identity();
    err=set_force_mode1();
    if (err!=ESP_OK) {
        ESP_LOGE(TAG,"MODE1_FAILED: bounded attempts exhausted; no EC/EE sampling");
        ESP_ERROR_CHECK(gpio_set_level(BOOST_EN_GPIO,0));
        vTaskDelete(NULL);
        return;
    }
    const uint8_t enter_c1[]={0xD0,0xEC,0x00,0xC1};
    err=traced_write(enter_c1,sizeof(enter_c1));
    if (err!=ESP_OK) {
        ESP_LOGE(TAG,"C1 entry failed: no EE reads, reset to retry");
        ESP_ERROR_CHECK(gpio_set_level(BOOST_EN_GPIO,0));
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG,"READY: C1 entered once; EE reads are 17 bytes; no EC10/ED operations");
    int64_t next_temp=0,next_sample=0,next_summary=now_ms()+10000;
    while (true) {
        if (surface_haptic_hw_process()!=BSP_STATUS_OK) {
            surface_haptic_hw_diagnostics(0);
            ESP_ERROR_CHECK(gpio_set_level(BOOST_EN_GPIO,0));
            ESP_LOGE(TAG,"HAPTIC_RUNTIME_FAILED: C1 test stopped");
            vTaskDelete(NULL);
            return;
        }
        if (now_ms()>=next_temp) { read_temperature(); next_temp=now_ms()+1000; }
        if (now_ms()>=next_sample) {
            int64_t started=now_ms(); sample_c1();
            next_sample=s_fs.consecutive>=3 ? now_ms()+1000 : started+100;
        }
        if (now_ms()>=next_summary) {
            ESP_LOGI(TAG,"SUMMARY ms=%" PRId64 " c1_n=%" PRIu32 " io_ok=%" PRIu32
                     " io_errors=%" PRIu32 " checksum_errors=%" PRIu32,
                     now_ms(),s_fs.attempts,s_fs.ok,s_fs.errors,s_c1_checksum_errors);
            next_summary=now_ms()+10000;
        }
        wait_ms(1);
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(xTaskCreate(sensor_task, "force_c1_test", 8192, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
