# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

# Windows pads one-byte settings to the PTP collection's largest Feature:
# 256 payload bytes plus Report ID. This bound also sizes TinyUSB's EP0 buffer.

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ESP32_HAPTIC_PRECISION_TOUCHPAD)
idf_build_set_property(COMPILE_DEFINITIONS "CFG_TUD_HID_EP_BUFSIZE=257" APPEND)
