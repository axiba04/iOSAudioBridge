# iOS Audio Bridge — 第一阶段 MVP

**新增 ZL 原版界面集成预览版**：运行 `dist/ZLUsbAudioBridge/ZLUsbAudioBridge.exe`。
已接通 USB → ZL Processor → WASAPI，默认静音。见[使用与构建](docs/zl-bridge-running.zh-CN.md)和[集成验收](docs/zl-bridge-validation.zh-CN.md)。
以下内容仍描述保留的第一阶段 WPF 程序。

Windows 11 x64 / .NET 8 WPF / C++20 Native DLL。

```text
iPhone USB → QuickTime 仅音频握手 → EAT!/CMSampleBuffer
           → PCM16 / 48 kHz / Stereo → SPSC → WASAPI Shared → 耳机 / DAC
```

已实现 USB 手机探测、指定输出端点、开始/停止、音量/静音及基础运行统计。
PCM 始终在 Native 层，C# 只交换控制参数和状态。第一阶段没有压缩器。

## 运行

本机已构建的程序：`dist/iOSAudioBridge/iOSAudioBridge.exe`。
这是包含 .NET 8.0.28 的自包含 x64 目录，使用时保留整个目录。
首次构建和本机验收见 [验收记录](docs/mvp-validation.zh-CN.md)。

1. 保留此前 iPhoneMirror 仅音频测试已正常工作的 Apple USB 支持和 libusb-win32 filter 驱动。
   本程序不安装、更换或卸载驱动。关闭占用同一手机的 iPhoneMirror / UsbAudioOnly。
2. 连接手机，解锁并信任电脑；打开程序，点击“刷新设备”。
3. 选择手机和耳机 / DAC，再点击“开始监听”。播放手机游戏或音乐。
4. 点击“停止”后等待 USB 恢复；关闭窗口也会先停止并恢复。
   当前默认使用快速停止策略，本机多轮测试约需 10 秒，包括协议收尾、重枚举与普通配置确认。
   具体比较和保守时序回退方法见 [USB 停止优化记录](docs/usb-stop-optimization.zh-CN.md)。

界面只在启动和手动刷新时重新枚举设备；自动连接和开机启动属于后续阶段。
播放时拔掉手机或输出设备会结束会话并显示错误；重新连接后刷新、再次开始。
手机 B 站等播放器暂停时发出的 STOP 会进入“手机音频已暂停，保持连接等待恢复”，
音频重新到达后继续播放。WPF 无定时结束监听的限制。修复及验证见
[暂停/恢复记录](docs/pause-resume-fix.zh-CN.md)。
不应在恢复进行中强行终止进程。若提示普通 USB 配置未确认恢复，请重新插拔数据线。

WPF 日志默认写入 `%LOCALAPPDATA%/iOSAudioBridge/logs/audio-core.log`；
命令行日志默认写入 `%TEMP%/iOSAudioBridge-capture.log`。
可用 `IPHONE_MIRROR_LOG_FILE` 指定命令行日志路径。
输出设备选择保存在 `%LOCALAPPDATA%/iOSAudioBridge/settings.json`。

## 构建

需要 CMake 3.25+、Ninja、MSVC C++20 / Windows SDK、支持目标框架 net8.0-windows 的 .NET SDK。
本机使用已有的便携工具，不修改其原始目录：

```powershell
./scripts/Build.ps1
# 仅编译 Native 和运行回归测试
./scripts/Build.ps1 -NativeOnly
# 其他机器：从 x64 Developer PowerShell 运行，确保 cmake/ninja/dotnet 已在 PATH
./scripts/Build.ps1
# 或指定具有相同目录结构的便携工具根目录
./scripts/Build.ps1 -ToolRoot 'D:/path/to/audio-only-work'
```

首次发布需要从 NuGet 下载 .NET 8 的框架包/运行时；默认输出自包含包。
`-FrameworkDependent` 可改为依赖机器已安装的 .NET 8 Desktop Runtime。
`-SkipTests` 只用于已通过测试后的界面迭代。Native 使用静态 MSVC CRT。

`scripts/Import-Baseline.ps1` 仅用于首次建立来源快照，当前源码已经导入且完成移植，
**构建不需运行它**；它会拒绝覆盖已有 manifest。第三方目录已包含所有 Native 构建输入。

## 诊断和测试

```powershell
./build/native/bin/AudioProbe.exe --devices
./build/native/bin/AudioProbe.exe --outputs
./build/native/bin/AudioProbe.exe --silence-test 9
./build/native/bin/AudioProbe.exe --pause-silence-test 9
# 以下命令会切换手机 USB 配置，结束后自动恢复；索引从 0 开始，以当次枚举为准
./build/native/bin/AudioProbe.exe --capture-muted 35 0 9
./build/native/bin/AudioProbe.exe --capture 120 0 9
# 隐藏窗口检查，不启动 USB 音频会话；结果写入程序目录的 JSON
./dist/iOSAudioBridge/iOSAudioBridge.exe --interop-smoke
./dist/iOSAudioBridge/iOSAudioBridge.exe --ui-smoke
```

