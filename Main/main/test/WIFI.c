#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "WIFI_SCAN";

void wifi_scan(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true
    };

    ESP_LOGI(TAG, "开始扫描...");
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
    ESP_LOGI(TAG, "扫描完成!");

    uint16_t number = 0;
    uint16_t ap_count = 20;
    wifi_ap_record_t ap_info[20];
    memset(ap_info, 0, sizeof(ap_info));

    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_info));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&number));
    ESP_LOGI(TAG, "找到的 AP 总数: %u, 显示前 %u 个:", number, ap_count);

    printf("%-32s | %-7s | %-4s | %-12s\n", "SSID", "RSSI", "CHAN", "MAC");
    for (int i = 0; i < ap_count; i++) {
        printf("%-32s | %-7d | %-4d | %02x:%02x:%02x:%02x:%02x:%02x\n",
               (char *)ap_info[i].ssid, ap_info[i].rssi, ap_info[i].primary,
               ap_info[i].bssid[0], ap_info[i].bssid[1], ap_info[i].bssid[2],
               ap_info[i].bssid[3], ap_info[i].bssid[4], ap_info[i].bssid[5]);
    }
}

void app_main(void) {
    wifi_scan();
}