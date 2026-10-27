[English](https://github.com/barryblueice/ESP32-Haptic-Precision-TouchPad/blob/main/README.md) | [Simplified Chinese](https://github.com/barryblueice/ESP32-Haptic-Precision-TouchPad/blob/main/README_CN.md)

<img width="1646" height="1080" alt="image" src="https://github.com/user-attachments/assets/62c18fca-4e30-4de0-82df-305f0f221452" />

***

# **What's This?**

Another touchpad hacking project, based on ESP32-S3 + Surface Laptop Studio 1964 Synaptics TouchPad.

 - Compatible with Microsoft Precision TouchPad (PTP) standard.
 - Supported Windows Touch Gesture.
 - Supported Haptic Engine.
 - Supported feedback adjustment.
 - Supported USB/BLE/2.4G connection.

Also include a Dell Goodix fingerprint module, in order to support fingerprint for Windows Hello.

> [!CAUTION]
> Hardware & software only support Surface Laptop Studio 1964, untested touchpad models may be incompatible. <br>Use of such models can lead to driver failure or permanent hardware damage caused by short circuits!<br>

> [!TIP]
> This project utilized AI-assisted development during its development process.<br>
> Thanks to OpenAI Codex, most of code is based on the reverse-engineering sample of SAM (Surface aggregator module) and CirrusLogic Waveform Firmware.

### Hardware opensource link: [Oshwhub - ESP32 Haptic Precision Touchpad](https://oshwhub.com/barryblueice/esp32-haptic-precision-touchpad)

#### **Application:**

 - **TouchPad Main PCB:**

<img width="1000" height="680" alt="image" src="https://github.com/user-attachments/assets/5b6872fd-f712-44a8-9dfa-73c060ba20b4" /><br>

<img width="1000" height="680" alt="image" src="https://github.com/user-attachments/assets/549454a9-c079-4c0e-b090-b5cde212baf4" /><br>

 - **2.4G Receiver:**

<img width="1000" height="293" alt="image" src="https://github.com/user-attachments/assets/70d6ea15-2a78-452b-8408-d5225be4694f" /><br>

<img width="1000" height="293" alt="image" src="https://github.com/user-attachments/assets/687e8a38-8139-482d-ae35-d18f7c7536c9" /><br>

 - **System Application:**

<img width="541" height="832" alt="image" src="https://github.com/user-attachments/assets/747130b1-241c-4b78-aa1a-21d87529cdef" /><br>

<img width="430" height="225" alt="image" src="https://github.com/user-attachments/assets/6c938d22-ceb2-4b2d-9e3d-539477b2a283" /><br>

<img width="442" height="226" alt="image" src="https://github.com/user-attachments/assets/d59daa97-79a1-4e96-ae57-ae769c75d617" /><br>

# TODO List

## Hardware
- [x] PCB Design
- [x] Appearance Design (Modeling in SOLIDWORKS)

## Software

### General Support
- [x] Recognized as a Microsoft Precision TouchPad
- [x] **Switching from Mouse Mode (single finger) to Absolute Mode (multi-finger supported)**

### Touch Support
- [x] Single Touch
- [x] Multi Touch
- [x] Edge-sliding custom gesture
- [x] Edge-point custom gesture

### Physical Controls
- [x] ~~Physical Buttons (Left & Right Click)~~<br>Physical Press Support (Supported by Haptic Motor & 6 Channel Pressure Sensors)

### Compatibility
- [x] Add new HID port for Mouse Mode (for older systems/BIOS, e.g., Windows 7)
- [x] PTP Simulated Mouse Mode
- [ ] (Future Plan) Apple Magic Trackpad & Mac Original MagicPad Protocal Support

### Wireless Modes
- [x] 2.4G Wireless
  - [x] Compatible with [ESP32-Precision-TouchPad](https://github.com/barryblueice/ESP32-Precision-TouchPad)
- [x] Bluetooth
  - [x] Mouse Mode
  - [ ] ~~PTP Mode~~

### Haptic Feedback
- [x] Hacking on Haptic Sensitivity
  - [x] CS40L25 SDK Support
  - [x] ROM Vibrate
  - [x] Specific waveform firmware
- [x] Haptic Support
  - [x] Click Sensitivity Adjustment
  - [x] Haptic Click & Intensity Adjustment
  - [x] Custom Haptic vibration
  - [ ] ~~Haptic Feedback & Intensity Control (tentative)~~

# Current Issues:
 -  Under BLE mode, Windows has recieved HID report, but most of gestures are unavailable, only a few gesture like 4 fingers tap can trigger.<br>
    So under BLE mode, mouse mode is supported only. Recommend to use PTP simulated mouse mode.
 Also, Windows Setting for PTP is unavailable under BLE mode.
 - Under BLE mode, PTP/Mouse Mode switching is unavailable, so BLE Mouse Mode will be default currently.
 - ~~Due to a firmware issue with the CS40L25, the vibration feedback when pressed could be strange.~~ <br>Vibration feedback has already fixed through reverse-engineering firmware & control logic by Surface SAM and CS40L25 firmware.

# Current Support System (already tested):

| System | PTP Mode Support | Mouse Mode Support | Notice
 :-: | :-: | :-: | :-: |
Windows 10/11 | ✔️ | ✔️ | Only Windows 11 support haptic feedback adjustment by system.<br> Windows 10 need [custom configurator software](https://github.com/barryblueice/R-SODIUM-Precision-TouchPad-Configurator)
Windows 7 | ❌ | ✔️ | Windows 7 may not support BLE device.
Windows XP | ❌ | ✔️ | BLE is not supported on Windows XP.
Linux | ✔️ | ✔️ | Only KDE support haptic feedback adjustment, other desktop environment is not confirmed.
Android | ✔️ | ✔️ | Oxygen OS 17 (based on Android 16) on PHK110
Huawei Harmony OS | ✔️ | ✔️ | Tested on Huawei Mate XT 2
Mac OS | ❌ | ✔️ | Mac OS needs more reverse-engineering. It does not support PTP protocol.

***

# About Solution Implementation

Please go to [wiki page](https://github.com/barryblueice/ESP32-Haptic-Precision-TouchPad/wiki) for further detail.

***

# Related derivative projects:

 - [ESP32 Precision TouchPad](https://github.com/barryblueice/ESP32-Precision-TouchPad) - Previous project about hacking ELAN & Goodix TouchPad.
 - [FluentGesture](https://github.com/barryblueice/FluentGesture) - Customizing gesture for Precision TouchPad.
 - [R-SODIUM Precision TouchPad Configurator](https://github.com/barryblueice/R-SODIUM-Precision-TouchPad-Configurator) - GUI Manager for R-SODIUM Precision TouchPad Product series.
