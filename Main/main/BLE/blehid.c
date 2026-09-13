#include "BLE/ble_hid_dev.h"
#include "BLE/BLE_bluedroid.h"
#include "SYS/input_pipeline.h"
#include "sdkconfig.h"

static portMUX_TYPE ble_tx_lock = portMUX_INITIALIZER_UNLOCKED;
static bool connected, subscribed, congested;
static uint16_t active_conn;

static void update_link(void)
{
    taskENTER_CRITICAL(&ble_tx_lock);
    bool ready = connected && subscribed;
    taskEXIT_CRITICAL(&ble_tx_lock);
#if CONFIG_BLE_ENABLE_PTP_MODE
    input_set_link(ready ? (1U << PTP_MODE) : 0);
#else
    input_set_link(ready ? (1U << MOUSE_MODE) : 0);
#endif
}
void ble_input_connection(bool up, uint16_t conn)
{
    taskENTER_CRITICAL(&ble_tx_lock);
    if (!up && connected && active_conn != conn) {
        taskEXIT_CRITICAL(&ble_tx_lock);
        return;
    }
    connected = up; subscribed = congested = false; active_conn = conn;
    taskEXIT_CRITICAL(&ble_tx_lock);
    update_link();
}
void ble_input_subscription(uint16_t conn, bool enabled)
{
    taskENTER_CRITICAL(&ble_tx_lock);
    if (connected && active_conn == conn) subscribed = enabled;
    taskEXIT_CRITICAL(&ble_tx_lock);
    update_link();
}
void ble_input_congestion(uint16_t conn, bool busy)
{
    taskENTER_CRITICAL(&ble_tx_lock);
    if (connected && active_conn == conn) congested = busy;
    taskEXIT_CRITICAL(&ble_tx_lock);
    input_wake_sender();
}

void ble_hid_task(void *arg)
{
    (void)arg;
    input_register_sender();
    input_report_t pending;
    bool have_pending = false;
    while (true) {
        input_log_stats();
        if (have_pending && !input_report_current(&pending)) have_pending = false;
        taskENTER_CRITICAL(&ble_tx_lock);
        bool ready = connected && subscribed && !congested;
        uint16_t conn = active_conn;
        taskEXIT_CRITICAL(&ble_tx_lock);
        if (!have_pending) have_pending = input_take_report(&pending);
        if (have_pending && ready && input_report_current(&pending)) {
            esp_err_t err = pending.mode == PTP_MODE ?
                hid_dev_send_report(hidd_le_env.gatt_if, conn, HID_RPT_ID_PTP_IN,
                    HID_REPORT_TYPE_INPUT, sizeof(ptp_report_t), (uint8_t *)&pending.data.ptp) :
                hid_dev_send_report(hidd_le_env.gatt_if, conn, HID_RPT_ID_MOUSE_IN,
                    HID_REPORT_TYPE_INPUT, sizeof(mouse_hid_report_t), (uint8_t *)&pending.data.mouse);
            if (err == ESP_OK) { input_report_ack(&pending); have_pending = false; continue; }
            else { input_submit_failed(); vTaskDelay(1); }
        }
        /* A successful synchronous submit may leave more reports to drain. */
        ulTaskNotifyTake(pdTRUE, have_pending ? 1 : portMAX_DELAY);
    }
}
