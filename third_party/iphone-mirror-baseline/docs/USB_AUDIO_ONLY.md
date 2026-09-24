# USB 纯音频实验版

基于官方 v1.8.3（commit `1270c443a29457751cadd20f6d7bbe1ce213b9db`）。

## GUI 版

工作区中运行 `Start-UsbAudioGui.cmd`，启动 `GuiAudioOnly/iPhoneMirror.exe`。
选择 USB 设备，在「设置 → 有线投屏协议」中选择「仅声音（USB）」，点击「开始播放声音」。
中央音乐界面显示音频包和视频包计数，音量/静音复用原有控制；纯音频模式关闭画面工具和视频输出入口。
停止后可切回原来的镜像模式。模式按设备在当前运行期间保留，重启后需重新选择。

GUI 将模式值 3 传入 `CaptureOptions.reserved[2]`，使用与命令行实测相同的核心 DLL。
发布目录自带 .NET 运行时及原版可选组件，原版安装文件不被覆盖。
模拟 WPF 测试已验证参数、显示布局、模式切换、设备隔离、AirPlay 回归和本地化；GUI 实机播放待用户手动测试。
完整旧逻辑测试受目录 ACL 权限限制未能跑完，细节见 `GuiAudioOnly/验证记录.md`。

当前工作区使用 `audio-only-work/Build-AudioOnlyGui.ps1 -Tests` 编译测试，
运行 `IPhoneMirror.App.Runtime.Tests.exe --usb-audio-only-ui-tests` 执行不连接手机的专项测试，
使用 `audio-only-work/Build-AudioOnlyGui.ps1` 发布并补齐 GUI 运行文件。

## 使用

1. 退出正在连接同一手机的 iPhoneMirror/其他 USB 镜像程序。
2. 用 USB 连接 iPhone，解锁并信任电脑。
3. 双击发布目录外的 `Start-UsbAudioOnly.cmd`。
4. 在手机上播放音乐。程序会显示音频包、视频包、采样率和声道数。
5. 按 **M** 静音/恢复，**+ / -** 调节电脑端音量，**Q** 正常停止并退出。

请使用 Q 退出，让原有 USB 恢复流程执行完毕。多台手机连接时，程序要求选择设备。
只有音频包持续增加、视频包一直为零且电脑确实出声，才代表实机播放成功。
不要同时运行原版和实验版采集同一手机。

## 验证边界

这是请求“手机不发送视频”的实验实现，不是隐藏窗口或丢弃视频的方案。
它不会发送 HPD1（启动显示）或 NEED（请求视频帧）。收到非请求的视频包时，计数并停止，明确报告失败。
当前手机已于 2026-09-22 实机验证：约 150 秒收到 7021 个音频包，视频包和视频 payload 字节均为零，正常退出并恢复 USB。其他设备/iOS 仍需验证。未协商到音频会在约 20 秒后报错，不会自动切回视频镜像。
音频协商完成后允许暂停播放，不因音频静默或没有视频触发视频重连；拔线仍通过 USB 错误和设备存在检查识别。

诊断文件 `usb-audio-only.log` 位于实验版 exe 同目录；其中 `usb_audio_only` 记录收到的音频包、视频包和视频包 payload 字节数。
没有录音或截图功能，不会把音频内容保存到磁盘。日志只记录诊断信息。

可选自动测试（需要手机持续播放声音）：

```powershell
.\UsbAudioOnly\iPhoneMirror.UsbAudioOnly.exe --verify 30
```

结果写到 `UsbAudioOnly/verification-result.json`。自动测试检查持续接收音频、零视频和正常停止；是否听到声音仍需人工确认。
`--list` 只枚举设备；多台手机测试可使用 `--device <UDID> --verify 30`。

## 实现与构建

- `SessionOptions.request_video=false` 禁止显示启动、取帧和显示重配置，独立推进音频状态。
- C API 的 `CaptureOptions.reserved[2]=3` 选择实验性 USB 纯音频模式；结构大小和默认值保持兼容。
- 纯音频不创建视频解码器，不应用首视频帧超时、视频帧率恢复和媒体静默超时。
- 停止时只等待已请求/已观察到的流释放，保留原有 USB 配置恢复流程。
- 独立命令行播放器复用原有设备枚举、USB 后端、WASAPI、音量和停止接口。
- 原始安装目录中的 exe、DLL、驱动及设置不被覆盖。

标准 MSVC/CMake 环境中构建 `iPhoneMirror.UsbAudioOnly`、`iPhoneMirror.Core.Tests` 和
`iPhoneMirror.UsbConfigurationSwitch` 目标。此工作区也提供 `audio-only-work/Build-AudioOnly.ps1`，
使用同目录的便携 MSVC/Windows SDK/CMake/Ninja。

运行协议测试时工作目录需是构建后的 `src/Core`，以便读取既有 fixtures。
协议测试不启动手机采集，包括正常音视频路径回归、模式配置、仅音频握手、意外视频计数、禁止取帧和释放流程。
