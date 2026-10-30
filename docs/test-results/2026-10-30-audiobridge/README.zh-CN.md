# AudioBridge / scrcpy 集成验收

本文件记录首次 Android 集成时的结果。后续 AirPlay 默认缓冲已恢复为 4096 / 6144 帧，最新构建见 [缓冲恢复验收](../2026-10-30-airplay-buffer-restore/README.zh-CN.md)；下方 1024/2048 帧描述及本目录哈希为当时记录。

2026-10-30，Windows x64，Clang 22.1.0 / MSVC STL / CMake 3.31.8。程序使用 ZLCompressor 0.6.1 和官方 scrcpy-server 5.0.1。

- 七组 CTest 全部通过：AudioCore.Tests、PcmProcessing.Tests、Scrcpy.Tests、ZLBridge.Dsp、AirPlay.Runtime、AirPlay.Tests、AirPlay.ReceiverTests。
- 新增 Scrcpy.Tests 使用独立模拟 ADB 进程和本机 TCP，覆盖分片/连续 PCM、异常报文、授权失败、旧 Android 版本、USB/无线缓冲、进程取消、转发创建取消后的清理、断开输出、失败后重启及输入来源互斥。用固定双声道 PCM 验证经过 PCM 队列和压缩器后的输出值。
- iOS USB、AirPlay、Android USB、Android Wi-Fi 四种界面静音 smoke 通过；检查最小窗口布局、无设备时禁用开始按钮、Android 不启用 Apple USB 修复按钮。
- 发布目录的空设备/空输出界面检查通过；WASAPI 48 kHz 静音 smoke 完成三次暂停/恢复，无欠载和缺失帧。
- 驱动安装保护测试 12 项、升级/恢复模拟测试 25 项通过；没有安装或更改系统驱动。
- `git diff --check` 通过。用户已有的 AirPlay 1024/2048 帧实验缓冲修改保留。

测试原始结果见本目录 JSON、CTest 日志及二进制哈希；UI 截图留在本机 `build/audiobridge-validation/`。

**未验收：** 本机 `adb devices -l` 没有 Android 设备。没有声称完成 Android 真机 USB/Wi-Fi 出声、手机端暂停、拔线/断网恢复、无线配对交互、端到端延迟、具体游戏或长期稳定性测试。协议与进程测试使用模拟服务器，不能替代以上实机验收。现有 iOS 协议测试通过不等于本轮重新进行 iPhone/iPad 实机听音测试。
