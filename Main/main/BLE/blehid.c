#include "BLE/ble_hid_dev.h"
#include "BLE/hidd_le_prf_int.h"
#include "esp_log.h"

#include "sdkconfig.h"

#include "SYS/hid_msg.h"

#include "BLE/BLE_bluedroid.h"

#include "I2C/TP/i2c_hid.h"

void ble_hid_task(void *arg) {
    tp_multi_msg_t tp_msg;
    mouse_msg_t mouse_msg;

    while (1) {
        QueueSetMemberHandle_t xActivatedMember = xQueueSelectFromSet(main_queue_set, portMAX_DELAY);
        
        uint16_t conn_id = ble_conn_id;

        if (ble_hid_is_connected) {

            if (xActivatedMember == tp_queue) {

                if (xQueueReceive(tp_queue, &tp_msg, 0)) {

                    if (current_tp_mode == MOUSE_MODE) {
                        #if CONFIG_PTP_SIMULATED_MOUSE_MODE
                            mouse_msg_t report = {0};

                            parse_ptp_simulated_mouse_report(&tp_msg, &report);

                            hid_dev_send_report(
                                hidd_le_env.gatt_if, 
                                conn_id, 
                                HID_RPT_ID_MOUSE_IN,
                                HID_REPORT_TYPE_INPUT, 
                                sizeof(mouse_hid_report_t), 
                                (uint8_t *)&report

                            );
                            
                        #endif
                    } else {
                        ptp_report_t report = {0};
                            
                        parse_ptp_report(&tp_msg, &report);

                        hid_dev_send_report(
                            hidd_le_env.gatt_if, 
                            conn_id, 
                            HID_RPT_ID_PTP_IN,
                            HID_REPORT_TYPE_INPUT, 
                            sizeof(ptp_report_t), 
                            (uint8_t *)&report
                        );
                    }

                    // ESP_LOGI("BLE_HID_TASK", "Received PTP Report");
                }
            
            } else if (xActivatedMember == mouse_queue) {

                if (xQueueReceive(mouse_queue, &mouse_msg, 0)) {

                    mouse_hid_report_t report = {0};

                    parse_mouse_report(&mouse_msg, &report);
                    
                    hid_dev_send_report(
                        hidd_le_env.gatt_if, 
                        conn_id, 
                        HID_RPT_ID_MOUSE_IN,
                        HID_REPORT_TYPE_INPUT, 
                        sizeof(mouse_hid_report_t), 
                        (uint8_t *)&report

                    );

                }

                // ESP_LOGI("BLE_HID_TASK", "Received Mouse Report");

            }
        }

    }
}
