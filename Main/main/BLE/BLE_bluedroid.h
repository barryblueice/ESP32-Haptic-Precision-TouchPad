#ifndef BLE_BLUEDROID_H
#define BLE_BLUEDROID_H

void ble_bluedroid_init();
void hidd_le_prepare_gatt_table();
void ble_hid_task(void *arg);

extern uint8_t hid_conn_id;

#endif /* BLE_BLUEDROID_H */