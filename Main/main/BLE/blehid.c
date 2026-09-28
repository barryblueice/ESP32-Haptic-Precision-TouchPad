#include "BLE/ble_hid.h"
#include "SYS/input_pipeline.h"
#include "SYS/aux_output.h"
#include "SYS/connection.h"
#include "esp_timer.h"

static portMUX_TYPE ble_tx_lock = portMUX_INITIALIZER_UNLOCKED;
static bool connected, subscribed, congested, flight, done;
static ble_tx_result_t result;
static uint16_t active_conn, flight_handle;
static uint32_t active_epoch;
static uint8_t auxiliary_ready;
static bool flight_auxiliary;

_Static_assert(sizeof(mouse_hid_report_t) == 5, "BLE mouse payload must be 5 bytes");

static bool ready_locked(void)
{
    return connected && subscribed;
}
static void update_link(void)
{
    taskENTER_CRITICAL(&ble_tx_lock); bool ready = ready_locked(); taskEXIT_CRITICAL(&ble_tx_lock);
    connection_lock();
    connection_link(BLE_MODE, ready);
    connection_unlock();
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
    auxiliary_ready = 0;
    active_conn = conn;
    uint32_t epoch = ++active_epoch;
    taskEXIT_CRITICAL(&ble_tx_lock);
    aux_output_reset(up);
    aux_output_set_ready(0);
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
void ble_input_aux_subscription(uint16_t conn, uint8_t mask)
{
    taskENTER_CRITICAL(&ble_tx_lock);
    bool current = connected && active_conn == conn;
    if (current) auxiliary_ready = mask & AUX_OUTPUT_ALL;
    taskEXIT_CRITICAL(&ble_tx_lock);
    if (current) aux_output_set_ready(mask);
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
    (void)arg; input_register_transport_sender(BLE_MODE);
    input_report_t pending = {0};
    aux_output_report_t auxiliary = {0};
    bool have_pending = false, have_auxiliary = false, prefer_auxiliary = true;
    uint32_t sender_epoch = 0;
    while (true) {
        input_log_stats();
        taskENTER_CRITICAL(&ble_tx_lock);
        bool ready = ready_locked() && !congested, completed = flight && done;
        ble_tx_result_t status = result;
        bool in_flight = flight;
        bool completed_auxiliary = flight_auxiliary;
        uint8_t mask = congested ? 0 : auxiliary_ready;
        uint16_t conn = active_conn;
        uint32_t epoch = active_epoch;
        if (completed) { flight = done = false; in_flight = false; }
        taskEXIT_CRITICAL(&ble_tx_lock);
        if (sender_epoch != epoch) {
            have_pending = have_auxiliary = false;
            sender_epoch = epoch;
        }
        if (completed) {
            if (completed_auxiliary) {
                if (status != BLE_TX_RETRY) {
                    aux_output_complete(status == BLE_TX_OK);
                    have_auxiliary = false;
                }
            } else {
                if (status == BLE_TX_OK) input_report_ack(&pending);
                else if (status == BLE_TX_FAILED && input_report_current(&pending)) input_recover();
                if (status != BLE_TX_RETRY) have_pending = false;
            }
            if (status != BLE_TX_OK) input_submit_failed();
        }
        if (have_pending && !in_flight && !input_report_current(&pending)) have_pending = false;
        if (have_auxiliary && !in_flight &&
            (!aux_output_report_current(&auxiliary) || !(mask & aux_output_report_mask(auxiliary.id)))) {
            aux_output_unsubmitted();
            have_auxiliary = false;
        }
        if (!in_flight) {
            if (ready && !have_pending) have_pending = input_take_report(&pending);
            bool choose_auxiliary = have_auxiliary || aux_output_release_pending_ready(mask) ||
                prefer_auxiliary || !have_pending;
            if (choose_auxiliary && !have_auxiliary && mask)
                have_auxiliary = aux_output_take_ready(&auxiliary, input_generation(), esp_timer_get_time() / 1000, mask);
            choose_auxiliary = choose_auxiliary && have_auxiliary;
            if (choose_auxiliary || (ready && have_pending)) {
                uint8_t id = choose_auxiliary && auxiliary.id != 2 ? auxiliary.id : HID_RPT_ID_MOUSE_IN;
                uint16_t handle = hid_dev_report_handle(id);
                bool current = choose_auxiliary ? aux_output_report_current(&auxiliary) : input_report_current(&pending);
                taskENTER_CRITICAL(&ble_tx_lock);
                bool channel_ready = choose_auxiliary ?
                    (auxiliary_ready & aux_output_report_mask(auxiliary.id)) != 0 : ready_locked();
                bool submit = current && connected && conn == active_conn && epoch == active_epoch && channel_ready && !congested && handle;
                if (submit) { flight = true; done = false; flight_handle = handle; flight_auxiliary = choose_auxiliary; }
                taskEXIT_CRITICAL(&ble_tx_lock);
                esp_err_t err = !submit ? ESP_FAIL : choose_auxiliary ?
                    ble_hid_send_aux(conn, epoch, &auxiliary) : ble_hid_send_mouse(conn, epoch, &pending);
                if (err == ESP_OK) prefer_auxiliary = !choose_auxiliary;
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
