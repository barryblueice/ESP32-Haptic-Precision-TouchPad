#include "esp_log.h"
#include "esp_mac.h"
#include "nvs/ptp_nvs.h"
#include "input/input_pipeline.h"
#include "wireless/wireless.h"
#include "usb/usbhid.h"

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_mode_init());
    input_init();
    /* Prepare the radio peer before USB callbacks can request a mode command. */
    wireless_init();
    usbhid_init();
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    ESP_LOGI("MAIN", "Device MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    /* tinyusb_driver_install owns the sole tud_task loop. */
}
