# USB 上电与电脑重启恢复修复

## 行为与原因

Windows 10/11 的 USB 总线复位不一定伴随 TinyUSB 的拔出回调。原实现只在拔出时清除发送忙标记，复位取消的传输没有完成回调时，会一直阻塞后续输入。

现在用项目侧的 TinyUSB 应用驱动观察 configuration reset。它不占用任何 HID 接口；复位清除旧会话的触摸、鼠标、辅助按键和配置响应状态。USB 提交、复位和完成回调均在 TinyUSB 任务中执行。发送任务仅安排延迟执行，并在事件队列满时重试。挂起保留在途传输，恢复仍等待真正完成，不能把挂起当作已经取消。

USB、BLE 和 2.4G 冷启动都不再要求先产生全抬起报告。启动阶段已有手指接触时，首次连接及初始模式设置把当前接触作为新手势，不要求“触摸—抬起”激活。首次真实输入得到发送确认后退出启动阶段；真实读帧错误、溢出和过期帧也会退出启动阶段，继续要求真实抬手。运行中重连及模式切换保留恢复保护。捕获阶段保存真实触摸状态，恢复许可与当前接触状态分开保存，抬手早于 USB 释放完成时不必再抬一次。GPIO 高电平、静默和超时均不用于生成假抬手。

根据“三种模式均需触摸再抬手”的硬件反馈，补充复现了两条遗漏路径：共享管线的首次连接无条件进入抬手等待；HID-I2C 的零长度复位应答被当作鼠标帧，导致下一帧 PTP 输入触发 `format` 恢复。现在复位及空应答在捕获入口过滤，不读取其尾部垃圾，也不能作为抬手证据。零长度复位应答依据：[Microsoft HID-I2C Reset](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/plug-and-play-support-and-power-management)。回归使用持续运行的生产解析循环，避免每个样本重启解析器而漏掉格式状态。

触摸 IRQ 读取提前到控制器初始化之后、其他外设初始化之前；共享 I2C0 的触觉固件加载在连接初始化之后启动。未删除控制器必要的复位等待。此调整和启动门槛修复尚未在硬件上测量，不能据主机测试宣称已实现上电一秒内响应。

新 USB 会话不发送无历史按下状态的冗余释放报告。实际提交过的触点／按键仍需释放；PTP 不再等待无关辅助接口的中性报告。辅助键自己的按下、释放和同端点串行顺序仍保留。

Input Mode 查询返回当前 USB 会话接受的模式：默认 0（鼠标），主机请求 3 后使用 PTP。主机复位清除模式，普通挂起保留模式。重复请求幂等，控制器模式写入失败每 100 ms 重试，新请求覆盖旧请求。默认模拟鼠标配置下，两种逻辑模式共用控制器 PTP 采集格式，因此不重复写入相同物理模式。协议依据：[Microsoft Configuration Collection](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchpad-configuration-collection)。

本次启动修复未更改描述符、Report ID、NVS 和配置协议；SDK、依赖锁及 ccache 设置保持不变。经用户授权恢复 `sdkconfig` 的 GATTS 功能及其兼容别名，以便链接原有 BLE HID 实现。

## 软件验证

在 Main 目录执行；主机 Clang 与 lld-link 沿用现有 `build/host-tools`：

```powershell
. 'C:/Espressif/tools/Microsoft.v6.1.PowerShell_profile.ps1'
$hostTools = (Resolve-Path 'build/host-tools').Path
$env:PATH = $hostTools + [IO.Path]::PathSeparator + $env:PATH
$hostCompiler = Join-Path $hostTools 'host-clang.cmd'
python tests/run_usb_tests.py $hostCompiler
python tests/run_host_tests.py --clang $hostCompiler
python tests/run_input_irq_tests.py $hostCompiler
python tests/run_wireless_haptic_tests.py $hostCompiler
python tests/run_ble_tests.py $hostCompiler
ninja -C build
```

