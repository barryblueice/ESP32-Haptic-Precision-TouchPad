set(srcs
    "main.c"

    "I2C/i2c_cmd.c"
    "I2C/i2c_queue.c"
    "I2C/i2c_watchdog.c"

    "USB/usb_descriptor.c"
    "USB/usbhid.c"
    
    "SYS/hid_msg.c"
    "SYS/rtos_queue.c"

    "GPIO/irq_func_btn.c"
    "GPIO/irq_tp_int.c"
    "GPIO/gpio_init.c"
    
    "NVS/nvs_handle.c"

    "WIFI/wifi_handle.c"
    "WIFI/broadcast.c"
    "WIFI/heartbeat.c"

    "BLE/ble_bluedroid_init.c"
    "BLE/esp_hidd_prf_api.c"
    "BLE/ble_hid_dev.c"
    "BLE/ble_hid_device_le_prf.c"
    "BLE/ble_hid_descriptor.c"

)

idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "."
    PRIV_REQUIRES 
        esp_driver_gpio
        esp_driver_i2c
        esp_tinyusb
        esp_timer
        bt
        nvs_flash
        esp_event
        esp_wifi
)