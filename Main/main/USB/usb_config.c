#include "usb_config.h"
#include "SYS/device_config.h"
#include "SYS/input_pipeline.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "tusb.h"
#include "esp_system.h"
#include "esp_log.h"
#include <string.h>

typedef struct { uint8_t data[64], legacy_id; uint16_t size; uint32_t epoch; } config_command_t;
static QueueHandle_t commands;
static TaskHandle_t worker;
static portMUX_TYPE tx_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t response[64];
static bool pending, busy, completed;
static uint32_t epoch;
extern void enter_dfu_mode(void);

void usb_config_receive(const uint8_t *data, uint16_t size)
{
    if (!commands || !data || size < 12) return;
    config_command_t cmd = {.size = size};
    memcpy(cmd.data, data, size < 64 ? size : 64);
    taskENTER_CRITICAL(&tx_lock); cmd.epoch = epoch; taskEXIT_CRITICAL(&tx_lock);
    (void)xQueueSend(commands, &cmd, 0);
}
void usb_config_legacy(uint8_t id, uint8_t value)
{
    if (!commands) return;
    config_command_t cmd = {.legacy_id = id, .data = {value}};
    taskENTER_CRITICAL(&tx_lock); cmd.epoch = epoch; taskEXIT_CRITICAL(&tx_lock);
    (void)xQueueSend(commands, &cmd, 0);
}
void usb_config_dfu(void) { usb_config_legacy(0xff, 0); }
static void config_task(void *arg)
{
    (void)arg;
    config_command_t cmd;
    while (true) {
        if (xQueueReceive(commands, &cmd, portMAX_DELAY) != pdTRUE) continue;
        taskENTER_CRITICAL(&tx_lock); bool current = cmd.epoch == epoch; taskEXIT_CRITICAL(&tx_lock);
        if (!current) continue;
        if (cmd.legacy_id) {
            if (cmd.legacy_id == 0xff) enter_dfu_mode();
            else {
                esp_err_t err = device_config_set_legacy(cmd.legacy_id == 0x40 ? CFG_LEVEL : CFG_INTENSITY, cmd.data[0], true);
                if (err != ESP_OK) ESP_LOGW("RSTP", "Feature rejected: %s", esp_err_to_name(err));
            }
            continue;
        }
        rstp_request_t request;
        if (!rstp_decode(cmd.data, cmd.size, &request)) continue;
        uint8_t payload[32] = {0}; uint16_t size = 0, status = request.status;
        if (!status) switch (request.command) {
        case RSTP_INFO:
            rstp_put32(payload, device_config_capabilities()); payload[4] = 1; payload[6] = 1; payload[10] = 1; size = 12; break;
        case RSTP_READ: {
            device_config_t config; device_config_get(&config); memcpy(payload, config.bytes, 32); size = 32; break;
        }
        case RSTP_WRITE: status = device_config_save(&request.config); break;
        }
        taskENTER_CRITICAL(&tx_lock);
        current = cmd.epoch == epoch;
        if (current) { rstp_response(response, &request, status, payload, size); pending = true; completed = false; }
        taskEXIT_CRITICAL(&tx_lock);
        if (!current) continue;
        input_wake_sender();
        while (true) {
            taskENTER_CRITICAL(&tx_lock);
            current = cmd.epoch == epoch; bool done = completed;
            taskEXIT_CRITICAL(&tx_lock);
            if (!current) break;
            if (done) {
                if (status == RSTP_RESTART) {
                    tud_disconnect();
                    vTaskDelay(pdMS_TO_TICKS(50));
                    esp_restart();
                }
                break;
            }
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
    }
}
esp_err_t usb_config_init(void)
{
    commands = xQueueCreate(8, sizeof(config_command_t));
    if (!commands) return ESP_ERR_NO_MEM;
    return xTaskCreate(config_task, "rstp_config", 4096, NULL, 5, &worker) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
void usb_config_send(void)
{
    taskENTER_CRITICAL(&tx_lock);
    bool send = pending && !busy;
    if (send) busy = true;
    taskEXIT_CRITICAL(&tx_lock);
    if (!send) return;
    if (!tud_hid_n_ready(0) || !tud_hid_n_report(0, 0, response, 64)) {
        taskENTER_CRITICAL(&tx_lock); busy = false; taskEXIT_CRITICAL(&tx_lock);
    }
}
void usb_config_complete(bool success)
{
    taskENTER_CRITICAL(&tx_lock);
    if (busy) { busy = false; if (success) { pending = false; completed = true; } }
    taskEXIT_CRITICAL(&tx_lock);
    if (worker) xTaskNotifyGive(worker);
    input_wake_sender();
}
void usb_config_detach(void)
{
    taskENTER_CRITICAL(&tx_lock); ++epoch; pending = busy = completed = false; taskEXIT_CRITICAL(&tx_lock);
    if (worker) xTaskNotifyGive(worker);
}
bool usb_config_active(void)
{
    taskENTER_CRITICAL(&tx_lock); bool active = pending || busy; taskEXIT_CRITICAL(&tx_lock);
    return active;
}
