#include "BLE/ble_hid_dev.h"
#include "BLE/hidd_le_prf_int.h"
#include "esp_log.h"

#include "SYS/hid_msg.h"

#include "BLE/BLE_bluedroid.h"

#include "I2C/i2c_hid.h"

void ble_hid_task(void *arg) {
    tp_multi_msg_t tp_msg;
    mouse_msg_t mouse_msg;

    while (1) {
        QueueSetMemberHandle_t xActivatedMember = xQueueSelectFromSet(main_queue_set, portMAX_DELAY);
        
        uint16_t conn_id = hid_conn_id;

        if (xActivatedMember == tp_queue) {

            if (xQueueReceive(tp_queue, &tp_msg, 0)) {

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


// void ble_mouse_task(uint16_t conn_id, uint8_t mouse_button, int8_t mickeys_x, int8_t mickeys_y)
// {
//     mouse_msg_t mouse_msg;

//     buffer[0] = mouse_button;   // Buttons
//     buffer[1] = mickeys_x;           // X
//     buffer[2] = mickeys_y;           // Y
//     buffer[3] = 0;           // Wheel
//     buffer[4] = 0;           // AC Pan

//     hid_dev_send_report(hidd_le_env.gatt_if, conn_id,
//                         HID_RPT_ID_MOUSE_IN, HID_REPORT_TYPE_INPUT, HID_MOUSE_IN_RPT_LEN, buffer);
//     return;
// }



        // if (xActivatedMember == mouse_queue) {
        //     if (xQueueReceive(mouse_queue, &mouse_msg, 0)) {

        //         mouse_hid_report_t report = {0};
                
        //         parse_mouse_report(&mouse_msg, &report);
                
        //         hid_dev_send_report(
        //             hidd_le_env.gatt_if, 
        //             conn_id, 
        //             HID_RPT_ID_MOUSE_IN,
        //             HID_REPORT_TYPE_INPUT, 
        //             sizeof(mouse_hid_report_t), 
        //             (uint8_t *)&report
        //         );
        //     }
        // }  else 