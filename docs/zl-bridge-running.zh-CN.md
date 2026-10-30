# AudioBridge 使用与构建

2026-10-30：Windows x64 本地集成预览版。保留 ZLCompressor 原版编辑器、压缩器和分析器；输入支持 iOS USB、iOS AirPlay、Android USB / Wi-Fi（scrcpy）。每次监听一个输入，停止后切换。不是 ZL-Audio 官方发行版。

Android 连接、无线配对、固定依赖版本与实机验收边界见 [Android 使用说明](android-scrcpy.zh-CN.md)。Android 无需 Apple USB filter；旧的 iOS Release 程序不包含 Android 功能。

## AirPlay 音频

请使用 `v0.2.0-preview.5` 或更新版本。`preview.4` 误用上游仅镜像模式，会以 RTSP 403 拒绝纯音频连接；表现为能看到 **ZL Audio Bridge**，但 iPhone/iPad 提示“无法连接”。更新时退出旧程序，完整解压新版后重新启动监听并选择接收端。

完整解压并保留 `Wireless/`。电脑与手机连接同一局域网，在程序左上角选择 **iOS AirPlay**，选择 Windows 耳机 / DAC，点击“开始监听”。状态显示接收服务就绪后，在 iPhone/iPad 的控制中心“正在播放”面板打开 AirPlay 音频列表，选择 **AudioBridge**。不要用“屏幕镜像”入口发起纯音频传输。

接收进程将解码的 PCM 直接交给 ZLCompressor，再经最终音量和 WASAPI 输出。支持 44.1 / 48 kHz、PCM16、立体声；ZL 按源采样率处理，设备格式转换交给 WASAPI。发送端 AirPlay 音量与本程序监听音量相乘，均作用于压缩处理后的输出。默认监听音量仍为 50%。

仅第一台发出音频的手机进入当前压缩器，其他手机不会混音；已选手机断开后可接收下一台。暂停时保留连接，断开后服务继续等待。停止监听会关闭接收进程和网络服务，再切回 USB 即可使用原链路。切换预设时仍按现有流程停止并重启输入，AirPlay 发送端可能需要重新选择接收端。

AirPlay 不需要 USB filter、手机 USB 信任或 Bonjour。Windows 防火墙需允许 `Wireless/AudioBridge.AirPlayHost.exe` 在专用局域网通信；控制端口为 TCP 5002/7002，媒体端口动态协商，发现依赖 mDNS。程序不自动新增防火墙规则。发现不到设备时先检查同一网段、无线客户端隔离及防火墙；接收进程退出时状态栏会报错，可停止后重试。

AirPlay 延迟包含发送端缓冲和网络抖动，界面延迟数字仅反映本机队列、WASAPI 和 DSP。对游戏低延迟监听优先使用 USB。`preview.5` 已通过真实接收 DLL 握手回归，以及 iPadOS 26.6.2 的连接和耳机有声复测；其他设备兼容性及长时间无线稳定性仍待验证。

2026-10-30：AirPlay 默认缓冲已从 1024 / 2048 帧低延迟实验档恢复为原来的起播预填充 4096 帧、队列高水位 6144 帧，为网络抖动预留更多余量。起播预留在 44.1 / 48 kHz 下约为 92.9 / 85.3 ms；实际阈值还受 WASAPI 缓冲和单包帧数限制，这些数值不是端到端延迟。接收日志的 `airplay_source buffer_config` 会记录本次采用的帧数。调用方显式指定的缓冲值仍然有效，iOS USB 和 Android 默认缓冲不变。

无设备诊断：`Wireless/AudioBridge.AirPlayHost.exe --check-runtime` 返回 0 表示接收库可加载；40 表示 Windows 代码完整性拒绝映像，41 表示依赖缺失或加载失败，42 表示接口不匹配。接收日志复用本程序的 capture 日志，条目前缀为 `airplay_host` / `airplay_source`。

## 运行

