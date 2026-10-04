#include "force_forward.h"
#if CONFIG_SURFACE_FORCE_FORWARD_ENABLE
#include "force_forward_protocol.h"
#include "I2C/I2C_handle.h"
#include "I2C/TP/i2c_hid.h"
#include "I2C/SUB_DEV/cs40l25_surface.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <inttypes.h>

#define TAG "FORCE_FORWARD"
static i2c_master_dev_handle_t force_device, touchpad_device;
static force_forward_state_t state;
static portMUX_TYPE epoch_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t lifecycle_epoch;

typedef struct {
    const input_frame_t *frame;
    uint32_t epoch;
} forward_context_t;

void force_forward_invalidate(void)
{
    taskENTER_CRITICAL(&epoch_lock);
    ++lifecycle_epoch;
    taskEXIT_CRITICAL(&epoch_lock);
}

static uint32_t epoch_snapshot(void)
{
    taskENTER_CRITICAL(&epoch_lock);
    uint32_t epoch = lifecycle_epoch;
    taskEXIT_CRITICAL(&epoch_lock);
    return epoch;
}

static uint32_t now_ms(void *ctx)
{
    (void)ctx;
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool ready(void *arg)
{
    const forward_context_t *ctx = arg;
    /* No locks span a bus call. An already submitted transaction may finish,
     * but a lifecycle change cancels the remaining transactions in the sample. */
    return input_force_forward_ready(ctx->frame->generation) &&
        !tp_modern_sleep_is_active() && cs40l25_surface_is_ready() &&
        (uint32_t)(now_ms(NULL) - ctx->frame->time_ms) <= REPORT_MAX_AGE_MS &&
        epoch_snapshot() == ctx->epoch;
}

static int select_c1(void *ctx, const uint8_t *tx, size_t size)
{
    (void)ctx;
    return i2c_master_transmit(force_device, tx, size, FORCE_FORWARD_TIMEOUT_MS);
}

static int read_c1(void *ctx, const uint8_t *tx, size_t size, uint8_t *rx, size_t count)
{
    (void)ctx;
    return i2c_master_transmit_receive(force_device, tx, size, rx, count, FORCE_FORWARD_TIMEOUT_MS);
}

static int forward(void *ctx, const uint8_t *tx, size_t size)
{
    (void)ctx;
    return i2c_master_transmit(touchpad_device, tx, size, FORCE_FORWARD_TIMEOUT_MS);
}

void force_forward_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t touchpad)
{
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x49,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &config, &force_device);
    if (err != ESP_OK) {
        force_device = NULL;
        ESP_LOGW(TAG, "Disabled: device registration failed: %s", esp_err_to_name(err));
        return;
    }
    touchpad_device = touchpad;
    ESP_LOGI(TAG, "0x49 C1 -> 0x2C enabled, qualified_keystroke=0, timeout=10 ms, error backoff=100 ms");
}

void force_forward_report(const input_frame_t *frame)
{
    if (!force_device) return;
    forward_context_t context = {.frame = frame, .epoch = epoch_snapshot()};
    const force_forward_io_t io = {&context, select_c1, read_c1, forward, now_ms, ready};
    force_forward_result_t result = force_forward_run(&state, &io, frame->bytes, sizeof(frame->bytes), context.epoch);
    static uint32_t logged_at;
    static bool logged_error;
    if (result >= FORCE_FORWARD_READ_ERROR && (!logged_error || (uint32_t)(now_ms(NULL) - logged_at) >= 5000U)) {
        logged_error = true;
        logged_at = now_ms(NULL);
        const char *stage = result == FORCE_FORWARD_READ_ERROR ? "read/select" :
                            result == FORCE_FORWARD_CHECKSUM_ERROR ? "checksum" : "forward";
        ESP_LOGW(TAG, "%s failed (io=%s): sent=%" PRIu32 " read=%" PRIu32 " checksum=%" PRIu32 " forward=%" PRIu32,
                 stage, esp_err_to_name(state.last_error), state.sent, state.read_failures,
                 state.checksum_failures, state.forward_failures);
    }
}
#endif
