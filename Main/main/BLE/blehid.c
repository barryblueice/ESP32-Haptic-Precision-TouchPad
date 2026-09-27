#include "BLE/ble_hid.h"
#include "SYS/input_pipeline.h"
#include "SYS/aux_output.h"

static portMUX_TYPE ble_tx_lock = portMUX_INITIALIZER_UNLOCKED;
static bool connected, subscribed, congested, flight, done;
static ble_tx_result_t result;
static uint16_t active_conn, flight_handle;
static uint32_t active_epoch;

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
uint32_t ble_input_connection(bool up, uint16_t conn)
{
    taskENTER_CRITICAL(&ble_tx_lock);
    if (!up && connected && active_conn != conn) {
        uint32_t epoch = active_epoch;
        taskEXIT_CRITICAL(&ble_tx_lock);
        return epoch;
    }
    connected = up; subscribed = congested = flight = done = false;
    active_conn = conn;
    uint32_t epoch = ++active_epoch;
    taskEXIT_CRITICAL(&ble_tx_lock);
    aux_output_reset(up);
    update_link();
    return epoch;
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
void ble_input_complete(uint16_t conn, uint32_t epoch, uint16_t handle, ble_tx_result_t status)
{
    taskENTER_CRITICAL(&ble_tx_lock);
    if (connected && active_conn == conn && active_epoch == epoch && flight && !done && handle == flight_handle) {
        done = true; result = status;
    }
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
        bool ready = ready_locked() && !congested, completed = flight && done;
        ble_tx_result_t status = result;
        bool in_flight = flight;
        uint16_t conn = active_conn;
        uint32_t epoch = active_epoch;
        if (completed) { flight = done = false; in_flight = false; }
        taskEXIT_CRITICAL(&ble_tx_lock);
        if (completed) {
            if (status == BLE_TX_OK) input_report_ack(&pending);
            else if (status == BLE_TX_FAILED && input_report_current(&pending)) input_recover();
            if (status != BLE_TX_RETRY) have_pending = false;
            if (status != BLE_TX_OK) input_submit_failed();
        }
        if (have_pending && !in_flight && !input_report_current(&pending)) have_pending = false;
        if (!in_flight && ready) {
            if (!have_pending) have_pending = input_take_report(&pending);
            if (have_pending) {
                uint8_t id = HID_RPT_ID_MOUSE_IN;
                uint16_t handle = hid_dev_report_handle(id);
                bool current = input_report_current(&pending);
                taskENTER_CRITICAL(&ble_tx_lock);
                bool submit = current && connected && conn == active_conn && epoch == active_epoch && ready_locked() && !congested && handle;
                if (submit) { flight = true; done = false; flight_handle = handle; }
                taskEXIT_CRITICAL(&ble_tx_lock);
                esp_err_t err = submit ? ble_hid_send_mouse(conn, epoch, &pending) : ESP_FAIL;
                if (err != ESP_OK) {
                    taskENTER_CRITICAL(&ble_tx_lock);
                    if (epoch == active_epoch) flight = done = false;
                    taskEXIT_CRITICAL(&ble_tx_lock);
                    input_submit_failed();
                }
            }
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
    }
}
