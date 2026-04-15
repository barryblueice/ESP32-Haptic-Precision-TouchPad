set(srcs
    "main.c"

    "I2C/i2c_cmd.c"
    "I2C/i2c_quene.c"

    "USB/usb_descriptor.c"
    "USB/usbhid.c"
    
    "SYS/hid_msg.c"
    "SYS/rtos_queue.c"

    "GPIO/irq_func_btn.c"

    "NVS/nvs_handle.c"
)

idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "."
    PRIV_REQUIRES 
        esp_driver_gpio
        esp_driver_i2c
        esp_tinyusb
        esp_timer
        nvs_flash
        esp_event
        esp_wifi
)