#ifndef BLE_BLUEDROID_H
#define BLE_BLUEDROID_H

#define REPORTID_TOUCHPAD         0x01
#define REPORTID_MAX_COUNT        0x03
#define REPORTID_PTPHQA           0x04
#define REPORTID_FEATURE          0x05
#define REPORTID_FUNCTION_SWITCH  0x06
#define REPORTID_HAPTIC_FEATURE    0x0C

void ble_bluedroid_init();
void hidd_le_prepare_gatt_table();
void ble_hid_task(void *arg);
void battery_task(void *pvParameters);

extern uint16_t ble_conn_id;

#endif /* BLE_BLUEDROID_H */