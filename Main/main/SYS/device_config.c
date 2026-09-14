#include "device_config.h"
#include "input_pipeline.h"
#include "I2C/TP/i2c_hid.h"
#include "I2C/SUB_DEV/cs40l25_surface.h"
#include "NVS/nvs_handle.h"
#include "nvs.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <string.h>

static device_config_t active, pending;
static portMUX_TYPE config_lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t writer, applied;
static bool initialized, pending_apply, pending_restart, halted, saved_restart;
static uint32_t capabilities = 0xff;

static esp_err_t store_config(const device_config_t *c)
{
    uint8_t record[40] = {'R','S','C','F',1,0,32,0};
    memcpy(record + 8, c->bytes, 32);
    nvs_handle_t h;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, "rstp_config", record, sizeof(record));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
static void mirror(const device_config_t *c)
{
    ptp_button_press_threshold = c->bytes[1];
    click_light_weight_threshold = c->bytes[2];
    click_midium_weight_threshold = c->bytes[3];
    click_strong_weight_threshold = c->bytes[4];
}
static int32_t legacy(const char *key, int32_t fallback, int32_t min, int32_t max)
{
    int32_t v;
    return nvs_read_int(key, &v) == ESP_OK && v >= min && v <= max ? v : fallback;
}
static uint8_t import_intensity(void)
{
    static const uint8_t migrated[] = {0,25,63,75,100};
    int32_t value;
    esp_err_t err = nvs_read_int("haptic_surface", &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) return migrated[legacy("haptic_click", 2, 0, 4)];
    /* An existing but corrupt new key must not resurrect an obsolete old value. */
    return err == ESP_OK && value >= 0 && value <= 100 ? value : 63;
}
esp_err_t device_config_init(void)
{
    writer = xSemaphoreCreateMutex(); applied = xSemaphoreCreateBinary();
    if (!writer || !applied) return ESP_ERR_NO_MEM;
    device_config_defaults(&active);
#if CONFIG_TP_ROTATION_PORTRAIT
    active.bytes[5] = 1;
#elif CONFIG_TP_ROTATION_LANDSCAPE_FLIPPED
    active.bytes[5] = 2;
#elif CONFIG_TP_ROTATION_PORTRAIT_FLIPPED
    active.bytes[5] = 3;
#endif
#if !CONFIG_TP_ENABLE_SLEEP_MODE
    active.bytes[6] = 0;
#endif
#ifdef CONFIG_TP_SLEEP_MODE_TIME_MS
    uint32_t timeout = CONFIG_TP_SLEEP_MODE_TIME_MS;
    if (timeout >= 1000 && timeout <= 3600000 && timeout % 1000 == 0) rstp_put32(active.bytes + 8, timeout);
#endif
    nvs_handle_t h;
    uint8_t record[40]; size_t size = sizeof(record);
    esp_err_t err = nvs_open("storage", NVS_READONLY, &h);
    if (err == ESP_OK) { err = nvs_get_blob(h, "rstp_config", record, &size); nvs_close(h); }
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        active.bytes[0] = import_intensity();
        int32_t level = 2;
        if (nvs_read_int("btn_press_th", &level) != ESP_OK) level = 2;
        active.bytes[1] = level < 1 ? 1 : level > 3 ? 3 : level;
        active.bytes[2] = legacy("clk_th_l", 80, 1, 255);
        active.bytes[3] = legacy("clk_th_m", 100, 1, 255);
        active.bytes[4] = legacy("clk_th_s", 130, 1, 255);
        if (active.bytes[3] < active.bytes[2]) active.bytes[3] = active.bytes[2];
        if (active.bytes[4] < active.bytes[3]) active.bytes[4] = active.bytes[3];
        err = store_config(&active);
        if (err != ESP_OK) ESP_LOGW("CONFIG", "Migration save failed: %s", esp_err_to_name(err));
    } else if (err == ESP_OK && size == 40 && !memcmp(record, "RSCF\1\0\40\0", 8)) {
        device_config_t loaded; memcpy(loaded.bytes, record + 8, 32);
        if (device_config_valid(&loaded)) active = loaded;
        else ESP_LOGW("CONFIG", "Invalid configuration retained; using defaults");
    } else ESP_LOGW("CONFIG", "Unreadable/unknown configuration retained; using defaults (%s)", esp_err_to_name(err));
    mirror(&active); initialized = true;
    return ESP_OK;
}
bool device_config_ready(void) { return initialized; }
void device_config_get(device_config_t *out)
{ taskENTER_CRITICAL(&config_lock); *out = active; taskEXIT_CRITICAL(&config_lock); }
uint8_t device_config_value(unsigned offset)
{ device_config_t c; device_config_get(&c); return offset < 32 ? c.bytes[offset] : 0; }
uint32_t device_config_capabilities(void)
{
    taskENTER_CRITICAL(&config_lock); uint32_t c = capabilities; taskEXIT_CRITICAL(&config_lock);
    if (cs40l25_surface_get_state() == SURFACE_FAULT) c &= ~((1U << 0) | (1U << 4));
    if (!(c & RSTP_CAP_EDGES)) c &= ~(RSTP_CAP_ARROW_KEYS | RSTP_CAP_EDGE_REPEAT);
    return c;
}
void device_config_disable(uint32_t caps)
{ taskENTER_CRITICAL(&config_lock); capabilities &= ~caps; taskEXIT_CRITICAL(&config_lock); }
static void apply_at_boundary(const device_config_t *c, bool restart)
{
    taskENTER_CRITICAL(&config_lock);
    pending = *c; pending_restart = restart; pending_apply = true;
    taskEXIT_CRITICAL(&config_lock);
    input_wake_parser();
    xSemaphoreTake(applied, portMAX_DELAY);
}
uint16_t device_config_save(const device_config_t *c)
{
    if (!device_config_valid(c)) return RSTP_INVALID;
    if (!initialized || xSemaphoreTake(writer, 0) != pdTRUE) return RSTP_BUSY;
    device_config_t old; device_config_get(&old);
    uint16_t status = RSTP_RESTART;
    if (saved_restart) status = RSTP_BUSY;
    else if (!device_config_supported(&old, c, device_config_capabilities())) status = RSTP_UNSUPPORTED;
    else if (store_config(c) != ESP_OK) status = RSTP_STORAGE;
    else { saved_restart = true; apply_at_boundary(c, true); }
    xSemaphoreGive(writer);
    return status;
}
esp_err_t device_config_set_legacy(unsigned offset, uint8_t value, bool persist)
{
    if ((offset != 0 && offset != 1) || (offset == 0 ? value > 100 : value < 1 || value > 3)) return ESP_ERR_INVALID_ARG;
    if (!initialized || xSemaphoreTake(writer, 0) != pdTRUE) return ESP_ERR_INVALID_STATE;
    device_config_t c; device_config_get(&c); c.bytes[offset] = value;
    esp_err_t err = saved_restart || !(device_config_capabilities() & (1U << offset)) ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (err == ESP_OK && persist) err = store_config(&c);
    if (err == ESP_OK) apply_at_boundary(&c, false);
    xSemaphoreGive(writer); return err;
}
bool device_config_parser_boundary(void)
{
    taskENTER_CRITICAL(&config_lock);
    bool change = pending_apply;
    if (change) {
        if (pending_restart) halted = true;
        else { active = pending; mirror(&active); }
        pending_apply = false;
    }
    bool stop = halted;
    taskEXIT_CRITICAL(&config_lock);
    if (change) { input_recover(); xSemaphoreGive(applied); }
    return stop;
}
uint8_t device_config_rotation(void)
{
    /* Wireless descriptors retain their compile-time orientation. */
    if (current_mode == WIRED_MODE && initialized) return device_config_value(CFG_ROTATION);
#if CONFIG_TP_ROTATION_PORTRAIT
    return 1;
#elif CONFIG_TP_ROTATION_LANDSCAPE_FLIPPED
    return 2;
#elif CONFIG_TP_ROTATION_PORTRAIT_FLIPPED
    return 3;
#else
    return 0;
#endif
}
uint16_t device_config_x_max(void) { return device_config_rotation() & 1 ? 1532 : 2302; }
uint16_t device_config_y_max(void) { return device_config_rotation() & 1 ? 2302 : 1532; }
