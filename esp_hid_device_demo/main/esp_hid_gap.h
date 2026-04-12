#ifndef _ESP_HID_GAP_H_
#define _ESP_HID_GAP_H_

#define HIDD_IDLE_MODE 0x00
#define HIDD_BLE_MODE 0x01
#define HIDD_BT_MODE 0x02
#define HIDD_BTDM_MODE 0x03

#define HID_DEV_MODE HIDD_BLE_MODE

#include "esp_err.h"
#include "esp_log.h"

#include "esp_bt.h"
#include "esp_hid_common.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_hid_gap_init(uint8_t mode);
esp_err_t esp_hid_gap_deinit(void);

esp_err_t esp_hid_ble_gap_adv_init(uint16_t appearance, const char *device_name);
esp_err_t esp_hid_ble_gap_adv_start(void);

#ifdef __cplusplus
}
#endif

#endif
