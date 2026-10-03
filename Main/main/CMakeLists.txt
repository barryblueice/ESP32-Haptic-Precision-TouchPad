set(srcs
    "main.c"
    "surface_haptic_test.c"
    "synaptics_firmware/synaptics_update.c"
    "synaptics_firmware/synaptics_rmi.c"
    "synaptics_firmware/synaptics_image.c"
    "force_firmware/force_update.c"
    "force_firmware/force_download.c"
    "force_firmware/force_image.c"

    "I2C/TP/force_forward.c"
    "I2C/TP/force_forward_protocol.c"
    "I2C/TP/i2c_cmd.c"
    "I2C/TP/i2c_queue.c"
    "I2C/TP/tp_motion_filter.c"
    "I2C/TP/tp_raw_trace.c"
    "I2C/TP/tp_sleep.c"
    "I2C/TP/i2c_watchdog.c"
    "I2C/TP/tp_report_handle.c"
    "I2C/TP/ptp_simulated_mouse_gesture.c"

    "I2C/SUB_DEV/cs40l25_surface.c"
    "I2C/SUB_DEV/surface_haptic_policy.c"
    "I2C/SUB_DEV/surface_haptic_runtime.c"
    "I2C/SUB_DEV/surface_haptic_hw.c"
    "I2C/SUB_DEV/surface_haptic_settings.c"
    "I2C/SUB_DEV/sub_i2c_init.c"
    "I2C/SUB_DEV/BQ24195_MAX17048_handle.c"
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
    "SYS/report_buffer.c"
    "SYS/input_pipeline.c"
    "SYS/connection.c"
    "SYS/connection_policy.c"
    "SYS/rstp_protocol.c"
    "SYS/device_config.c"
    "SYS/edge_gesture.c"
    "USB/usb_config.c"
    "SYS/aux_output.c"
    "SYS/point_gesture.c"
    "SYS/knuckle_gesture.c"

    "GPIO/irq_func_btn.c"
    "GPIO/irq_tp_int.c"
    "GPIO/gpio_init.c"
    "GPIO/gpio_handle.c"

    "NVS/nvs_handle.c"

    "WIFI/wifi_handle.c"
    "WIFI/broadcast.c"
    "WIFI/heartbeat.c"
    "WIFI/wireless_settings.c"

    "BLE/ble_nimble.c"
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

# Keep this image out of normal / Force firmware builds.
if(CONFIG_SURFACE_SYNAPTICS_UPDATE_MODE)
    set(synaptics_image "${CMAKE_CURRENT_SOURCE_DIR}/synaptics_firmware/TM3651-001_Surface_4.12.139.img")
    file(SHA256 "${synaptics_image}" synaptics_sha256)
    if(NOT synaptics_sha256 STREQUAL "08657877a9ce4709fc138162a9b4ee086b6a9b921dde6eafef4f682bc74de3ee")
        message(FATAL_ERROR "Synaptics image does not match the pinned TM3651-001 Surface 4.12.139 image")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${synaptics_image}")
    target_add_binary_data(${COMPONENT_LIB} "${synaptics_image}" BINARY RENAME_TO synaptics_image)
endif()
