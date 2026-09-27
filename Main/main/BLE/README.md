# NimBLE HID

BLE 使用 ESP-IDF 自带 NimBLE，单连接、最多保存 15 个配对设备。设备名为
`R-SODIUM BLE`；Report ID 1 为五字节鼠标输入，包含按键、X/Y、纵向滚轮和横向滚轮；
Report ID 7 为两字节 Consumer 输入，Report ID 8 为八字节键盘输入；
Feature Report `0x41` 为 0–100 的震动强度。电量每 10 秒更新，NimBLE 日志保持 WARNING。

本次增加 Consumer／键盘后 HID 描述符发生变化，请在电脑上删除旧设备并重新配对一次
（包括之前已使用 NimBLE 的设备）。新配对的密钥和
通知订阅由 NimBLE 保存；旧 Bluedroid 配对数据和 `ble_ccc` 不再读取，也不会清空
其他设备设置。USB 和 2.4G 行为不变，BLE 继续使用 Report 模式鼠标发送路径。

默认模拟鼠标构建的 BLE 模式支持四边滑动、四角轻点、长按重复和轻点转滑动，
复用 USB／2.4G 配置工具保存的设置、旋转和手势震动开关。未新增蓝牙配置通道。
原生鼠标构建不支持依赖绝对坐标的边缘手势。

鼠标与辅助发送共用 NimBLE Host 邮箱，并检查连接代次、加密、对应通知订阅和报告有效性。
辅助释放优先，其余动作与鼠标交替发送；滚动保留最近成功发送的鼠标按键。
未订阅通道的动作直接丢弃，恢复订阅后先发送中立报告。15 个配对设备共预留 90 个 CCCD。
资源不足时重试同一报告。发送成功表示协议栈接受通知，并非电脑应用已确认收到。

## 构建与回归

按 Main 的 `AGENTS.md`，核对 VS Code 设置与现有缓存后使用匹配的 EIM 环境：

```powershell
. 'C:/Espressif/tools/Microsoft.v6.1.PowerShell_profile.ps1'
ninja -C build
$env:PATH = (Resolve-Path build/host-tools).Path + ';' + $env:PATH
$hostCompiler = (Resolve-Path build/host-tools/host-clang.cmd).Path
python tests/run_ble_tests.py $hostCompiler
python tests/run_ble_gatt_tests.py $hostCompiler
python tests/run_ble_gesture_tests.py $hostCompiler
python tests/build_ble_variant.py
```

主机测试使用工作区已有的 Windows 编译器包装脚本。`tests/` 和 `sdkconfig` 按本仓库
现有规则属于本地文件；版本控制中的默认协议栈选择保存在 `sdkconfig.defaults`。
变体脚本使用默认构建生成的 SDK 库，在 `build/validation/` 分别生成模拟鼠标与原生
鼠标固件，不改变默认配置或固件。

硬件验收仍需验证 Windows 重新配对、鼠标点击和双向滚动、亮度／音量／媒体键／方向键／
快捷键、长按释放、手势震动开关、震动强度读写及保存、电量显示、断线重连、断电后的
配对与订阅恢复。构建和主机测试不包含刷写。
