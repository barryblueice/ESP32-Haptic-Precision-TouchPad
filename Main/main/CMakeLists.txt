set(srcs
    "main.c"

    "I2C/TP/i2c_cmd.c"
    "I2C/TP/i2c_queue.c"
    "I2C/TP/i2c_watchdog.c"
    "I2C/TP/tp_report_handle.c"
    "I2C/TP/ptp_simulated_gesture.c"

    "I2C/SUB_DEV/cs40l25_surface.c"
    "I2C/SUB_DEV/sub_i2c_init.c"
    "I2C/SUB_DEV/IP5X09_handle.c"
    "I2C/SUB_DEV/MP28167_handle.c"

    "I2C/SUB_DEV/mcu-drivers/common/bridge/bridge.c"
    "I2C/SUB_DEV/mcu-drivers/common/buffers/data_ringbuf.c"
    "I2C/SUB_DEV/mcu-drivers/common/buffers/dspbuf.c"
    "I2C/SUB_DEV/mcu-drivers/common/platform_bsp/platform_bsp.c"
    "I2C/SUB_DEV/mcu-drivers/common/platform_bsp/syscalls.c"
    "I2C/SUB_DEV/mcu-drivers/common/platform_bsp/sysmem.c"
    "I2C/SUB_DEV/mcu-drivers/common/fw_img.c"
    "I2C/SUB_DEV/mcu-drivers/common/mem_alloc_wrapper.c"
    "I2C/SUB_DEV/mcu-drivers/common/regmap.c"
    "I2C/SUB_DEV/mcu-drivers/cs40l25/bsp/bsp_cs40l25.c"
    "I2C/SUB_DEV/mcu-drivers/cs40l25/bsp/vregmap.c"
    "I2C/SUB_DEV/mcu-drivers/cs40l25/cs40l25_ext.c"
    "I2C/SUB_DEV/mcu-drivers/cs40l25/cs40l25_spec.c"
    "I2C/SUB_DEV/mcu-drivers/cs40l25/cs40l25.c"
    "I2C/SUB_DEV/mcu-drivers/fw_img/cs40l25_cal_fw_img.c"
    "I2C/SUB_DEV/mcu-drivers/fw_img/cs40l25_fw_img.c"

    "USB/usb_descriptor.c"
    "USB/usbhid.c"

    "SYS/hid_msg.c"
    "SYS/rtos_queue.c"

    "GPIO/irq_func_btn.c"
    "GPIO/irq_tp_int.c"
    "GPIO/gpio_init.c"
    "GPIO/gpio_handle.c"

    "NVS/nvs_handle.c"

    "WIFI/wifi_handle.c"
    "WIFI/broadcast.c"
    "WIFI/heartbeat.c"

    "BLE/ble_bluedroid_init.c"
    "BLE/esp_hidd_prf_api.c"
    "BLE/ble_hid_dev.c"
    "BLE/ble_hid_device_le_prf.c"
    "BLE/ble_hid_descriptor.c"
    "BLE/blehid.c"

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
