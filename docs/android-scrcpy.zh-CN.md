# AudioBridge：Android scrcpy 音频

2026-10-30，本地集成预览版。Windows x64；Android 11+。iOS USB、iOS AirPlay、Android USB 和 Android Wi-Fi 四种入口共用 ZLCompressor 与 WASAPI，每次监听一个输入，停止后切换。不是多设备混音器。旧 iOS USB 驱动和 Android ADB 是独立的接入方式。

## USB

1. 完整解压 AudioBridge，保留 `Android/adb.exe`、`AdbWinApi.dll`、`AdbWinUsbApi.dll`、`scrcpy-server`、许可证和版本清单。
2. 在 Android 开启开发者选项、USB 调试，用数据线连接电脑，在手机授权该电脑。Windows 如不识别设备，按手机厂商说明安装 ADB 驱动。
3. 打开 `AudioBridge.exe`，选择 **Android USB**，刷新并选择手机与耳机 / DAC，点击 **开始监听**。Android 11 启动时保持解锁。
4. 播放手机音频，实时调整压缩器、监听音量和静音。停止时释放本次流并清理本次端口转发。

USB 列表保留“未授权”和“离线”设备并显示原因，此时不能开始监听。授权后点击刷新。多个设备按各自序列号选择；不使用隐式默认设备。

## Wi-Fi

1. 电脑和手机连接同一局域网，手机开发者选项开启 **无线调试**。
2. 选择 **Android Wi-Fi → 无线连接**。首次使用，在手机点“使用配对码配对设备”，填写配对 IP:端口和六位码。
3. 同时填写无线调试主页面显示的 **连接 IP:端口**，点击 **配对并连接**。这两个页面的端口通常不同。
4. 已配对的手机只填连接地址，点击 **连接**。成功后自动刷新，选择手机和输出设备，开始监听。
5. 断网或关闭无线调试后会停止音频并显示错误。恢复网络后重新连接、刷新、开始监听；当前不自动重连。连接端口可能随手机重启改变。

只保存连接地址，不把配对码写入设置或应用日志。电脑已有 ADB 无线连接（包括手动 `adb tcpip` 建立的连接）也可刷新选用。程序不会自动将 USB 设备改为不加密的 TCP 调试模式。

## 音频链路与限制

```text
Android AudioRecord → scrcpy-server 5.0.1 raw PCM
  → ADB forward（USB 或网络）→ AudioBridge PCM 队列
  → ZLCompressor → 监听音量/静音 → WASAPI → 耳机/DAC
```

固定原始 PCM16LE / 48 kHz / 双声道，净音频带宽约 1.536 Mbit/s（不含传输开销）。不启动 scrcpy 桌面播放器，不启用视频、屏幕控制或虚拟声卡。`audio_source=output` 使用 Android 系统输出采集，通常会使手机扬声器静音；受保护内容或特定系统实现可能无法采集。

USB 默认启动/高水位缓冲为 1024/3072 帧，Wi-Fi 为 3072/6144 帧。界面软件延迟估计不含手机采集、ADB、Wi-Fi、驱动和 DAC 的全部延迟。原始 PCM 对无线网络抖动更敏感；当前无 Opus 压缩或自适应时钟漂移补偿。

每个会话使用独立随机 scrcpy socket 名、动态本机端口和设备临时服务器路径。停止只关闭本次客户端、端口转发和临时文件，不调用 `adb kill-server`、`forward --remove-all` 或断开其他设备。可以与其他设备的 scrcpy 会话共存；不保证同一手机被多个程序同时采集音频时的行为。

输入源更换前必须停止；iOS USB 的“修复 USB”按钮仅用于 Apple 设备，Android 不使用 Apple filter。

## 构建与依赖

```powershell
./scripts/Prepare-ZLDependencies.ps1
./scripts/Build-AudioBridge.ps1
# 已有官方压缩包时离线准备
./scripts/Prepare-Scrcpy.ps1 -Archive 'D:/Downloads/scrcpy-win64-v5.0.1.zip'
```

产物 `dist/AudioBridge/AudioBridge.exe`；旧 `Build-ZLBridge.ps1` 保持兼容。服务器的版本参数由 `third_party/scrcpy.pins.json` 传入构建，构建时核对服务器哈希；不要单独替换为其他版本的服务器，scrcpy 协议是版本相关的内部接口。

设备选择和处理器状态存于 `%APPDATA%/AudioBridge/`，第一次正常启动会复制旧 `%APPDATA%/iOSAudioBridge-ZL/` 中尚未存在的设置。新 ZL 界面/预设目录尚不存在时也复制旧产品目录。原设置保留。底层日志入口保持兼容：默认 `%TEMP%/iOSAudioBridge-capture.log`，或通过 `IPHONE_MIRROR_LOG_FILE` 指定。

## 验证范围

自动回归覆盖 scrcpy 协议的所有单次分割点、逐字节/连续帧、禁用音频、错误 codec、超大及未对齐包、ADB 授权/离线状态、USB/无线筛选、Windows 参数引用、超时取消、进程失败、会话清理、错误后重启、来源互斥，以及真实接收类经 PCM 队列和模拟压缩器处理后的双声道采样值。进程测试使用模拟 ADB/服务器，不连接手机。

2026-10-30 本机未枚举到 Android 设备。真实手机 USB/Wi-Fi 出声、两种连接方式切换、拔线/断网、Android 11 解锁行为、具体应用兼容性、延迟听感和持续运行仍需实机验收。`--ui-smoke --android-usb` / `--android-wifi` 可进行静音界面检查；`--capture-muted 60 --android-usb` / `--android-wifi` 仅在设备已连接并选好后用于静音接收诊断。

参考：[scrcpy 音频](https://github.com/Genymobile/scrcpy/blob/v5.0.1/doc/audio.md)、[scrcpy 协议](https://github.com/Genymobile/scrcpy/blob/v5.0.1/doc/develop.md)、[Android 无线 ADB](https://developer.android.com/tools/adb#wireless)、[固定发行版](https://github.com/Genymobile/scrcpy/releases/tag/v5.0.1)。
