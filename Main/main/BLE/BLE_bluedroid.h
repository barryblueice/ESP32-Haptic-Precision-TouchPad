#ifndef BLE_BLUEDROID_H
#define BLE_BLUEDROID_H

#define REPORTID_HAPTIC_INTENSITY 0x41

void ble_bluedroid_init();
void hidd_le_prepare_gatt_table();
void ble_hid_task(void *arg);
void battery_ble_notify_task(void *pvParameters);

extern uint16_t ble_conn_id;

#endif /* BLE_BLUEDROID_H */
