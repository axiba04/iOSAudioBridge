# 当前构建 USB 仅音频稳定性测试

> 历史记录：WPF 前端及其发布脚本现已移除；下文保留当时的设计或验收依据。当前入口见 [README](../README.md)。

日期：2026-09-24，时区 Asia/Shanghai。

## 结论

当前发布版在本轮约 15 分钟持续静音测试中没有 USB 断连、会话退出或重连，
结束后正常恢复 USB，手机重新可访问。发生 **1 次 WASAPI 缓冲欠载**，丢帧为 0。
因此“本轮连接保持”通过，“零欠载连续输出”未通过；不能把进程返回 0 当成全部质量指标通过。

## 上游 issue 的关系

查阅 [iPhoneMirror issue #24](https://github.com/RayrenSX/iPhoneMirror/issues/24)：
报告环境为 v1.8.3、Windows 11、iPhone 13 Pro，描述的是反向控制运行一段时间后失效，
需要重新投屏并开启有线控制。报告没有提供诊断日志，不能由此认定音频 USB 通道也断开。

当前音频桥接没有移植反向控制模块。两者仍可能共享底层设备或驱动环境，
因此也不能仅凭功能不同就保证不会断连；本轮实测检验当前程序自身的音频通道。

## 条件与版本

- 用户确认准备好，并按测试要求维持 iPhone 亮屏、解锁、持续播放。程序未独立读取屏幕/锁定状态；
  本轮未单独记录具体播放 App，不能将其标为 PUBG 实测。
- iPhone 15 Pro Max，设备报告 iPhone16,2 / iOS 26.6。
- Windows 上使用 Realtek 扬声器端点，输出增益为 0，全程静音。pureaural DAC 未参与本轮。
- 固定 48 kHz / PCM16 / Stereo；WASAPI 共享事件驱动，周期 480 帧，端点缓冲 1056 帧。
- 直接运行 `dist/iOSAudioBridge/AudioProbe.exe --capture-muted 900 0 2`，
  加载发布目录同一个 AudioCore.dll；没有重编译或替换当前发布二进制。
- 六个主要二进制 SHA-256 与前次暂停恢复修复发布记录完全一致，见本轮 manifest。
- 只验证 Native 音频链路，未覆盖 WPF 长期交互、有声听感或立体声定位。

## 结果

| 指标 | 实测 |
|---|---:|
| 秒级状态记录 | 900 条 |
| USB 激活 / 打开次数 | 1 / 1 |
| 运行中断连 / 重新连接 / 会话错误 | 0 / 0 / 0 |
| 首次有音频后相邻秒级采样音频包不增长 | 0 次 |
| 首次有音频后输出 inactive / source idle 采样 | 0 / 0 |
| 最终音频包 | 42,593 |
| 视频包 / 视频字节 | 0 / 0 |
| 提交的有效 PCM 帧 | 43,613,696（约 908.62 秒） |
| 缓冲欠载 | **1 次** |
| 丢弃 PCM 帧 | 0 |
| 运行时队列采样范围 | 160–1920 帧 |
| 60–900 秒进程私有内存采样范围 | 6.883–7.113 MiB |
| 句柄情况 | 预热后 320，后续约 315，清理阶段降至 293 |
| 总 CPU 时间 / 单逻辑核等效平均占用 | 约 5.44 秒 / 0.59% |
| 最终输出峰值 | 左右均 -120 dBFS（静音计量下限） |
| 主动停止耗时 | 12.397 秒 |
| USB 正常配置确认 / 手机可访问 | 是 / 是 |
| 测试进程退出码 | 0 |

09:46:59.113 打开 USB 接口；完成 900 次秒级采样后主动停止，
10:02:20.331 确认正常 USB 配置。测试脚本总耗时约 927.86 秒，包含启动、
采样调度开销、USB 恢复和脚本轮询；采样 `t` 是循环序号，并非精确墙钟秒数。

## 欠载解释与后续诊断

`t=761` 时欠载为 0，`t=762` 时变为 1，此后直到结束保持 1。
当时状态仍为 Streaming、output_active=1、source_idle=0，音频包持续增加；
没有手机 STOP、暂停恢复、USB 错误或重新打开接口记录。

当前计数定义是在一次 WASAPI 填充中，可用 PCM 少于需要提交的帧数，且不属于已知暂停/恢复准备/空闲。
缺少的部分由程序补零。这次事件表明至少一次填充发生了真实短缺，**不等于 USB 断连**。
由于 AudioProbe 本轮没有逐秒输出 `missing_frames` 或精确事件时间，
不能从“1 次”反推缺音持续时间，也无法仅凭现有日志区分 USB 包抖动、线程调度或时钟漂移。
静音测试没有验证人耳是否能听出该事件。

后续应先增加缺失帧数、包到达间隔和水位趋势诊断，再进行更长时间的 PUBG / 目标 DAC 测试。
根据证据决定是否调整水位或加入时钟漂移补偿，避免直接增加延迟来掩盖问题。
本轮没有自动修改缓冲设置、驱动或音频实现，也没有把 15 分钟外推为数小时稳定性保证。

## 证据和复测

原始证据：[test-results/2026-09-24-094655-stability/](test-results/2026-09-24-094655-stability/)。
包含 `capture.txt`、`capture.log`、`stderr.txt`、`resources.csv`、`manifest.json`、
`completion.json`、`summary.json` 和测试前后设备枚举。

后续在手机准备好、其他程序停止占用 USB 后，可运行：

```powershell
# 先确认设备索引；输出索引可能随硬件变化。
./scripts/Build-ZLBridge.ps1
./build/zl-bridge/bin/AudioProbe.exe --devices
./build/zl-bridge/bin/AudioProbe.exe --outputs
./scripts/Test-Stability.ps1 -Seconds 900 -DeviceIndex 0 -OutputIndex 2
./scripts/Summarize-Stability.ps1 -ResultDirectory '<本轮生成的结果目录>'
```

当前脚本使用 `build/zl-bridge/bin` 中的原生诊断工具，只验证直通音频底层，不覆盖 ZL DSP 或界面；上面的历史结果仍对应旧构建。
脚本固定静音，不录制 PCM。测试工具按时长主动 stop 并等待 USB 恢复；
完成后检查日志、状态样本和各计数，不只看退出码。
