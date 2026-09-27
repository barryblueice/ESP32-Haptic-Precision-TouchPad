#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "SYS/report_buffer.h"

#define HID_RPT_ID_MOUSE_IN 1
#define REPORTID_HAPTIC_INTENSITY 0x41

extern const uint8_t ble_mouse_hid_report_descriptor[];
extern const uint16_t ble_mouse_hid_report_len;

esp_err_t ble_hid_init(void);
void ble_hid_task(void *arg);

/* Internal transport boundary. No Bluetooth stack types escape this header.
 * Epochs distinguish connections even when the controller reuses a handle. */
typedef enum { BLE_TX_OK, BLE_TX_RETRY, BLE_TX_FAILED } ble_tx_result_t;
uint32_t ble_input_connection(bool up, uint16_t conn);
void ble_input_subscription(uint16_t conn, bool enabled);
void ble_input_congestion(uint16_t conn, bool busy);
void ble_input_complete(uint16_t conn, uint32_t epoch, uint16_t handle, ble_tx_result_t result);
uint16_t hid_dev_report_handle(uint8_t id);
esp_err_t ble_hid_send_mouse(uint16_t conn, uint32_t epoch, const input_report_t *report);
