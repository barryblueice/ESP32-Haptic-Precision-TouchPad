#include "connection.h"
#include "connection_policy.h"
#include "device_config.h"
#include "input_pipeline.h"
#include "aux_output.h"
#include "GPIO/GPIO_handle.h"
#include "NVS/nvs_handle.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"

static SemaphoreHandle_t route_mutex;
static connection_policy_t policy;
static int route = -1;
static bool links[3], retiring;
static unsigned flights[3];
static uint32_t route_epoch = 1, sampled_at;
static uint8_t usb_mode = MOUSE_MODE;
static portMUX_TYPE event_lock = portMUX_INITIALIZER_UNLOCKED;
static bool config_wait, wake_event;
static RTC_NOINIT_ATTR struct { uint32_t magic, inverse; int mode; uint32_t high; } handoff;
#define HANDOFF_MAGIC 0x52535434U

void connection_lock(void) { if (route_mutex) xSemaphoreTake(route_mutex, portMAX_DELAY); }
void connection_unlock(void) { if (route_mutex) xSemaphoreGive(route_mutex); }
bool connection_selected(int transport) { return route_mutex ? route == transport : current_mode == transport; }
bool connection_can_send(int transport)
{
    return connection_selected(transport) && (!route_mutex || (links[transport] &&
        (transport != WIRED_MODE || gpio_get_level(VBUS_DET_GPIO))));
}
uint32_t connection_epoch(void) { return route_epoch; }
int connection_mode(void)
{
    connection_lock();
    int selected = policy.target >= 0 ? policy.target : current_mode;
    connection_unlock(); return selected;
}
void connection_flight(int transport, uint32_t epoch, bool submitted)
{
    if (epoch != route_epoch || !connection_selected(transport)) return;
    if (submitted) ++flights[transport];
    else if (flights[transport]) --flights[transport];
}
void connection_init(int boot)
{
    route_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(route_mutex ? ESP_OK : ESP_ERR_NO_MEM);
    bool manual = esp_reset_reason() == ESP_RST_SW && handoff.magic == HANDOFF_MAGIC &&
        handoff.inverse == ~HANDOFF_MAGIC && handoff.mode == boot && handoff.high <= 1;
    bool high = handoff.high == 1;
    handoff.magic = handoff.inverse = 0;
    connection_policy_init(&policy, boot, device_config_value(CFG_AUTO_SWITCH) != 0, manual, high);
    if (boot == BLE_MODE) route = BLE_MODE;
    sampled_at = (uint32_t)(esp_timer_get_time() / 1000) - 10U;
}
void connection_link(int transport, bool ready)
{
    /* Called with connection_lock held by the transport. */
    if (route_mutex && transport == WIRED_MODE && !gpio_get_level(VBUS_DET_GPIO)) ready = false;
    links[transport] = ready;
    if (connection_selected(transport)) {
        if (transport == WIRED_MODE) input_usb_link(ready);
        else input_set_link(ready ? (transport == BLE_MODE ? 1 : 3) : 0);
    }
    input_wake_parser();
}
void connection_usb_reset(void)
{
    links[WIRED_MODE] = false; usb_mode = MOUSE_MODE;
    flights[WIRED_MODE] = 0;
    if (connection_selected(WIRED_MODE)) { input_usb_reset(); aux_output_reset(false); }
    input_wake_parser();
}
void connection_usb_mode(uint8_t mode)
{
    usb_mode = mode;
    if (connection_selected(WIRED_MODE) && !retiring) input_request_mode(mode);
}
void connection_config_pending(void)
{ taskENTER_CRITICAL(&event_lock); config_wait = true; taskEXIT_CRITICAL(&event_lock); }
void connection_config_complete(void)
{
    taskENTER_CRITICAL(&event_lock); config_wait = false; taskEXIT_CRITICAL(&event_lock);
    input_wake_parser();
}
void connection_wake(void)
{
    taskENTER_CRITICAL(&event_lock); wake_event = true; taskEXIT_CRITICAL(&event_lock);
    input_wake_parser();
}
void connection_poll(void)
{
    if (!route_mutex) return;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if ((uint32_t)(now - sampled_at) < 10U) return;
    sampled_at = now;
    connection_lock();
    taskENTER_CRITICAL(&event_lock);
    bool wait = config_wait, wake = wake_event; wake_event = false;
    taskEXIT_CRITICAL(&event_lock);
    if (wake) connection_policy_wake(&policy);
    if (!wait) connection_policy_enable(&policy, device_config_value(CFG_AUTO_SWITCH) != 0);
    bool high = gpio_get_level(VBUS_DET_GPIO) != 0;
    if (!high && links[WIRED_MODE]) connection_link(WIRED_MODE, false);
    int target = connection_policy_sample(&policy, high, now);
    if (target >= 0 && (route != target || retiring)) {
        if (!retiring && route >= 0) {
            retiring = true;
            input_transport_quiesce();
            aux_output_cancel();
        }
        bool drained = route < 0 || !links[route] ||
            (!flights[route] && input_transport_drained() && aux_output_drained(route == _2_4_MODE));
        if (drained) {
            bool initial = route < 0;
            ++route_epoch;
            for (unsigned i = 0; i < 3; ++i) flights[i] = 0;
            route = target; retiring = false;
            aux_output_reset(route == WIRED_MODE && links[route]);
            input_transport_start(route, route == WIRED_MODE ? usb_mode : PTP_MODE,
                                  links[route], initial);
            ESP_LOGI("CONNECTION", "VBUS=%u target=%d ready=%u manual=%u epoch=%lu",
                policy.stable, route, links[route], policy.manual, (unsigned long)route_epoch);
        }
    }
    connection_unlock();
}
esp_err_t connection_manual_restart(int mode)
{
    if (mode < WIRED_MODE || mode > BLE_MODE) return ESP_ERR_INVALID_ARG;
    esp_err_t err = nvs_write_int("current_mode", mode);
    if (err != ESP_OK) return err;
    connection_lock();
    handoff.mode = mode;
    handoff.high = policy.valid ? policy.stable : gpio_get_level(VBUS_DET_GPIO) != 0;
    handoff.inverse = ~HANDOFF_MAGIC;
    handoff.magic = HANDOFF_MAGIC;
    connection_unlock();
    return ESP_OK;
}
