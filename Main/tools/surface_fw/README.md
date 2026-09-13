# Surface 固件、振动服务与独立测试

输入与发送通路已接入统一报告缓冲，模拟鼠标在输入任务中处理，2.4G 模拟鼠标发送已补齐。
当前输入行为、增量验证入口和最新构建结果见 [输入链路说明](../input_pipeline/README.md)；
本目录保留 Surface 固件来源、触觉专用测试和此前集成记录。

默认入口为 `main/main.c` 的正常触控板模式，真实点击使用 Surface 按下／释放策略。
入口已先从提交 `f858a7cf24c61f19d712ee2720838c63b0c7e8f4` 还原并校验一致，
随后调整配置加载和 I²C 初始化顺序、接入振动服务及测试模式选择。
强度范围 0～100，默认 63；保存的有效连接模式继续使用，无有效值时默认 USB。

原独立测试保存在 `main/surface_haptic_test.c`，仅在
`CONFIG_SURFACE_HAPTIC_TEST_MODE=y` 时替代正常入口；该选项默认关闭。
独立测试仍按 `0 → 25 → 63 → 75 → 100 → 0` 循环，每档先按下、后释放，
每槽约 2 秒、每轮约 20 秒。正常模式没有这个固定播放间隔。
两个模式共用 `surface_haptic_hw.c` 初始化和固件检查；CP/GPI 衰减为 0、
GPIO 自动触发关闭，不执行 ROM/BHM 启动振动、校准或旧固件回退。

正常服务、设置迁移、验收步骤及验证边界见 [集成说明](integration.md)。

## 波形策略与移植边界

策略模块为 `main/I2C/SUB_DEV/surface_haptic_policy.c/.h`。
映射来源是 Surface SAM variant 0 的 `0xD859A` 例程，
应用于本工程的 `0x1400E1 / 0x0A0603` 运行固件。

| 设置值 | 按下索引 | 释放索引 | 自动测试代表值 |
|---|---:|---:|---:|
| 0 | 100 | 100 | 0（关闭） |
| 1～38 | 17 | 13 | 25 |
| 39～63 | 21 | 15 | 63 |
| 64～88 | 24 | 17 | 75 |
| 89～100 | 36 | 29 | 100 |
| 101～255 | 无效，保留调用者原输出 | — | 不进入自动测试 |

`surface_haptic_resolve(setting, out)` 解析设置；空指针或无效设置返回 false，
不修改输出。关闭档保留原始 `100/100`，并标记 `enabled=false`。
`surface_haptic_play_event(pair, release)` 为同步调用，必须由唯一拥有 CS40L25
的任务在初始化成功后调用。每个启用事件先配置整对索引、零衰减和 GPIO 禁用，
再通过 `bsp_dut_trigger_haptic(index, 0)` 播放；错误向上传递，不重试。
启用状态下任一索引超出 `0～77` 时，在硬件写入之前拒绝。

原机触发数据包的寄存器地址为 `0x00013020`，对应当前驱动的 MBOX1。
时长参数 0 表示选择按索引播放路径，不表示播放零毫秒；不写 100 ms 超时，
不使用 MBOX2 定时播放。实际波形持续时间由固件及波形数据决定。
关闭档在主机侧跳过所有映射和触发写入；**不能把索引 100 当普通波形触发**。
这是本项目适配策略，原机 DSP 对 100 的内部语义尚未确定。

[policy_reference](policy_reference/) 保存 256 项映射 CSV、所需汇编片段和
来源哈希。汇编只规范换行和末尾空行；manifest 中的文件哈希对应工程内副本，
函数哈希对应原 SAM 二进制。资料中的函数名是分析者命名，不是恢复的原始源码。
原始 SAM 内置 DSP 版本为 0x0A0601，命名符号来自 0x0A0603 SDK 对照；
校验脚本核对按下、释放地址与本项目固件符号的一致性，但这不等于物理触感一致。

本次不移植原机 BuckBoost/Touchpad 状态机或其初始化寄存器表，
沿用 ESP32 板级供电、GPIO 和现有 BSP 初始化。
正常触控板通过事件队列接入此策略；USB／BLE 使用单字节强度报告 0x41，
旧波形列表 0x42 和手动触发 0x43 已移除。新设置保存在 NVS `haptic_surface`。

## 固件来源

- 外部来源：`D:\Documents\mcu-drivers\targetbin\unpacked\cs40l25\sdk_surface`。
- 原始更新包：`SurfaceTouchpadHaptic_2.9.139.bin`。
- 原始包 SHA-256：`e06ae4cd8aa6dd99bf07066756f3b5339477760f228d7db3a683baca230e8616`。
- 导入数组：`main/I2C/SUB_DEV/mcu-drivers/fw_img/cs40l25_fw_img.c`。
- 数组二进制 SHA-256：`f93d5b1058b09f11c7d2a28560d92c43a2407f4012293a1e3c39a574215d3cda`。
- 数组长度 38,576 字节，fw_img_v2，Firmware ID `0x1400E1`，DSP revision `0x0A0603`。
- 33 个有效符号、167 个有序写入块；算法为 Firmware、VIBEGEN、Dynamic F0。

导入文件仅规范了文本换行和文件末尾空白，固件数组字节未改变。
构建使用工程内副本，不依赖外部目录。旧校准资源保持独立，不在本测试中加载。
`reference.json` 保存了原解包目录的符号映射和全部写入块的地址、长度、SHA-256。

## 符号与寄存器

运行固件使用新的通用符号编号，禁止混用旧的 75 项映射：

