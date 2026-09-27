#include "BLE/ble_hid_dev.h"
#include "BLE/BLE_bluedroid.h"
#include "SYS/input_pipeline.h"
#include "SYS/aux_output.h"

static portMUX_TYPE ble_tx_lock = portMUX_INITIALIZER_UNLOCKED;
static bool connected, subscribed, congested, flight, done, success;
static uint16_t active_conn, flight_handle;

_Static_assert(sizeof(mouse_hid_report_t) == 5, "BLE mouse payload must be 5 bytes");

static bool ready_locked(void)
{
    return connected && subscribed;
}
static void update_link(void)
{
    taskENTER_CRITICAL(&ble_tx_lock); bool ready = ready_locked(); taskEXIT_CRITICAL(&ble_tx_lock);
    input_set_link(ready ? (1U << MOUSE_MODE) : 0);
    input_wake_sender();
}
void ble_input_connection(bool up, uint16_t conn)
{
    taskENTER_CRITICAL(&ble_tx_lock);
    if (!up && connected && active_conn != conn) { taskEXIT_CRITICAL(&ble_tx_lock); return; }
    connected = up; subscribed = congested = flight = done = false;
    active_conn = conn;
    taskEXIT_CRITICAL(&ble_tx_lock);
    aux_output_reset(up);
    update_link();
}
void ble_input_subscription(uint16_t conn, bool enabled)
{
    taskENTER_CRITICAL(&ble_tx_lock);
    if (connected && active_conn == conn) subscribed = enabled;
    taskEXIT_CRITICAL(&ble_tx_lock); update_link();
}
void ble_input_congestion(uint16_t conn, bool busy)
{
    taskENTER_CRITICAL(&ble_tx_lock);
    if (connected && active_conn == conn) congested = busy;
    taskEXIT_CRITICAL(&ble_tx_lock); input_wake_sender();
}
void ble_input_complete(uint16_t conn, uint16_t handle, bool ok)
{
    taskENTER_CRITICAL(&ble_tx_lock);
    if (connected && active_conn == conn && flight && handle == flight_handle) { done = true; success = ok; }
    taskEXIT_CRITICAL(&ble_tx_lock); input_wake_sender();
}
void ble_hid_task(void *arg)
{
    (void)arg; input_register_sender();
    input_report_t pending = {0};
    bool have_pending = false;
    while (true) {
        input_log_stats();
        taskENTER_CRITICAL(&ble_tx_lock);
        bool ready = ready_locked() && !congested, completed = flight && done, ok = success;
        bool in_flight = flight;
        uint16_t conn = active_conn;
        if (completed) { flight = done = false; in_flight = false; }
        taskEXIT_CRITICAL(&ble_tx_lock);
        if (completed) {
            if (ok) input_report_ack(&pending); else input_recover();
            have_pending = false;
            if (!ok) input_submit_failed();
        }
        if (have_pending && !in_flight && !input_report_current(&pending)) have_pending = false;
        if (!in_flight && ready) {
            if (!have_pending) have_pending = input_take_report(&pending);
            if (have_pending) {
                uint8_t id = HID_RPT_ID_MOUSE_IN;
                uint16_t handle = hid_dev_report_handle(id);
                uint8_t length = sizeof(mouse_hid_report_t);
                uint8_t *data = (uint8_t *)&pending.data.mouse;
                bool current = input_report_current(&pending);
                taskENTER_CRITICAL(&ble_tx_lock);
                bool submit = current && connected && conn == active_conn && ready_locked() && !congested && handle;
                if (submit) { flight = true; done = false; flight_handle = handle; }
                taskEXIT_CRITICAL(&ble_tx_lock);
                esp_err_t err = submit ? hid_dev_send_report(hidd_le_env.gatt_if, conn, id, HID_REPORT_TYPE_INPUT, length, data) : ESP_FAIL;
                if (err != ESP_OK) {
                    taskENTER_CRITICAL(&ble_tx_lock); flight = done = false; taskEXIT_CRITICAL(&ble_tx_lock);
                    input_submit_failed();
                }
            }
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
    }
}
