# 荣耀平板 Android USB 兼容修复

日期：2026-10-30。设备：荣耀 MagicPad3 Pro 13.3，型号 YLP-W00，Android 16 / API 36。

Windows 能识别 MTP 与 ADB Interface，后者使用微软 WinUSB、设备问题码为 0；但接口注册使用厂商 GUID，标准 ADB 后端 `LIBADBUSB` 返回空设备列表。切换同一份 ADB 37.0.1 的 USB 后端至 `LIBUSB` 后，设备立即进入 `unauthorized`；用户在平板允许 USB 调试后进入 `device` 状态。没有更换驱动或修改设备注册表。

修复将默认 `ADB_LIBUSB=1` 放入 ADB 子进程环境，保留用户显式覆盖值和继承的其他环境变量，不修改父进程或系统环境。新增进程回归验证默认后端、显式覆盖、Unicode 环境继承与父进程环境不变。七项 CTest 均通过，见 `ctest.log`。

使用本地修复版 `dist/AudioBridge/AudioBridge.exe` 验证从已停止的 ADB 服务冷启动，未预设 `ADB_LIBUSB`。程序自动启动 libusb 后端并完成 20 秒 USB 静音接收。`usb-capture.json` 记录：913 个音频包、934912 接收帧、932736 渲染帧、48 kHz、0 次欠载、0 丢失帧、高水位裁剪 1024 帧；未生成视频流。程序正常退出并清理会话。

此验证确认设备识别、授权后的 ADB 通路、scrcpy PCM 接收和 WASAPI 静音输出。没有录制音频内容；不代表听音质量、端到端延迟、Wi-Fi 或长时间稳定性已通过验收。测试二进制 SHA256 见 `summary.json`。
