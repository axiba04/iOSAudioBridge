# iOS Audio Bridge — ZL USB Audio Bridge

Windows x64 原生音频程序：接收 iPhone USB 音频，使用 ZLCompressor 原版界面和处理器，经 WASAPI 输出到耳机或 DAC。基于 C++20 / JUCE，无需 .NET 或 VST Host。这是社区集成预览版，不是 ZL-Audio 官方发行版。

```text
iPhone USB → QuickTime 仅音频握手 → PCM16 / 48 kHz / Stereo
           → SPSC → ZL Processor → 最终音量/静音 → WASAPI Shared → 耳机 / DAC
```

## 运行

仓库附带已构建的 [Windows x64 程序及运行依赖](dist/ZLUsbAudioBridge/) 和 [驱动离线安装包](dist/DriverInstallKit-x64-1.4.0.2.zip)。克隆或下载仓库后即可使用；驱动 ZIP 需完整解压后按安装说明操作。

运行 `dist/ZLUsbAudioBridge/ZLUsbAudioBridge.exe`，保留同目录的 `AudioBridge.UsbRestore.exe`、`libusb0.dll` 和许可证文件。

1. 沿用已验证的 Apple USB 支持与 libusb-win32 filter，连接手机、解锁并信任电脑；关闭其他占用同一手机的程序。
2. 刷新并选择手机和输出设备，点击“开始监听”。每次启动默认静音、监听音量 50%；需要出声时手动解除静音。
3. 压缩器参数可实时调整；“高级设置”中的“监听音量”只影响本程序最终输出。
4. 停止或关闭窗口后等待 USB 恢复完成。手机暂停播放时保持连接，恢复播放后自动继续。

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

仓库已包含 ZLCompressor、JUCE、Highway、zldsp_fft 的源码快照及集成修改，版本来源见 `third_party/ZLCompressor.pins.json`。

```powershell
# 检查源码，并在需要时下载、校验和解压便携 LLVM 22.1.0
./scripts/Prepare-ZLDependencies.ps1
# 构建公共底层、诊断工具、ZL 程序，执行回归测试并打包
./scripts/Build-ZLBridge.ps1
# 指定具有相同目录结构的便携 MSVC/SDK、Ninja、CMake 工具根目录
./scripts/Build-ZLBridge.ps1 -ToolRoot 'D:/path/to/audio-only-work'
# 只构建和测试，不更新发布目录
./scripts/Build-ZLBridge.ps1 -NoPublish
```

构建脚本默认使用 `D:/GreenSoftware/iPhoneMirror-v1.8.3-win-x64/audio-only-work` 中的 MSVC/Windows SDK、Ninja、CMake，以及 `.tools/llvm-22.1.0` 中的 Clang。下载 LLVM 时需要 curl 和 7z。
输出为 `build/zl-bridge` 与 `dist/ZLUsbAudioBridge`。默认运行 AudioCore.Tests、PcmProcessing.Tests、ZLBridge.Dsp 三组回归，不连接手机或打开音频端点。

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
| `src/ZLUsbAudioBridge/` | JUCE 程序、原版 ZL 编辑器集成、处理器适配及自测 |
| `src/AudioCore/` | USB 协议、设备发现、PCM 队列、音频引擎、WASAPI 和诊断 C ABI |
| `src/AudioBridge.UsbRestore/` | 独立 USB 配置恢复辅助程序 |
| `tests/`、`tools/AudioProbe/` | 协议 fixtures、Native/PCM/ABI 回归、命令行诊断 |
| `third_party/` | 上游源码快照、依赖、补丁、版本与许可证 |
| `scripts/` | 依赖准备、ZL 构建打包、稳定性测试及驱动备份与试切换 |

WPF 前端、旧发布脚本和一次性基线导入脚本已移除，需要追溯时可查看 Git 历史。当前构建直接使用已导入的源码和来源清单。

## 当前限制

- 输入固定为 48 kHz / PCM16 / 立体声，WASAPI 使用共享事件模式；不支持独占输出。
- Lookahead、Oversampling 和外部侧链固定关闭；预设导入也受相同约束。原版压缩器其他参数仍可调节。
- 延迟显示是软件队列、WASAPI padding 和 DSP 延迟的估计，不是实测端到端延迟。
- 尚未实现自动连接、开机启动和时钟漂移补偿。长时间游戏、目标 DAC 听感及异常交互验收仍待完成。

## 许可证

ZL 集成程序按 AGPL-3.0 提供，公共音频底层保留 GPL-3.0 声明；来源和各组件许可见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。分发二进制时需同时提供对应源码和构建材料。
