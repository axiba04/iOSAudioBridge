# USB 手动修复离线验收

2026-09-29，Windows x64，Clang 22.1.0，Release。

- 构建通过；AudioCore.Tests、PcmProcessing.Tests、ZLBridge.Dsp 三组全部通过。
- 新增原生测试：A/B 精确选择、序列号格式归一、目标拔出、重复目标、错误厂商/产品/子接口/序列号、非法参数、同设备跨线程互斥与不同设备独立、连续 PnP/USBMux 就绪观察。
- 对实际 `AudioBridge.UsbRepair.exe` 执行无副作用保护检查：序列号不匹配返回 87，Apple 音频配件返回 87，不存在的合成手机返回 1168。均未进入设备重启。
- 检查助手 PE 导入：只有 SETUPAPI.dll、KERNEL32.dll，没有 libusb0.dll，恢复启动不依赖 libusb。
- `--ui-smoke --ui-empty` 返回 0；手机与输出数量均为 0，开始和修复按钮均禁用，静音开启。已检查 1000×780 最小窗口截图，新按钮无重叠。

构建命令：`./scripts/Build-ZLBridge.ps1 -NoPublish`。UI 检查产物、CTest 日志与发布文件哈希 `build-hashes.json` 在本目录；运行文件另通过 Release 分发。

本轮未连接真实手机，未请求 UAC、重启真实 USB 节点或进行有声采集。故障现场的恢复效果、UAC 取消/授权交互、两台设备 A→B→A 切换仍待实机验证。
