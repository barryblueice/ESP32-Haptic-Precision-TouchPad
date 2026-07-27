#include "SYS/hid_msg.h"
#include "I2C/TP/i2c_hid.h"
#include "esp_log.h"

#include "sdkconfig.h"

#define TAG "TP_REPORT"

void parse_mouse_report(const mouse_msg_t *msg, mouse_hid_report_t *report) {
    int move_x = (int)(msg->x * SENSITIVITY);
    int move_y = (int)(msg->y * SENSITIVITY);

    if (move_x > 127)  move_x = 127;
    if (move_x < -127) move_x = -127;
    if (move_y > 127)  move_y = 127;
    if (move_y < -127) move_y = -127;

    report->x = (int8_t)move_x;
    report->y = (int8_t)move_y;
    report->buttons = msg->buttons & 0x07;
    #if CONFIG_PTP_SIMULATED_MOUSE_MODE
    report->wheel = msg->wheel;
    report->pan = msg->pan;
    #endif
}

void parse_ptp_report(const tp_multi_msg_t *msg, ptp_report_t *report) {

    // uint8_t packet[4];

    // i2c_master_receive(dev_haptic_motor_handle, packet, 4, 100);

    // ESP_LOG_BUFFER_HEX(TAG, packet, sizeof(packet));

    report->scan_time = msg->scan_time;
    report->contact_count = msg->actual_count;
    report->buttons = (msg->button_mask > 0) ? 0x01 : 0x00;

    for (int i = 0; i < 5; i++) {
        report->fingers[i].x = msg->fingers[i].x;
        report->fingers[i].y = msg->fingers[i].y;
        report->fingers[i].pressure_z = msg->fingers[i].tip_switch ? msg->fingers[i].pressure_z : 0;

        uint8_t base_id = (msg->fingers[i].tip_switch << 1) | (msg->fingers[i].confidence & 0x01);

        report->fingers[i].tip_conf_id = (i << 2) | base_id;
    }

}