从 [v0.3.0-preview.1 Release](https://github.com/axiba04/iOSAudioBridge/releases/tag/v0.3.0-preview.1) 下载程序包并完整解压，运行 `AudioBridge/AudioBridge.exe`；本地构建程序位于 `dist/AudioBridge/AudioBridge.exe`。保留同目录的 `AudioBridge.UsbRestore.exe`、`AudioBridge.UsbRepair.exe`、`libusb0.dll`、`Wireless/` 与 `Android/`。无需 VST Host 或 .NET。旧 `v0.2.0-preview.5` 为 iOS 版本，Android 输入需使用本次新版本。

当前包使用 libusb-win32 1.4.0.2 DLL。系统驱动单独维护，本机升级及恢复方法见[驱动升级记录](driver-upgrade-evaluation.zh-CN.md)。

1. 沿用这台机器已验证的 Apple USB 支持与 libusb-win32 filter；本程序不安装驱动。
2. 连接并解锁手机，退出其他监听程序；刷新并选择手机、输出设备。
3. 选齐设备后点击“开始监听”。未检测到手机或音频输出时，下拉框显示明确提示，“开始监听”不可用。首次打开及每次重启均默认非静音，监听音量为 50%；需要静音时勾选主界面的“静音输出”。点击“高级设置”可在底部展开“监听音量”和诊断信息，收起面板不会改变音量。它只调节本软件最终输出，不改变 Windows 音量。自动验收始终静音。
4. 原版 Threshold、Ratio、Attack、Release、Knee、Makeup 可实时调整。原版曲线/电平显示处理器的信号；高级设置中的“设备输出”是最终音量和静音后的电平。因此静音时原版分析器仍有输入。
5. 手机暂停播放会保持 USB，恢复后重新预填充缓冲。常规运行没有定时停止；断线或输出失败会结束会话并显示原因。
6. 点击停止或关闭窗口后等待 USB 恢复完成。部分 iPad 在切回普通配置后仍需一次自动重新枚举，停止可能需要约 25 秒；看到“已停止，USB 已释放”后即可再次连接。修复说明见项目文档 `docs/usb-restore-ipad-fix.zh-CN.md`。
7. 换机后 USB 连接失败、重开软件仍无效时，关闭其他使用该手机的软件，待本程序停止后选中故障手机，点击“修复 USB”并完成管理员授权。按钮只在未采集且有手机可选时启用，不要求已选择音频输出。修复不会开始监听，成功后手动点击“开始监听”。Windows 仍可识别、但 Apple 服务未发现的手机显示“USB 待检查”，也可以选中修复。设备未登记 filter 时先完成驱动安装。

当前仅支持 48 kHz / PCM16 / 立体声 USB 输入，内部转为双通道 float 调用 ZL，再转 PCM16 输出。
WASAPI 使用共享事件模式和 MMCSS Pro Audio。选定端点不支持原生格式时允许系统转换。
自动连接、开机启动、独占模式尚未加入。

## 游戏调试预设

“PUBG 调试预设”：Threshold −18 dB，Ratio 4:1，Attack 2 ms，Release 100 ms，Knee 6 dB，Makeup 0 dB；L/R Max、100% 联动、100% Wet。
这只是待实际游戏试听调整的起点，不保证所有枪声峰值均被限制；零 lookahead 的压缩器不能预测瞬态。

本构建固定 Lookahead 0、Oversampling Off、外部侧链 Off，对应控件禁用。预设导入同样先归一化这些参数。
其余原版功能可以改变声音，包括 Side Out、Delta 和侧链 EQ，请留意当前状态。
整份预设/游戏预设在停止音频线程、完成 USB 恢复后应用；若原先在监听则重新连接。六项旋钮的实时调整不会重连。

Processor 状态与设备选择保存在 `%APPDATA%/AudioBridge/`；首次正常启动会从旧 `%APPDATA%/iOSAudioBridge-ZL/` 复制尚不存在的设置，保留原文件。原版界面属性和预设使用 `AudioBridge` 产品路径，与原 ZLCompressor 隔离。
日志可通过环境变量 `IPHONE_MIRROR_LOG_FILE` 指定；默认位置为 `%TEMP%\iOSAudioBridge-capture.log`。连接失败时，`usb_probe` 记录打开句柄、读取序列号、读取配置的具体阶段与错误码；`usb_repair` 记录手动修复结果。日志中的设备标识使用会话内指纹。详见[连接诊断与修复](usb-device-repair.zh-CN.md)。

顶部控件和底部区域跟随 ZL 的背景与文字主题颜色。状态栏显示连接状态、静音和监听音量；缓冲、欠载、缺失帧和延迟信息位于“高级设置”。刷新设备时会尽量保留当前选择。

高级设置中的延迟仅估算队列、WASAPI padding 和 ZL 报告的处理延迟，不含手机端、USB 到达前等待、驱动/DAC 全部延迟；0 帧 DSP 不是端到端 0 ms。

## 构建

在项目根目录运行：

```powershell
./scripts/Prepare-ZLDependencies.ps1
./scripts/Build-AudioBridge.ps1
```

仓库已包含 ZLCompressor 0.6.1、JUCE 9.0.2 及其余依赖的源码快照和集成修改，无需额外克隆或应用补丁。
准备脚本检查源码是否齐全；缺少 LLVM 时使用 curl、7z 下载 LLVM 22.1.0，校验压缩包 SHA-256 后仅解压到 `.tools`，不执行安装器或修改系统 PATH。
构建脚本优先使用 `.tools/build-tools` 内的 MSVC/SDK、Ninja、CMake，缺失时回退到原 `D:/GreenSoftware/iPhoneMirror/audio-only-work`；其他机器通过 `-ToolRoot` 提供相同目录结构。升级依赖或移动工具链目录后，使用 `./scripts/Build-AudioBridge.ps1 -Fresh` 重新生成 CMake 配置。构建会自动下载并校验固定 scrcpy 运行库；旧 `Build-ZLBridge.ps1` 入口仍可用。
固定版本见 `third_party/ZLCompressor.pins.json`。Clang 编译使用 MSVC STL 和 Windows SDK；C++20、SSE2、静态 CRT。

构建目录为 `build/zl-bridge`，本地发布目录为 `dist/AudioBridge`。`dist/` 不纳入 Git。
默认执行 Native、PCM 适配、scrcpy 协议/进程、ZL DSP、AirPlay 运行库加载、AirPlay IPC 和真实 DLL 纯音频握手七组回归；不打开音频端点、不连接 USB。握手测试覆盖 FairPlay、非镜像会话 SETUP、type 96 音频端口协商、RECORD、TEARDOWN 和重新连接；会临时使用 TCP 5002/7002，测试前需停止正在监听的桥接程序。DSP 检查包含 Normal→High→Normal 的 FFT 历史缓冲切换、频率/幅度、44.1↔48 kHz 切换后的真实压缩曲线和零 DSP 延迟检查。
发布目录包含依赖许可和来源清单。对外分发时需同时提供本项目对应源码、固定依赖及构建材料，不能只发 EXE。

## 静音诊断入口

```powershell
./dist/AudioBridge/AudioBridge.exe --self-test --result ./build/dsp.json
./dist/AudioBridge/AudioBridge.exe --ui-smoke --result ./build/ui.json
./dist/AudioBridge/AudioBridge.exe --ui-smoke --ui-empty --result ./build/ui-empty.json
./dist/AudioBridge/AudioBridge.exe --ui-smoke --airplay --result ./build/ui-airplay.json
./dist/AudioBridge/AudioBridge.exe --wasapi-smoke --result ./build/wasapi.json
./dist/AudioBridge/AudioBridge.exe --wasapi-smoke --wasapi-rate 44100 --result ./build/wasapi-44100.json
# 仅在手机已准备好时运行；使用已保存/默认输出，强制静音
./dist/AudioBridge/AudioBridge.exe --capture-muted 60 --result ./build/capture.json
# AirPlay 静音实机验收：运行后在手机上选择接收端并播放
./dist/AudioBridge/AudioBridge.exe --airplay --capture-muted 60 --result ./build/capture-airplay.json
```

UI smoke 枚举设备、用合成信号绘制隐藏界面，并输出主界面、展开高级设置（`-advanced.png`）、最小窗口（`-minimum.png`）截图；附加 `--airplay` 检查 AirPlay 入口（不启动接收服务、不枚举 USB），附加 `--ui-empty` 可清空测试用设备列表，验证无设备时的提示及按钮状态。WASAPI smoke 向默认端点提交静音合成音频，检查 3 次暂停/恢复；`--wasapi-rate 44100` 验证 AirPlay 常见采样率。
capture-muted 包含连接启动时间，音频持续时间可能略短于指定秒数，结束还需等待 USB 释放；同时输出每秒 JSONL。
测试程序退出码 0 不代替检查报告中的欠载、缺失帧、丢帧、视频包、静音和最终 USB 状态。

验收范围与原始结果见项目中的 `docs/zl-bridge-validation.zh-CN.md`。本轮不宣称完成 15 分钟对照、30 分钟 PUBG、2 小时稳定性或 pureaural 设备验收。
