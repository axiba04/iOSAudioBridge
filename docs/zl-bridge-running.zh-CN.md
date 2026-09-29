# ZL USB Audio Bridge 使用与构建

2026-09-24：Windows x64 本地集成预览版。保留 ZLCompressor 原版编辑器、压缩器和分析器；输入改为 iPhone USB 仅音频流。不是 ZL-Audio 官方发行版。

## 运行

从 [GitHub Releases](https://github.com/axiba04/iOSAudioBridge/releases) 下载并完整解压 Windows x64 程序包，打开 `ZLUsbAudioBridge/ZLUsbAudioBridge.exe`；自行构建的程序位于 `dist/ZLUsbAudioBridge/`。保留同目录的 `AudioBridge.UsbRestore.exe`、`AudioBridge.UsbRepair.exe` 与 `libusb0.dll`。无需 VST Host 或 .NET。

当前包使用 libusb-win32 1.4.0.2 DLL。系统驱动单独维护，本机升级及恢复方法见[驱动升级记录](driver-upgrade-evaluation.zh-CN.md)。

1. 沿用这台机器已验证的 Apple USB 支持与 libusb-win32 filter；本程序不安装驱动。
2. 连接并解锁手机，退出其他监听程序；刷新并选择手机、输出设备。
3. 选齐设备后点击“开始监听”。未检测到手机或音频输出时，下拉框显示明确提示，“开始监听”不可用。首次打开及每次重启均默认静音，监听音量为 50%。主界面保留“静音输出”；点击“高级设置”可在底部展开“监听音量”和诊断信息，收起面板不会改变音量。它只调节本软件最终输出，不改变 Windows 音量。解除静音由使用者手动操作；自动验收始终静音。
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

Processor 状态与设备选择保存在 `%APPDATA%/iOSAudioBridge-ZL/`；原版界面属性使用重命名后的 `ZL USB Audio Bridge` 产品路径，与原 ZLCompressor 隔离。
日志可通过环境变量 `IPHONE_MIRROR_LOG_FILE` 指定；默认位置为 `%TEMP%\iOSAudioBridge-capture.log`。连接失败时，`usb_probe` 记录打开句柄、读取序列号、读取配置的具体阶段与错误码；`usb_repair` 记录手动修复结果。日志中的设备标识使用会话内指纹。详见[连接诊断与修复](usb-device-repair.zh-CN.md)。

顶部控件和底部区域跟随 ZL 的背景与文字主题颜色。状态栏显示连接状态、静音和监听音量；缓冲、欠载、缺失帧和延迟信息位于“高级设置”。刷新设备时会尽量保留当前选择。

高级设置中的延迟仅估算队列、WASAPI padding 和 ZL 报告的处理延迟，不含手机端、USB 到达前等待、驱动/DAC 全部延迟；0 帧 DSP 不是端到端 0 ms。

## 构建

在项目根目录运行：

```powershell
./scripts/Prepare-ZLDependencies.ps1
./scripts/Build-ZLBridge.ps1
```

仓库已包含 ZL 及其依赖的源码快照和集成修改，无需额外克隆或应用补丁。
准备脚本检查源码是否齐全；缺少 LLVM 时使用 curl、7z 下载 LLVM 22.1.0，校验压缩包 SHA-256 后仅解压到 `.tools`，不执行安装器或修改系统 PATH。
构建脚本默认使用本机 `D:/GreenSoftware/iPhoneMirror-v1.8.3-win-x64/audio-only-work` 内的 MSVC/SDK、Ninja、CMake；其他机器需通过 `-ToolRoot` 提供相同目录结构。
固定版本见 `third_party/ZLCompressor.pins.json`。Clang 编译使用 MSVC STL 和 Windows SDK；C++20、SSE2、静态 CRT。

构建目录为 `build/zl-bridge`，本地发布目录为 `dist/ZLUsbAudioBridge`。`dist/` 不纳入 Git，预编译程序和驱动 ZIP 作为 Release 附件分发。
默认执行 Native、PCM 适配和 ZL DSP 三组回归；合成 DSP 检查不打开音频端点、不连接 USB。
发布目录包含依赖许可和来源清单。对外分发时需同时提供本项目对应源码、固定依赖及构建材料，不能只发 EXE。

## 静音诊断入口

```powershell
./dist/ZLUsbAudioBridge/ZLUsbAudioBridge.exe --self-test --result E:/Code/iOSAudioBridge/build/dsp.json
./dist/ZLUsbAudioBridge/ZLUsbAudioBridge.exe --ui-smoke --result E:/Code/iOSAudioBridge/build/ui.json
./dist/ZLUsbAudioBridge/ZLUsbAudioBridge.exe --ui-smoke --ui-empty --result E:/Code/iOSAudioBridge/build/ui-empty.json
./dist/ZLUsbAudioBridge/ZLUsbAudioBridge.exe --wasapi-smoke --result E:/Code/iOSAudioBridge/build/wasapi.json
# 仅在手机已准备好时运行；使用已保存/默认输出，强制静音
./dist/ZLUsbAudioBridge/ZLUsbAudioBridge.exe --capture-muted 60 --result E:/Code/iOSAudioBridge/build/capture.json
```

UI smoke 只枚举 USB、用合成信号绘制隐藏界面，并输出主界面、展开高级设置（`-advanced.png`）、最小窗口（`-minimum.png`）截图；附加 `--ui-empty` 可在枚举后清空测试用设备列表，验证无手机和无输出时的提示及按钮状态，不修改系统设备。WASAPI smoke 向默认端点提交静音合成音频，检查 3 次暂停/恢复。
capture-muted 包含连接启动时间，音频持续时间可能略短于指定秒数，结束还需等待 USB 释放；同时输出每秒 JSONL。
测试程序退出码 0 不代替检查报告中的欠载、缺失帧、丢帧、视频包、静音和最终 USB 状态。

验收范围与原始结果见项目中的 `docs/zl-bridge-validation.zh-CN.md`。本轮不宣称完成 15 分钟对照、30 分钟 PUBG、2 小时稳定性或 pureaural 设备验收。
