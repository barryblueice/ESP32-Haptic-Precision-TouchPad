# NimBLE HID

BLE 使用 ESP-IDF 自带 NimBLE，单连接、最多保存 15 个配对设备。设备名为
`R-SODIUM BLE`；Report ID 1 为五字节鼠标输入，包含按键、X/Y、纵向滚轮和横向滚轮；
Feature Report `0x41` 为 0–100 的震动强度。电量每 10 秒更新。

从 Bluedroid 固件升级后，请在电脑上删除旧设备并重新配对一次。新配对的密钥和
通知订阅由 NimBLE 保存；旧 Bluedroid 配对数据和 `ble_ccc` 不再读取，也不会清空
其他设备设置。USB 和 2.4G 行为不变，BLE 继续使用 Report 模式鼠标发送路径。

鼠标发送经过 NimBLE Host 队列，并检查连接代次、加密、通知订阅和报告有效性。
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
python tests/build_ble_variant.py
```

主机测试使用工作区已有的 Windows 编译器包装脚本。`tests/` 和 `sdkconfig` 按本仓库
现有规则属于本地文件；版本控制中的默认协议栈选择保存在 `sdkconfig.defaults`。
变体脚本使用默认构建生成的 SDK 库，在 `build/validation/` 分别生成模拟鼠标与原生
鼠标固件，不改变默认配置或固件。

硬件验收仍需验证 Windows 首次配对、鼠标点击和滚动、震动强度读写及保存、电量
显示、断线重连、断电后的配对与订阅恢复。本次迁移的构建和主机测试不包含刷写。
