# iPad 停止监听后的 USB 恢复修复

日期：2026-09-27。设备：iPad13,4 / iPadOS 26.6.2，Apple USB 538.0.0.0，libusb-win32 1.4.0.2。

## 问题与证据

旧版可以接收音频，但停止后显示“USB 普通配置未确认恢复”，退出码为 1。
只读 USB 描述符检查确认：采集前后活动配置都是正常配置 3，停止后已不再使用 QuickTime 配置 5。
尽管配置切换辅助程序返回成功，Apple USBMux 的设备列表仍为空；延长协议收尾等待也无效。
因此不能仅凭辅助程序退出码、配置描述符或 Windows 父设备存在就报告恢复成功。

## 修改

1. 配置切换辅助程序退出后，才开始完整的 12 秒 Windows/Apple 枚举观察窗口，避免辅助程序自身的等待占用观察预算。
2. 切换成功但正常枚举仍未恢复时，增加一次 `reconnect` 辅助操作。它重新验证设备序列号、当前配置和描述符，确认该设备处于包含 Apple 管理接口且不包含 QuickTime/占位接口的正常配置，再调用 `usb_reset_ex(..., USB_RESET_TYPE_CYCLE_PORT)`。
3. 重新枚举后，仍要求同一台设备出现在 USBMux 中，且父设备、MI_00 和 MI_01 都正常启动，连续两次满足后才显示“USB 已释放”。

恢复动作最多一次，不重发 0x52，不卸载驱动、不重启 Apple 服务、不要求管理员权限。
辅助进程保留失效句柄直到精确设备完成移除/到达并稳定，避免在重枚举期间关闭旧句柄。
未知序列号、未配置状态、活动音频配置、缺少管理接口或含音频占位接口时拒绝端口重新枚举。
设备缺失、配置切换失败或驱动状态不明时不执行补救。

该操作使用已安装的 libusb filter 提供的端口重枚举能力；其实现见
[libusb-win32 reset_device.c](https://github.com/mcuee/libusb-win32/blob/0987f983a89e5e60d3c2db2af46564239c1b0d37/libusb/src/driver/reset_device.c)。

## 验证与构建

原生测试增加正常配置、不同 QuickTime 配置编号、序列号缺失、零配置、活动采集和占位接口等保护条件。
AudioCore.Tests、PcmProcessing.Tests 和 ZLBridge.Dsp 用于验证公共底层、PCM 适配和 DSP。
实机测试必须同时检查采集结果、停止状态，以及下一轮无需拔插即可重新连接。
全程静音，不能据此声称完成有声听感或长期稳定性验收。

本机便携工具链位于 `.tools`：LLVM 22.1.0、MSVC 14.44.35207、Windows SDK 10.0.26100.0、CMake 3.31.8、Ninja 1.12.1。
构建命令：

```powershell
./scripts/Build-ZLBridge.ps1 -ToolRoot "$PWD/.tools" -NoPublish
```

实机结果和发布文件哈希见 `docs/test-results/2026-09-27-ipad-usb-restore/`。
当前修复优先保证恢复可靠性；触发补救的停止操作可能需要约 25 秒。

| 实机轮次 | 音频包 | 欠载 / 缺失帧 / 丢帧 | 停止耗时 | 最终结果 |
|---|---:|---|---:|---|
| AudioProbe 后端验证 | 351 | 0 / 0 / 0 | 25.16 秒 | 已停止，USB 已释放，accessible=1 |
| 最终主程序第 1 轮 | 445 | 0 / 0 / 0 | 23.20 秒 | state=0，退出码 0，accessible=true |
| 最终主程序第 2 轮 | 449 | 0 / 0 / 0 | 23.19 秒 | state=0，退出码 0，accessible=true |

最终主程序两轮顺序运行，中间没有拔插数据线、重启设备节点或重启 Apple 服务。
两轮视频包均为 0，停止后 GetValue 均成功，恢复日志每轮只出现一次 reconnect 动作。
三个 CTest 测试组全部通过。
