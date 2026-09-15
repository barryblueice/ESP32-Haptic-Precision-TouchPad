#include "esp_wifi.h"
#include "esp_now.h"
#include "WIFI/wireless_wifi.h"
#include "SYS/input_pipeline.h"
#include "wireless_settings.h"

void wifi_now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    if (recv_info && data && len == 38) wireless_surface_ack(recv_info->src_addr, data, len);
    if (recv_info && data && len == 38) wireless_settings_receive(recv_info->src_addr, data, len);
    if (data && len == 1 && (data[0] == PTP_MODE || data[0] == MOUSE_MODE))
        input_request_mode(data[0]);
}

void wireless_espnow_init(void)
{
    ESP_ERROR_CHECK(esp_now_register_recv_cb(wifi_now_recv_cb));
}
