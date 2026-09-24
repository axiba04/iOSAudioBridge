# iOS Audio Bridge 使用说明

1. 使用之前 iPhoneMirror 测试成功的 Apple USB 支持和 libusb0 filter 环境，关闭同一手机上的旧采集程序。
2. 保留本文件所在发布目录的全部 DLL 和 `AudioBridge.UsbRestore.exe`，运行 `iOSAudioBridge.exe`。
3. 手机解锁并信任电脑，点击“刷新设备”，选择手机和耳机 / DAC，点击“开始监听”。
4. 手机播放游戏或音乐；初始音量 50%，可先勾选“静音”再开始。
5. 点击“停止”或关闭窗口，等待 USB 配置恢复。恢复可能需要十几至数十秒。

在手机 B 站等播放器里暂停时，程序会显示“手机音频已暂停，保持连接等待恢复”；
重新播放后继续接收音频。只有点击软件“停止”、关闭软件或实际发生连接/输出错误时，才结束监听。

当前默认使用快速停止策略，本机短时多轮测试约 10 秒完成恢复。
如需排查兼容性，可在 PowerShell 中用原保守时序启动：

```powershell
$env:AB_USB_STOP_MODE='conservative'
./iOSAudioBridge.exe
```

关闭程序后，执行 `Remove-Item Env:AB_USB_STOP_MODE` 再启动可恢复快速策略。

当前为 48 kHz / 16 bit / Stereo 音频直通，没有压缩器。此次真机验收只做了静音，目标 DAC 尚未测试。
延迟数字仅估算主机排队，不代表端到端实测延迟。不要在 USB 恢复期间强行结束进程。

设备列表未显示手机时，检查数据线、信任状态和原有驱动环境。输出设备拔出后需要停止并刷新。
日志位于 `%LOCALAPPDATA%/iOSAudioBridge/logs/audio-core.log`。
出现“普通配置未确认恢复”时，重新插拔手机数据线。

命令行诊断：`AudioProbe --devices`、`AudioProbe --outputs`；
`AudioProbe --capture-muted 35 0 9` 表示对第 0 台手机、第 9 个输出端点进行 35 秒静音测试。
索引从 0 开始，以当次枚举为准。测试时长包含连接初始化，恢复时间另外计算。

本目录是本地 MVP 包；源代码、构建说明、测试记录在项目工作区 `E:/Code/iOSAudioBridge/`。
第三方许可见 `THIRD_PARTY_NOTICES.md` 与 `licenses/`。
