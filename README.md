[English](https://github.com/barryblueice/ESP32-Haptic-Precision-TouchPad/blob/main/README.md) | [Simplified Chinese](https://github.com/barryblueice/ESP32-Haptic-Precision-TouchPad/blob/main/README_CN.md)

# **What's This?**

Another touchpad hacking project, based on ESP32-S3 + Surface Laptop Studio 1964 Synaptics TouchPad.

 - Compatible with Microsoft Precision TouchPad (PTP) standard. 
 - Supported Windows Touch Gesture. 
 - Supported Taptic Engine.
 - Supported feedback adjustment.
 - Supported USB/BLE/2.4G connection.

Also include a Dell Goodix fingerprint module, in order to support fingerprint for Windows Hello.

> [!IMPORTANT]
> This project is basically an expanded and strengthened version of an existing project [ESP32 Precision TouchPad](https://github.com/barryblueice/ESP32-Precision-TouchPad).<br>
> Most of the functions will be developed by referring to the source code of previous projects.<br>
> So the basic functions of these two projects are the same. This project has added more new features.

> [!CAUTION]
> Hardware & software only support the models of the touchpad mentioned above, untested touchpad models may be incompatible. <br>Use of such models can lead to driver failure or permanent hardware damage caused by short circuits!

#### **Application:**

 - **TouchPad Main PCB:**

<img width="1000" height="680" alt="image" src="https://github.com/user-attachments/assets/5b6872fd-f712-44a8-9dfa-73c060ba20b4" />

<img width="1000" height="680" alt="image" src="https://github.com/user-attachments/assets/549454a9-c079-4c0e-b090-b5cde212baf4" />

 - **2.4G Reciever:**

<img width="1000" height="293" alt="image" src="https://github.com/user-attachments/assets/70d6ea15-2a78-452b-8408-d5225be4694f" />

<img width="1000" height="293" alt="image" src="https://github.com/user-attachments/assets/687e8a38-8139-482d-ae35-d18f7c7536c9" />

***

# TODO List:
  ## Hardware:
  - [x] PCB Design
  - [ ] Appearance Design (Modeling by SOLIDWORKS)

  ## Software:
  - [x] Recognized as a Microsoft Precision TouchPad
  - [x] **Switching From Mouse Mode (Only One Finger) to Absolute Mode (Supported Multi Finger)**
  - [x] Single Touch Support
  - [x] Multi Touch Support
    - [x] Scroll Gesture Support
    - [x] Single Tap Support
    - [x] Multi Tap Support
  - [ ] Physical Buttons Support (left click & right click)
  - [x] Add a new HID port to support Mouse Mode compatibility.<br>
        (available for some old systems/BIOS that doesn't support PTP, like Windows 7)
  - [x] 2.4G wireless mode support
  - [x] Bluetooth wireless mode support
    - [x] Mouse Mode
    - [ ] PTP Mode
  - [ ] Hacking on Haptic Sensitivity Adjustment Principle

# Current Issues:
 - Feedback adjustment may not support by Windows 11 original setting. It may need third-party setting software to config.
 -  Under BLE mode, Windows has recieved HID report, but most of gestures are unavailable, only a few gesture like 4 fingers tap can trigger.<br>
 Also, Windows Setting for PTP is unavailable under BLE mode.
 - PTP/Mouse Mode switching is unavailable, so BLE Mouse Mode will be default currently.

# Current Support System (already tested):

 - Windows XP (Mouse Mode);
 - Windows 7 (Mouse Mode);
 - Windows 10/11 (PTP Mode);
 - Ubuntu 22.04 or newer (PTP Mode);
 - Oxygen OS 17 (based on Android 16) on PHK110 (PTP Mode);
 - HP / MSI BIOS (Mouse Mode)

***

# About Solution Implementation

~~Please go to [wiki page](https://github.com/barryblueice/ESP32-Haptic-Precision-TouchPad/wiki) for further detail.~~<br>
Wiki still under construction.

***

# Related derivative projects:

 - ESP32 Precision TouchPad - Previous project about hacking ELAN & Goodix TouchPad. [Link](https://github.com/barryblueice/ESP32-Precision-TouchPad)
 - R-SODIUM Precision TouchPad Configurator - GUI Manager for R-SODIUM Precision TouchPad Product series. [Link](https://github.com/barryblueice/R-SODIUM-Precision-TouchPad-Configurator)
