# CoreDevice USB 音频输入

这是在原 USB / AirPlay 之外新增的输入路线：

```text
iPhone/iPad → Apple USB 服务 → CoreDevice 隧道（lwIP）
           → displayservice 音频 → AAC-ELD 解码 → PCM16 / 48 kHz / 双声道
           → 原有 PCM 队列 → ZLCompressor → 监听音量/静音 → 所选 WASAPI 输出
```

此路线不切换 USB 配置、不需要 libusb filter，也不请求视频。开始监听时由界面
启动独立的 `CoreDevice/AudioBridge.CoreDeviceHost.exe`；停止、换输入和退出时结束
接收进程。进程之间使用私有管道，不使用 rPlayHub 的固定 TCP 端口。

## 使用

1. 设备使用 iOS/iPadOS 27+，宿主机安装 Apple Mobile Device Support，USB 连接、解锁并信任宿主机。
2. 开启开发者模式。若设置中没有入口，双击 `CoreDevice/show-developer-mode.cmd`，随后在设备上开启、重启并确认。
3. `CoreDevice/iOS_DDI/Restore` 内需有 DDI 文件。可以预先复制整套文件；未准备时运行 `CoreDevice/download-ddi.cmd`。挂载时仍需联网获取 Apple 签名票据。
4. 界面输入选择 **CoreDevice USB**，选择手机和输出设备，点击“开始监听”。
5. 在设备上播放声音，压缩器和音量/静音照常使用。切换到其他输入前先停止。

静音只作用于 Windows 监听输出；本集成没有自动静音 iPhone/iPad 扬声器的功能。
界面只有在收到解码后的 PCM 时才显示“正在监听”。连接成功但没有音频会保持连接中，
超时或协商失败显示错误，详细日志位于 `%LOCALAPPDATA%/iOSAudioBridge-ZL/Logs/CoreDevice-<进程号>.log`。
断线后点击停止、开始可重新建立会话；暂不自动重连。

保留整个 `CoreDevice` 文件夹（含 DLL、准备工具、许可证和可选的 DDI），复制到其他
Windows x64 电脑时不需要 Python 或编译环境。

## 构建

先在 MSYS2 UCRT64 安装 GCC、pkgconf、libimobiledevice、FFmpeg、OpenSSL 和 zlib：

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-libimobiledevice mingw-w64-ucrt-x86_64-ffmpeg \
  mingw-w64-ucrt-x86_64-openssl mingw-w64-ucrt-x86_64-zlib
```

然后从仓库 PowerShell 执行（路径换为自己的工具和 DDI 目录）：

```powershell
./scripts/Build-CoreDeviceHost.ps1 -MsysRoot C:/msys64 -DdiRoot C:/path/to/iOS_DDI
./scripts/Build-ZLBridge.ps1
```

第一个脚本从已纳入仓库的 rPlayHub C 源码构建音频接收进程，并按需下载 lwIP 2.2.1。
`-DdiRoot` 可省略，之后运行下载脚本。产物位于 `build/coredevice-runtime`，主工程会将它
复制到程序旁的 `CoreDevice`。也可通过 CMake 的 `AB_COREDEVICE_RUNTIME` 指定已有运行库目录。
未准备运行库时，原 USB/AirPlay 仍可构建使用，选择 CoreDevice 会明确提示缺少接收程序。

## 验证边界

2026-10-20 在 Windows 虚拟机完成 Release 构建（MSVC 19.44、Windows SDK 10.0.26100；
CoreDevice 子进程使用 MSYS2 UCRT64 GCC 16）。CoreDevice 的运行库、PCM 管道和连接失败
三项测试通过，界面 smoke test 通过；AAC-ELD 真实样本 30 帧解码通过，DDI 新挂载和已挂载
两种服务刷新测试通过。整套 CTest 为 8/9 通过；原有 `AirPlay.ReceiverTests` 两次在 45 秒
超时，该环境的 7002 端口被 SafeNet 服务占用。尚未在此次集成中完成真机试听。

`CoreDevice.Tests` 验证 PCM 经过压缩器、通道顺序、启停、重新启动、子进程错误和半包取消。
准备好真实运行库时，额外验证解码器加载和不存在设备的错误回传。
`scripts/Test-CoreDeviceDecoder.ps1 -MsysRoot C:/msys64` 用 FFmpeg 的 AAC-ELD 测试样本验证
48 kHz 双声道 PCM16 解码；只在开发时下载 fixture，不需要安装 Python。

rPlayHub 原路线及 DDI 挂载后刷新服务的修复已经用户真机确认。接入 ZLCompressor 后的
完整 iOSAudioBridge 路线仍需同一设备复测，不能将原播放器试听当成新界面集成的实机验收。