| 控制 | 新符号 ID | 总线地址 |
|---|---:|---:|
| HALO_STATE | 0x01 | 0x02801510 |
| HALO_HEARTBEAT | 0x02 | 0x02801514 |
| INDEXBUTTONPRESS | 0x0D | 0x0280167C |
| INDEXBUTTONRELEASE | 0x0E | 0x0280168C |
| GAIN_CONTROL | 0x10 | 0x028016C0 |
| VIBEGEN_TIMEOUT_MS | 0x1D | 0x02800B54 |
| VIBEGEN_COMPENSATION_ENABLE | 0x1E | 0x02800B5C |

VIBEGEN enable/status/NUMBEROFWAVES 未导出到新镜像的符号表。
`surface_fw_metadata.h` 中的总线地址来自配套 Surface WMFW 的元数据：
VIBEGEN 算法 ID=0xBD，XM 基址 723 words，控制偏移分别为 0、1、3 words。
对应地址为 0x02800B4C、0x02800B50、0x02800B58。
这些是直接寄存器地址，不是固件符号 ID。

不再写入旧的波形数量 28，也不写入猜测的数量 78。
测试在 DSP 启动后读取 NUMBEROFWAVES，只有返回 78 才继续自动触发。

## 波表及验证

波表按 32 位大端存储字读取；每条描述为 type、offset、length 三个字，
offset/length 的单位为存储字，0x00FFFFFF 为描述表终止标记。
解析同时检查描述表与数据区分离、载荷连续性、区域覆盖和长度边界。

| 区域 | 起始地址 | 提取字节数 | 条目数 | 全局索引 |
|---|---:|---:|---:|---|
| XM | 0x02800B60 | 2408 | 25 | 0～24 |
| YM | 0x03400000 | 4048 | 53 | 25～77 |

完整清单见 [waveforms.csv](waveforms.csv)，每项包含区域、局部索引、类型、
偏移、长度和载荷哈希。结构合法不等于实际振动已验证。

在工程根目录运行完整离线测试（Python 标准库及 PATH 中的本机 Clang）：

```powershell
python -B tools/surface_fw/verify.py --self-test
```

不带 `--self-test` 仅校验镜像、参考资料和配置，不需要 Clang。
单独执行策略 C 测试可运行 `python -B tools/surface_fw/test_policy.py`。
测试将实际策略模块与 BSP 调用记录桩编译为临时动态库；Windows 使用
Clang/lld 无 CRT 链接，不依赖 ESP-IDF、Surface 外部目录或硬件。

额外与原解包目录核对：

```powershell
python -B tools/surface_fw/verify.py --self-test --source-root D:/Documents/mcu-drivers
```

添加 `--write-report` 可刷新本目录的 CSV 清单及 `validation.json`。
默认不修改项目文件，C 测试只在临时目录生成并清理主机动态库。
校验包括镜像 SHA-256、Fletcher 校验、原始有序块哈希、
驱动关键符号、固件专用寄存器元数据、策略参考文件哈希和自动测试设置。
8 项反例测试覆盖截断、载荷损坏、错误符号、缺少终止标记、非法偏移、
越界长度、错误条目类型和错误条目数量。
另有 8 项实际 C 策略测试，覆盖全部 256 个设置值、空指针、关闭档零写入、
全部启用设置的两个事件、任一非法索引、有效索引边界、映射失败禁止触发、
触发失败原样返回且不重试。调用记录还检查整对映射、零衰减、GPIO 禁用、
映射先于触发及触发参数为 0。

`validation.json` 中 `first_index/last_index` 描述完整固件波表，
`automatic_test` 单独描述实际演示序列；`duration_argument_ms=0` 不是实测时长。
`policy_host_tests` 仅在执行 `--self-test` 时记录本次通过结果，
普通验证报告中为 null。主机桩测试不等于实际 mailbox 或 I²C 硬件测试。

## 构建及板上验收

使用 VS Code 插件配置的 ESP-IDF 6.0 环境和既有 `build` 目录增量构建。
本次未执行 clean/fullclean，未切换 sdkconfig 来重复构建测试模式，未刷写设备。
新增源文件触发 CMake 刷新构建规则；最终应用、bootloader 和分区大小检查通过。
产物为 `build/ESP32_HAPTIC_PRECISION_TOUCHPAD.bin`，实际大小及 SHA-256 记录在
[集成构建记录](integration_build.json)。

离线验证包含 8 项固件反例、8 项实际 C 策略、16 项事件／设置／描述符测试，
以及 11 个实际振动任务模拟场景和 8 个压力／模拟鼠标函数输入场景。
连续点击场景验证最早待确认心跳的 2 秒期限不会被后续点击延后。
BLE PTP 和独立测试入口另外通过现有编译参数的语法检查；未将这些模式编译成固件。
验证对象和结果见 `validation.json`；测试桩不能证明真实 I²C、ACK、DSP 或触感通过。

此前独立五档板测由用户报告完成；本次共用初始化与正常服务集成后尚未刷写或板测。
集成验收先检查 USB 点击、拖动、强度读写及保存，再检查 BLE／2.4G、休眠唤醒和
振动故障后的触控可用性，详见 [集成验收步骤](integration.md)。

如后续手动启用独立测试模式，关闭档应有两条 SKIP，第四档应触发 YM 36/29，
每轮约 20 秒；对实际触发观察 ACK、心跳和振动。关键失败后输出诊断并关闭升压，
等待人工复位，不重新下载。正常服务则锁定振动故障并结束振动任务，其他任务继续运行。