新增 USB 测试直接编译生产回调、会话观察器、发送循环、配置传输、输入解析与恢复代码，仅模拟 RTOS、硬件和 USB 栈边界。覆盖首次滑动、启动按住、模式查询／重试／交错、无拔出回调的复位、辅助及配置传输取消、实际按下状态释放、挂起、读帧错误、队列溢出、过期数据、调度队列满和重复复位。

软件结果：USB 22、核心配置 33、IRQ 5、输入／无线触觉与 VBUS 31、BLE 9，共 100 项通过。新增覆盖三种连接方式首次滑动无前置抬手、连接建立前后持续触摸、复位应答与空应答后首次触摸、空应答不能解除真实错误门槛、非法长度仍要求真实抬手，以及正常使用后的 USB 重连保护。结果保存在对应 `build/*host-tests/result.json`。测试文件继续沿用仓库忽略 `tests/` 的约定；交付或复制工作区时应同时保留新增的 `run_usb_tests.py`、`usb_cases.c`、`usb_runtime.h`、修改后的 `wireless_haptic_cases.c` 及现有主机测试支持文件。

本轮在 EIM ESP-IDF v6.1 环境执行 `ninja -C build` 成功，生成 `build/ESP32_HAPTIC_PRECISION_TOUCHPAD.bin`，大小 `0x15e930`，应用分区剩余 `0x186d0`（7%），分区检查通过。首次链接因当前 `sdkconfig` 关闭 GATTS 而缺少 `esp_ble_gatts_*` 符号；经用户明确授权重新启用 `CONFIG_BT_GATTS_ENABLE` 及兼容别名后，在原构建目录完成所需重编译。没有清理缓存或刷写设备。

## 诊断与硬件验收

USB 日志记录 reset/configured/suspended/resumed 和会话 `epoch`。`INPUT` 统计新增 `source_wait_up`、`recovery_ready`、`host_active`：前者表示等待真实抬手，后两者分别表示恢复许可和主机可能仍持有的触点／按键。`releases` 非零表示必要释放尚未完成。统计最多每 5 秒且仅状态变化时输出，不逐帧打印。

新增一次性时间日志：`Startup: touch capture enabled`、`Startup: transport initialized`、`First controller input`、`First host input acknowledged`。时间为启动后的毫秒数；连接初始化返回不代表主机已完成枚举或订阅，发送确认也不代表 Windows 已绘制光标。结合 USB/BLE/无线连接日志判断 5–10 秒发生在初始化、连接、采样还是发送阶段。

**尚未刷写设备，以下硬件验收未执行，软件测试不能代替硬件结论。**

| 场景 | 次数 | 验收要求 | 状态 |
| --- | --- | --- | --- |
| 电脑已进入 Windows，触摸板冷启动 | 至少 10 次 | 枚举及模式协商完成后，首次滑动可用 | 待验证 |
| USB 一直连接，电脑冷启动 | 至少 10 次 | 进入系统后无需重插 USB | 待验证 |
| USB 一直连接，Windows 重启 | 至少 10 次 | 无旧会话忙状态残留，无需重插 | 待验证 |
| Windows 运行时重插 USB | 至少 10 次 | 新会话可用，无旧点击或移动重放 | 待验证 |
| USB／BLE／2.4G 触摸板冷启动，提前放上手指 | 每种至少 10 次 | 连接就绪后继续滑动即可响应，无须先抬手，无旧手势重放 | 待验证 |
| 正常使用后断线／主机重启，已有手指按住 | 各场景补测 | 必要时真实抬手一次后恢复，无误点击 | 待验证 |
| 挂起／恢复时正在触摸或按住辅助键 | 补测 | 无卡住按键，必要释放完成后正常使用 | 待验证 |

每轮检查滑动、点击、长按拖动和双指滚动。记录 Windows 版本、USB 连接方式、是否提前按住、枚举及模式协商时间、首个有效输入时间、是否重插，以及相关 USB/INPUT 日志。若仍有异常，日志应能区分未配置、模式写入重试、等抬手和等端点释放。