`--capture` 输出音量为 50%；`--capture-muted` 始终静音。Ctrl+C 会请求正常停止。
采集时长包含连接初始化时间，USB 恢复时间另外计算。

## 代码布局

| 路径 | 职责 |
|---|---|
| `src/iOSAudioBridge.App/` | WPF、P/Invoke、SafeHandle、设备选择、状态和音量 |
| `src/AudioCore/include/AudioCore.h` | 有版本/尺寸校验的 C ABI，UTF-16 字符串，x64 / pack 8 |
| `src/AudioCore/src/Protocol/`、`Media/`、`Pcm/` | QuickTime 拆包、音频握手、CMSampleBuffer 和严格 PCM 格式校验 |
| `src/AudioCore/src/Transport/`、`Device/` | 官方 usbmux 27015、配对信息、PnP、libusb0 私有 bulk 接口 |
| `src/AudioCore/src/Session/` | USB 接收线程、异常收尾、停止/恢复、状态机 |
| `src/AudioCore/src/Buffer/`、`Wasapi/` | SPSC、事件驱动共享模式、MMCSS Pro Audio、补静音、增益平滑和计量 |
| `src/AudioBridge.UsbRestore/` | 沿用成功分支的独立 USB 配置辅助进程 |
| `tests/`、`tools/AudioProbe/` | 协议 fixtures、并发队列/ABI 测试、命令行验收 |
| `third_party/iphone-mirror-baseline/` | 未改动来源文件、原本地补丁和 SHA-256 manifest |

`QtUsbTransport.h` 保留上游文件名，仅含 USB 公共类型；本程序不依赖 Qt 或 libusb-1.0。
原始文件清单、依赖与后续 ZL Compressor 集成设计见
[分析与计划](docs/upstream-analysis-and-mvp-plan.zh-CN.md)。

第二阶段最新路线：保留 ZLCompressor 原版界面，以自定义 JUCE Standalone 接入现有 USB 与 WASAPI 后端，
详见 [ZL Standalone 集成设计与实施计划](docs/zl-standalone-usb-integration-plan.zh-CN.md)。
目前仅完成设计，当前发布版仍是第一阶段 WPF 程序。

## 运行约定和当前限制

当前构建约 15 分钟静音实测：无 USB 断连，出现 1 次缓冲欠载，详见
[稳定性验证记录](docs/stability-validation.zh-CN.md)。

- 固定接收 48 kHz、有符号 little-endian packed PCM16、双声道；不符合时明确报错。
- 不发送 HPD1 / NEED；收到非请求视频即停止，保留视频包/字节计数。
- 默认 SPSC 容量 8192 帧，启动水位 3072，高水位 4096；消费端追赶时丢弃最旧积压，
  满队列时生产端丢弃新到帧，均记入丢帧计数。容量不等于实际延迟。
- WASAPI 优先 IAudioClient3 支持的最小共享周期；失败则使用共享自动格式转换。
  不强制将 Windows/DAC 的硬件混音格式设为 48 kHz。无独占模式。
- 实时渲染中无队列锁、动态分配或日志 I/O；USB 协议解析线程目前仍分配和复制 packet。
- 音量左右共同增益，并在 5 ms 量级内平滑；峰值表是每个渲染块的 DSP 前/后 peak，非 RMS 或峰值保持表。
- 延迟显示为软件队列 + 最近提交的 WASAPI padding 的估计，不是实测端到端延迟；
  `GetStreamLatency` 单独保留于 C ABI 状态，避免重复相加。
- 连续 250 ms 没有 USB PCM 且队列排空后显示为推测空闲；此前短缺会计入欠载。
  软件无法仅凭“没有 PCM”可靠区分手机静默和 USB 传输停顿。
- 停止/销毁同步等待 USB 恢复，调用方需放在工作线程。所有句柄必须在卸载 DLL 前销毁。
  WPF 关闭期间会等待正在执行的操作，避免开始/停止与 SafeHandle 释放交错。
- 只有主机主动停止进入收尾后，收到有效手机 STOP，连续 250 ms 无音频尾包且没有未拼完的数据包，才可提前结束协议收尾；
  缺少这些条件时保留原 6 秒上限（开始收尾前手机已停止则为 1 秒）。自动恢复观察窗口为 1 秒，
  设备身份校验、独立辅助程序和完整恢复确认保持原逻辑。设置 `AB_USB_STOP_MODE=conservative`
  后启动程序可恢复原收尾逻辑和 3 秒自动恢复观察窗口；无需改驱动。
- 手机端暂停保留 USB 和 WASAPI 会话；恢复时先补足启动水位再消耗 PCM，等待期间输出静音。
  该等待和已确认的暂停不计欠载，正常连续播放中的真实短缺仍统计。
- 未实现时钟漂移补偿、自动重连、独占输出、压缩器或开机启动。
  30 分钟 PUBG、至少 2 小时运行、DAC 出声/左右定位、拔线/端点移除仍待验收。

## 许可证

GPL-3.0；来源和依赖许可证见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
发布到其他地方时同时提供对应源代码和构建脚本。Apple 驱动不随本项目分发。
