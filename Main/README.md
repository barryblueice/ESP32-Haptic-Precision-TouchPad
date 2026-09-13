# ESP32 Haptic Precision Touchpad

ESP32-S3 / ESP-IDF 6.0 触控板固件，支持 USB HID、BLE HID、ESP-NOW 2.4G 和 CS40L25 Surface 触觉反馈。

正常入口是 `main/main.c`。触控采集通过 16 帧 FIFO 交给唯一解析任务，压力和模拟鼠标手势只解析一次；
32 项报告缓冲保留按钮／触点边沿，合并相邻移动，再由所选连接方式的任务发送。
触觉任务独立处理固件和播放；发送重试不会重复产生触觉事件。

```text
GPIO IRQ → I²C 采集 → 16 帧 FIFO → 触控／手势解析 → 32 项报告缓冲 → USB / BLE / ESP-NOW
                                             └→ Surface 触觉事件 → CS40L25
```

在 VS Code 中选择 `.vscode/settings.json` 指定的 ESP-IDF 环境，沿用已有 `build` 目录增量构建。
不要为日常验证切换工作区 sdkconfig 或执行 fullclean。当前硬件目标、描述符和模式选项位于 `sdkconfig`、
`sdkconfig.defaults` 及项目 Kconfig 中。

在已配置的 ESP-IDF 终端中运行：

```powershell
python -B tools/input_pipeline/verify.py
```

该命令读取 VS Code 所选 EIM 环境，复用 `build/CMakeCache.txt` 中的 Python、Ninja 和已有编译命令，执行触觉回归、输入与发送任务回归、
默认配置增量构建，以及其他配置分支的语法检查。不会刷写设备。

- [输入通路、恢复语义与验证说明](tools/input_pipeline/README.md)
- [最近一次验证与固件哈希](tools/input_pipeline/validation.json)
- [Surface 固件来源、触觉设置与离线验证](tools/surface_fw/README.md)
- [触觉供电和集成板测要求](tools/surface_fw/integration.md)

BLE PTP 与 BLE 鼠标沿用互斥构建，每个构建仅发送自身描述符声明的报告。
2.4G 的模拟鼠标复用现有鼠标包，无线包布局和报告协议保持兼容。
实际连接可靠性、延迟和触感仍需板上验证；主机桩测试只验证软件状态及调用顺序。
