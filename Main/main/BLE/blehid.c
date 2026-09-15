#include "esp_timer.h"
#include "BLE/ble_hid_dev.h"
#include "BLE/BLE_bluedroid.h"
#include "SYS/input_pipeline.h"
#include "SYS/aux_output.h"
#include "sdkconfig.h"

static portMUX_TYPE ble_tx_lock = portMUX_INITIALIZER_UNLOCKED;
static bool connected, subscribed, congested, flight, done, success;
static uint8_t aux_subscriptions;
static uint16_t active_conn, mtu = 23, flight_handle;

static bool ready_locked(void)
{
#if CONFIG_BLE_ENABLE_PTP_MODE
    return connected && subscribed && mtu >= sizeof(ptp_report_t) + 3 && aux_subscriptions == 7;
#else
    return connected && subscribed;
#endif
}
static void update_link(void)
{
    taskENTER_CRITICAL(&ble_tx_lock); bool ready = ready_locked(); taskEXIT_CRITICAL(&ble_tx_lock);
#if CONFIG_BLE_ENABLE_PTP_MODE
    input_set_link(ready ? (1U << PTP_MODE) : 0);
#else
    input_set_link(ready ? (1U << MOUSE_MODE) : 0);
#endif
    input_wake_sender();
}
void ble_input_connection(bool up, uint16_t conn)
{
    taskENTER_CRITICAL(&ble_tx_lock);
    if (!up && connected && active_conn != conn) { taskEXIT_CRITICAL(&ble_tx_lock); return; }
    connected = up; subscribed = congested = flight = done = false;
    aux_subscriptions = 0; mtu = 23; active_conn = conn;
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
void ble_input_aux_subscription(uint16_t conn, unsigned index, bool enabled)
{
    taskENTER_CRITICAL(&ble_tx_lock);
    if (connected && active_conn == conn && index < 3) {
        if (enabled) aux_subscriptions |= 1U << index;
        else aux_subscriptions &= ~(1U << index);
    }
    taskEXIT_CRITICAL(&ble_tx_lock); update_link();
}
void ble_input_mtu(uint16_t conn, uint16_t value)
{
    taskENTER_CRITICAL(&ble_tx_lock);
    if (connected && active_conn == conn) mtu = value;
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
    aux_output_report_t aux = {0};
    bool have_pending = false, auxiliary = false;
#if CONFIG_BLE_ENABLE_PTP_MODE
    bool prefer_aux = true;
#endif
    while (true) {
        input_log_stats();
        taskENTER_CRITICAL(&ble_tx_lock);
        bool ready = ready_locked() && !congested, completed = flight && done, ok = success;
        bool in_flight = flight;
        uint16_t conn = active_conn;
        if (completed) { flight = done = false; in_flight = false; }
        taskEXIT_CRITICAL(&ble_tx_lock);
        if (completed) {
            if (auxiliary) aux_output_complete(ok);
            else { if (ok) input_report_ack(&pending); else input_recover(); have_pending = false; }
            if (!ok) input_submit_failed();
        }
        if (have_pending && !in_flight && !input_report_current(&pending)) have_pending = false;
        if (!in_flight && ready) {
            if (!have_pending) have_pending = input_take_report(&pending);
            auxiliary = false;
#if CONFIG_BLE_ENABLE_PTP_MODE
            if (aux_output_release_pending() || prefer_aux || !have_pending)
                auxiliary = aux_output_take(&aux, input_generation(), (uint32_t)(esp_timer_get_time() / 1000));
#endif
            if (auxiliary || have_pending) {
                uint8_t id = auxiliary ? aux.id : pending.mode == PTP_MODE ? HID_RPT_ID_PTP_IN : HID_RPT_ID_MOUSE_IN;
                uint16_t handle = hid_dev_report_handle(id);
                uint8_t length = auxiliary ? aux.length : pending.mode == PTP_MODE ? sizeof(ptp_report_t) : sizeof(mouse_hid_report_t);
                uint8_t *data = auxiliary ? aux.data : (uint8_t *)&pending.data;
                bool current = auxiliary ? aux_output_report_current(&aux) : input_report_current(&pending);
                taskENTER_CRITICAL(&ble_tx_lock);
                bool submit = current && connected && conn == active_conn && ready_locked() && !congested && handle;
                if (submit) { flight = true; done = false; flight_handle = handle; }
                taskEXIT_CRITICAL(&ble_tx_lock);
                esp_err_t err = submit ? hid_dev_send_report(hidd_le_env.gatt_if, conn, id, HID_REPORT_TYPE_INPUT, length, data) : ESP_FAIL;
                if (err != ESP_OK) {
                    taskENTER_CRITICAL(&ble_tx_lock); flight = done = false; taskEXIT_CRITICAL(&ble_tx_lock);
                    if (auxiliary) aux_output_unsubmitted();
                    input_submit_failed();
                }
#if CONFIG_BLE_ENABLE_PTP_MODE
                else prefer_aux = !auxiliary;
#endif
            }
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
    }
}
