[英文](https://github.com/barryblueice/ESP32-Haptic-Precision-TouchPad/blob/main/README.md) | [简体中文](https://github.com/barryblueice/ESP32-Haptic-Precision-TouchPad/blob/main/README_CN.md)

# **这是什么？**

又一个触摸板破解项目。基于ESP32-S3 + Surface Laptop Studio 1964 Synaptics TouchPad。

 - 兼容Microsoft精确式触摸板规范。
 - 支持Windows触摸板手势。
 - 支持压感引擎。
 - 支持反馈调整。
 - 支持USB有线/2.4G/蓝牙连接。

同时包含了一个Dell Goodix指纹模块，用于Windows Hello登录。

> [!IMPORTANT]
> 这个项目基本是前代项目[ESP32 Precision TouchPad](https://github.com/barryblueice/ESP32-Precision-TouchPad)的扩展和增强版本。<br>
> 大部分代码都基于前代项目开发而来。<br>
> 所以这两个项目基本相同，只是这个项目加入了更多新特性。

> [!CAUTION]
> 硬件&软件仅适配上面提到的触摸板型号，未经测试的触摸板型号可能会因不兼容导致无法驱动或短路烧毁。

> [!TIP]
> 这个项目在开发的过程中使用了AI辅助开发。

#### **应用:**

 - **触摸板PCB:**

<img width="1000" height="680" alt="image" src="https://github.com/user-attachments/assets/5b6872fd-f712-44a8-9dfa-73c060ba20b4" /><br>

<img width="1000" height="680" alt="image" src="https://github.com/user-attachments/assets/549454a9-c079-4c0e-b090-b5cde212baf4" /><br>

 - **2.4G接收器:**

<img width="1000" height="293" alt="image" src="https://github.com/user-attachments/assets/70d6ea15-2a78-452b-8408-d5225be4694f" /><br>

<img width="1000" height="293" alt="image" src="https://github.com/user-attachments/assets/687e8a38-8139-482d-ae35-d18f7c7536c9" /><br>

 - **系统应用:**

<img width="541" height="832" alt="image" src="https://github.com/user-attachments/assets/747130b1-241c-4b78-aa1a-21d87529cdef" /><br>

<img width="430" height="225" alt="image" src="https://github.com/user-attachments/assets/6c938d22-ceb2-4b2d-9e3d-539477b2a283" /><br>

<img width="442" height="226" alt="image" src="https://github.com/user-attachments/assets/d59daa97-79a1-4e96-ae57-ae769c75d617" /><br>

# TODO List:
  ## 硬件:
  - [x] PCB设计
  - [ ] 外观设计 (SOLIDWORKS建模)

  ## Software:
  - [x] Microsoft精确式触摸板握手
  - [x] **从Mouse Mode (仅支持单指) 切换到Absolute Mode（支持多指）**
  - [x] 单指触摸支持
  - [x] 多指触摸支持
    - [x] 多指滑动手势支持
    - [x] 单指Tap支持
    - [x] 多指Tap支持
  - [ ] 物理按键支持（左键 & 右键）
  - [x] 添加新的HID端口以支持Mouse Mode兼容<br>
        （对于部分不支持PTP的老系统/BIOS可用，例如Windows 7/Windows XP）
  - [x] PTP模拟Mouse Mode支持
  - [x] 2.4G无线模式支持
    - [x] 与上代项目[ESP32-Precision-TouchPad](https://github.com/barryblueice/ESP32-Precision-TouchPad)兼容
  - [x] 蓝牙无线模式支持
    - [x] Mouse Mode
    - [ ] PTP Mode
  - [x] 压感调整原理破解
    - [x] CS40L25 SDK适配
    - [x] ROM模式下触发振动
    - [ ] 通过特定wavefrom固件获取更好的震感反馈

# 目前仍然存在的问题：
 - 在蓝牙模式下，Windows接受到并正确解析了HID报文，但是手势处于不可用的状态。只有很小一部分手势例如四指轻触可以识别到。<br>
同时，在蓝牙模式下目前Windows依旧不支持PTP设置。
 - 蓝牙模式下暂时不支持PTP/Mouse Mode切换，所以目前默认只使用BLE Mouse模式。

# 目前支持的系统（已经过测试）：

 - Windows XP （鼠标模式）；
 - Windows 7 （鼠标模式）；
 - Windows 10/11（精确式触摸板模式）；
 - Ubuntu 22.04及更新版本系统（精确式触摸板模式）；
 - Color OS 17 （基于Android 16），测试设备为一加Ace2（精确式触摸板模式）；
 - HP / MSI BIOS（鼠标模式）

***

# 关于方案实现：

可前往[wiki page](https://github.com/barryblueice/ESP32-Haptic-Precision-TouchPad/wiki) 了解更多内容。

***

# 相关的衍生计划:

 - ESP32 Precision TouchPad - 上一代关于ELAN & Goodix TouchPad破解的项目。 [Link](https://github.com/barryblueice/ESP32-Precision-TouchPad)
 - R-SODIUM Precision TouchPad Configurator - R-SODIUM触摸板产品系列的GUI设置软件。 [Link](https://github.com/barryblueice/R-SODIUM-Precision-TouchPad-Configurator)
