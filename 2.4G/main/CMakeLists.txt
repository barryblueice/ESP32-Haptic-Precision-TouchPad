idf_component_register(
    SRCS "main.c"
        "protocol.c"
        "wireless/receiver_extension.c"
        "../../Main/main/SYS/aux_output.c"
        "input/report_buffer.c"
        "input/input_pipeline.c"
        "usb/usbhid.c"
        "usb/usb_descriptor.c"
        "nvs/ptp_nvs.c"
        "wireless/wifi_receive.c"
        "wireless/heartbeat.c"
        "wireless/broadcast.c"
    INCLUDE_DIRS "." "../../Main/main"
    PRIV_REQUIRES esp_driver_gpio
        esp_tinyusb
        esp_timer
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
    )

target_compile_definitions(${COMPONENT_LIB} PRIVATE AUX_OUTPUT_RECEIVER=1)
