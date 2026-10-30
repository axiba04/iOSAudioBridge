# AudioBridge

Windows x64 原生音频程序：接收 iPhone / iPad 的 USB、AirPlay 音频，以及 Android 的 scrcpy USB、Wi-Fi 音频，使用 ZLCompressor 原版界面和处理器，经 WASAPI 输出到 Windows 耳机或 DAC。基于 C++20 / JUCE，无需虚拟声卡、.NET 或 VST Host。这是社区集成预览版，不是 ZL-Audio 官方发行版。

iOS 与 Android 接收功能共存在同一程序中，每次监听一个输入，切换前先停止；当前不混合多台手机的声音。项目原名 iOSAudioBridge，现已改名 AudioBridge；GitHub 地址暂沿用原仓库。

```text
iPhone USB → QuickTime 仅音频握手 → PCM16 / 48 kHz / Stereo
iPhone AirPlay → 独立接收进程解码 → PCM16 / 44.1 或 48 kHz / Stereo
Android USB / Wi-Fi → ADB → scrcpy-server 5.0.1 → PCM16 / 48 kHz / Stereo
           → SPSC → ZL Processor → 最终音量/静音 → WASAPI Shared → Windows 耳机 / DAC
```

## 运行

下载 [AudioBridge v0.3.0-preview.1 Windows x64 程序包](https://github.com/axiba04/iOSAudioBridge/releases/download/v0.3.0-preview.1/AudioBridge-v0.3.0-preview.1-win-x64.zip)，完整解压后运行 `AudioBridge/AudioBridge.exe`。iOS USB 按需下载同版 [驱动离线安装包](https://github.com/axiba04/iOSAudioBridge/releases/download/v0.3.0-preview.1/DriverInstallKit-x64-1.4.0.2.zip)。[Release 页面](https://github.com/axiba04/iOSAudioBridge/releases/tag/v0.3.0-preview.1) 提供校验文件、改动和验收范围。旧 `v0.2.0-preview.5` 只包含 iOS 功能。本地构建输出仍为 `dist/AudioBridge/AudioBridge.exe`，`dist/` 不纳入 Git。

运行 `AudioBridge.exe`，完整保留同目录的 `AudioBridge.UsbRestore.exe`、`AudioBridge.UsbRepair.exe`、`libusb0.dll`、`Wireless/`、`Android/` 和许可证文件。Android 模式无需安装 Apple USB filter。

### Android scrcpy：USB / Wi-Fi

- USB：Android 11+ 开启开发者选项和 USB 调试，连接数据线，在手机允许调试；选择 **Android USB**，刷新并选择手机及音频输出，再开始监听。
- Wi-Fi：电脑和手机连接同一局域网，在手机开启“无线调试”；选择 **Android Wi-Fi → 无线连接**。首次填写配对地址、六位配对码及连接地址，点击“配对并连接”；已配对只需填写连接地址并点击“连接”。配对端口与连接端口通常不同。
- Android 11 启动时需解锁屏幕。捕获系统输出，手机本地播放通常静音；停止监听后恢复。无需开启视频或屏幕控制。
- 固定使用 scrcpy-server 5.0.1 原始 PCM；不经过桌面 scrcpy 播放器。USB 默认启动缓冲约 21 ms，Wi-Fi 约 64 ms，这不是端到端延迟。

完整步骤、依赖来源、限制与验收边界见 [Android 使用说明](docs/android-scrcpy.zh-CN.md)。Android 真机 USB／Wi-Fi 出声和长期稳定性待验收；协议、进程和 DSP 路由已通过自动测试。

### iOS USB

1. 沿用已验证的 Apple USB 支持与 libusb-win32 filter，连接手机、解锁并信任电脑；关闭其他占用同一手机的程序。
2. 刷新并选择手机和输出设备，点击“开始监听”。每次启动默认非静音、监听音量 50%；需要静音时勾选“静音输出”。
3. 压缩器参数可实时调整；“高级设置”中的“监听音量”只影响本程序最终输出。
4. 停止或关闭窗口后等待 USB 恢复完成。手机暂停播放时保持连接，恢复播放后自动继续。

### AirPlay 仅音频

请使用 `v0.2.0-preview.5` 或更新版本。`preview.4` 的接收模式会拒绝纯音频连接，导致 iPhone/iPad 提示“无法连接”；`preview.5` 修复此问题。

1. 电脑与 iPhone/iPad 连接同一局域网，完整保留程序旁的 `Wireless/` 目录。
2. 左上角输入选择 **iOS AirPlay**，输出选择 Windows 上的耳机或 DAC，点击“开始监听”。
3. 在 iPhone 控制中心的“正在播放”面板打开 **AirPlay 音频**列表，选择 **AudioBridge**，再播放音乐。
4. 音频直接进入 ZLCompressor；压缩器、监听音量和静音照常使用。手机端 AirPlay 音量也会作用于最终输出。

使用“正在播放”的音频路由即可只传音频，无需开启屏幕镜像。一次处理一台发送设备；切换 USB / AirPlay 前先停止。AirPlay 模式不需要 USB filter 或 Bonjour。

若发现不到接收端或连接后无声，检查同一局域网及 Windows 防火墙是否允许 `Wireless/AudioBridge.AirPlayHost.exe` 在专用网络收发。控制端口为 TCP 5002/7002，媒体端口动态协商；仅放行两个固定端口可能仍无声。程序不自动修改防火墙。

AirPlay 的发送端缓冲和无线网络延迟不计入界面的“软件延迟估计”。对游戏实时监听仍建议 USB。编译、自动回归与实机记录见[集成验收](docs/zl-bridge-validation.zh-CN.md)；`preview.5` 已通过 iPad 连接及耳机有声复测，长时间无线稳定性仍待验证。

iPad 停止后无法重新连接的问题已加入自动恢复，触发补救时可能需要约 25 秒，见[修复与实机验证](docs/usb-restore-ipad-fix.zh-CN.md)。

换机后连接失败、退出重开仍无效时，可在停止状态选择故障手机，点击“修复 USB”，按 Windows 提示授权。程序只重启所选手机的设备节点，并验证 USB 通信；成功后手动开始监听。驱动检查 `READY` 只表示安装与设备登记通过，不能替代实际通信测试。见[连接诊断与手动恢复](docs/usb-device-repair.zh-CN.md)。

详细操作、预设、日志和静音诊断入口见[使用与构建](docs/zl-bridge-running.zh-CN.md)。

当前包使用 libusb-win32 1.4.0.2 用户态运行库；系统 filter 需单独安装。
本机驱动切换结果、备份与回退命令见[驱动升级记录](docs/driver-upgrade-evaluation.zh-CN.md)。
新电脑请使用精简后的 [DriverInstallKit 安装说明](DriverInstallKit/README.zh-CN.md)，依次运行
`01-AppleSupport.cmd`、`02-iPhoneFilter.cmd`、`03-Check.cmd`。可用
`./scripts/Build-DriverInstallKit.ps1` 生成独立离线 ZIP；完整首次安装仍待新电脑验收。
同一电脑可重复运行第二步，按设备编号分别登记多台 iPhone / iPad，共用一套驱动；
第三步会逐台报告状态。多设备实机验收尚未完成，软件单窗口仍只采集一台。
已有已验证的 libusb 1.2.6.0 可运行 `04-UpgradeLibUsb.cmd`，按提示备份、拔线升级；
`05-RestoreLibUsb.cmd` 用于恢复本电脑的升级备份。这两个入口无需开发工具。

## 构建

仓库已包含 ZLCompressor 0.6.1、JUCE 9.0.2、Highway、zldsp_fft 的源码快照及集成修改，版本来源见 `third_party/ZLCompressor.pins.json`。构建时从该清单读取 ZL 版本与提交号。

```powershell
# 检查源码，并在需要时下载、校验和解压便携 LLVM 22.1.0
./scripts/Prepare-ZLDependencies.ps1
# 构建公共底层、诊断工具、ZL 程序，执行回归测试并打包
./scripts/Build-AudioBridge.ps1
# 指定具有相同目录结构的便携 MSVC/SDK、Ninja、CMake 工具根目录
./scripts/Build-AudioBridge.ps1 -ToolRoot 'D:/path/to/audio-only-work'
# 只构建和测试，不更新发布目录
./scripts/Build-AudioBridge.ps1 -NoPublish
# 升级依赖或移动工具链目录后，重新生成 CMake 配置
./scripts/Build-AudioBridge.ps1 -Fresh
```

构建脚本优先使用 `.tools/build-tools` 的 MSVC/Windows SDK、Ninja、CMake，缺失时回退至原 `D:/GreenSoftware/iPhoneMirror/audio-only-work`，Clang 位于 `.tools/llvm-22.1.0`。旧 `Build-ZLBridge.ps1` 入口仍有效。构建自动调用 `Prepare-Scrcpy.ps1` 下载官方 Windows 包并校验 SHA-256，只提取 ADB、服务器及许可证；固定版本见 `third_party/scrcpy.pins.json`。下载 LLVM 时需要 curl 和 7z。
输出为 `build/zl-bridge` 与 `dist/AudioBridge`。默认运行 AudioCore.Tests、PcmProcessing.Tests、Scrcpy.Tests、ZLBridge.Dsp、AirPlay.Runtime、AirPlay.Tests、AirPlay.ReceiverTests 七组回归，不连接手机或打开音频端点。真实接收 DLL 的握手测试会临时使用 TCP 5002/7002；运行测试前停止正在监听的桥接程序。

## 诊断与稳定性测试

`AudioProbe` 和 C ABI 仍用于公共音频底层的直通诊断；其测试不覆盖 ZL DSP 或界面。

```powershell
./build/zl-bridge/bin/AudioProbe.exe --devices
./build/zl-bridge/bin/AudioProbe.exe --outputs
# 手机已准备好时运行；索引以本次枚举为准，全程静音
./scripts/Test-Stability.ps1 -Seconds 900 -DeviceIndex 0 -OutputIndex 2
./scripts/Summarize-Stability.ps1 -ResultDirectory '<本轮生成的结果目录>'
```

ZL 集成测试结果与边界见[集成验收](docs/zl-bridge-validation.zh-CN.md)。历史测试记录和原始证据保留在 `docs/`，其中第一阶段 WPF 的命令、路径及结果仅供追溯。

## 代码布局

| 路径 | 职责 |
|---|---|
| `src/AudioBridge/` | JUCE 程序、四种输入、无线配对界面、ZL 编辑器集成及自测 |
| `src/AudioCore/` | iOS USB / AirPlay / Android scrcpy、ADB、PCM 队列、音频引擎、WASAPI 和诊断 C ABI |
| `src/AudioBridge.UsbRestore/` | 独立 USB 配置恢复辅助程序 |
| `src/AudioBridge.AirPlayHost/` | 独立 AirPlay 接收进程构建，源码及运行库固定在 `third_party/iPhoneMirror-AirPlay/` |
| `tests/`、`tools/AudioProbe/` | 协议 fixtures、Native/PCM/ABI 回归、命令行诊断 |
| `third_party/` | 上游源码快照、依赖、补丁、版本与许可证 |
| `scripts/` | 依赖准备、ZL 构建打包、稳定性测试及驱动备份与试切换 |

WPF 前端、旧发布脚本和一次性基线导入脚本已移除，需要追溯时可查看 Git 历史。当前构建直接使用已导入的源码和来源清单。

## 当前限制

- USB 为 48 kHz，AirPlay 支持 44.1 / 48 kHz；均为 PCM16 / 立体声，WASAPI 使用共享事件模式；不支持独占输出。
- Lookahead、Oversampling 和外部侧链固定关闭；预设导入也受相同约束。原版压缩器其他参数仍可调节。
- 延迟显示是软件队列、WASAPI padding 和 DSP 延迟的估计，不是实测端到端延迟。
- 尚未实现自动连接、开机启动和时钟漂移补偿。长时间游戏、目标 DAC 听感及异常交互验收仍待完成。

## 许可证

ZL 集成程序按 AGPL-3.0 提供，公共音频底层保留 GPL-3.0 声明；来源和各组件许可见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。分发二进制时需同时提供对应源码和构建材料。
