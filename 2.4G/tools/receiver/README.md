# 接收器功能同步与验证

接收器直接编译 Main 的 `aux_output.c`，并引用共享无线协议与 HID 描述符。
动作 1–6 保留方向／长按语义，13–47 输出媒体键、功能键和组合键；本次不包含指关节动作。
无线封包仍为 38 字节，USB Report ID、VID/PID 和设备序列号不变。

Main 切回 2.4G 时会创建新会话并默认使用 PTP 模式。接收器应用新会话或旋转配置后，
通过现有无线控制队列重发电脑当前选择的鼠标／PTP 模式。相同通告只重新确认，
不会清空正在处理的输入。已有会话恢复机制负责按键释放和设置 ACK 隔离。

USB Input Mode 的 GET_FEATURE 回读当前模式（鼠标 0、PTP 3），不固定返回 PTP。
总线复位通过 TinyUSB class reset 回调清除旧模式及在途报告；挂载回调保留已经协商的模式，
避免先收到 PTP SET_FEATURE、随后挂载时又被重置为鼠标模式。模式选择允许零填充，
非零尾部被视为格式错误而忽略。USB 配置和模式写入会打印日志，便于随后检查无线转发。

自定义手势振动、离电压感阈值和自动切换由触控板端执行。接收器继续转发 Windows
振动强度和按压力度设置，不提供完整 RSTP 配置或手动波形播放接口。

从 `2.4G` 目录运行：

```powershell
python -B tools/receiver/verify.py --build-dir build --host-clang ../Main/build/host-clang.cmd
```

`--host-clang` 也支持已有的 Windows 编译器 `.cmd` 包装脚本，或通过 `HOST_CLANG`
环境变量提供；未指定时从 PATH 查找主机 Clang，并排除 ESP Clang。额外编译器参数可用
`--host-clang-arg=参数` 重复传入。需要能够构建 x64 Windows DLL 的 Clang、LLD 和标准头文件。
主机测试不依赖 Main 的测试目录，只读取 Main 的生产代码。

可用 `--host-only` 或 `--build-only` 单独执行其中一部分。完整验证包含：

- 接收队列、USB 生命周期、长按／取消、动作 13–47 的准确键值及释放报告。
- 新会话模式恢复、重复通告、会话切换中的在途按键及旧设置 ACK。
- ESP32-S2 固件构建，以及新旧描述符和 1／10 ms 轮询间隔的四种编译检查。
- 原配置／旧缓存保护、依赖版本校验、共享源码指纹与固件 SHA-256。

固件构建使用 `.vscode/settings.json` 选中的 ESP-IDF 及对应 EIM 激活脚本。
当前使用 v6.1 和已有 `build/` 缓存增量构建，保留项目配置、依赖锁及 ccache 设置，
不执行清理或全量重编译。上例复用本机已有的 Windows Clang 包装脚本；其他机器可替换
`--host-clang` 路径。

如需独立构建，可显式指定其他项目内目录；首次创建时复制 `sdkconfig` 和依赖锁文件，
后续使用该目录缓存增量构建。独立目录迁移 SDK 时只允许更新锁文件的 IDF 条目，
其他依赖版本及内容哈希必须保持一致。

输出位于所选构建目录：

- `ESP32-Haptic-2.4G-Receiver.bin` / `.elf`：接收器固件。
- `receiver-host-tests/`：由生产 C 源码生成的主机测试及结果。
- `receiver_validation/`：构建、大小和描述符检查日志，以及 `result.json`。

完整验证还会更新本目录的 `validation.json`。该记录包含实际 SDK、编译器、
构建输入、源码哈希和产物哈希；仅执行部分验证不会覆盖这份完整记录。

验证不会刷写设备。Windows 实际操作、无线切换及触控板振动仍需硬件验收。
