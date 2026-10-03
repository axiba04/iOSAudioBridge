# ZL 原版界面 USB 集成：实现与验收

日期：2026-09-24。对应 `zl-standalone-usb-integration-plan.zh-CN.md`。
2026-10-03 更新：升级至 ZLCompressor 0.6.1 / JUCE 9.0.2，桥接补丁保留；构建、三组回归、无设备界面检查与静音 WASAPI 暂停/恢复通过，详见下方本轮验收。
2026-09-29 更新：新增“修复 USB”入口与分阶段连接诊断；三组原生/DSP 回归、助手目标拒绝检查和无设备 UI smoke 通过。真实故障恢复、UAC 交互与 A→B→A 换机尚待实机验证，见项目文档 `docs/test-results/2026-09-29-usb-repair/README.zh-CN.md`。
2026-09-27 更新：iPad 停止后的自动 USB 恢复已修复，连续两轮主程序采集、停止及重新连接通过。触发补救时停止约 23 秒，详见项目文档 `docs/usb-restore-ipad-fix.zh-CN.md`；下方历史验收数据保持原样。
当前交付为可运行的集成预览版，核心功能覆盖 A–D；尚未满足 A–D 全部交互、异常与实时性验收条目，E 长时间验收未执行。

后续界面更新：音量滑块已移入默认收起的“高级设置”，命名为“监听音量”；默认 50%，主界面保留静音。2026-10-03 升级完成后，常规启动改为默认非静音，自动验收仍强制静音。该改动后的最终构建三组回归全部通过，证据见 [构建日志](test-results/2026-10-03-zl-0.6.1/default-unmuted-build.log)、[CTest 日志](test-results/2026-10-03-zl-0.6.1/default-unmuted-ctest.log)、[DSP 结果](test-results/2026-10-03-zl-0.6.1/default-unmuted-dsp.json)与[二进制哈希](test-results/2026-10-03-zl-0.6.1/default-unmuted-build-hashes.json)。下方 UI/WASAPI 测试数据与截图仍对应调整前的构建；当前包哈希也见发布目录 `build-hashes.json`。

## 实际链路与改动

```text
iPhone → 原仅音频 USB 协议 → UsbAudioSource → PcmPipe / SPSC
       → WASAPI 渲染线程：PCM16 转 float → 单个 ZL Processor → float 转 PCM16
       → 最终音量/静音 → WASAPI Shared Event → 输出设备
                              ↑
                  同一个 Processor 的原版 ZL Editor
```

- `Capture/UsbAudioSource.*` 保留已验证的协议、暂停等待、手机独占会话锁、安全 USB 恢复流程。
- `Buffer/PcmPipe.h` 将队列和源统计从 WASAPI 分离；`Engine/AudioEngine.*` 组织生命周期。
- `Processing/IBlockProcessor.h`、`PcmBlockRenderer.h` 提供预分配处理、有限值检查、剪裁计数和最终增益渐变；静音仍执行 DSP。
- `Session/AudioSession.h` 保留原名称门面；C ABI 结构与版本未改变，默认处理器为直通，当时保留了 WPF 构建；WPF 前端现已移除，C ABI 继续供 AudioProbe 和回归测试使用。
- `src/ZLUsbAudioBridge/` 为自定义 JUCE 程序、ZL float 适配和测试。没有 JUCE 默认 StandalonePluginHolder、AudioDeviceManager、AudioProcessorPlayer 或 VST Host 的第二条音频回调。
- 原版 Editor、参数附件、分析器与预设浏览器保留。Lookahead/Oversampling/外部侧链受约束；整份预设走停止→恢复 USB→应用状态→按需重连，六项旋钮实时更新。
- 两通道 L/R、禁用 Aux，总线布局在 prepare 阶段固定。块上限 4096；WASAPI 较大请求由适配层分块；正常渲染路径不调用 UI/文件/设备枚举。
- 所有新增内容独立构建到 `dist/ZLUsbAudioBridge`。原 `dist/iOSAudioBridge` 的 6 个关键二进制与 15 分钟测试清单逐项 SHA-256 一致。

源码依赖固定、上游修改导出、许可证打包已落实；重复运行准备脚本已通过。
新增应用按 AGPL-3.0 提供，来源和各组件许可见根目录 `THIRD_PARTY_NOTICES.md`。

## 已完成测试

### 2026-10-03：ZLCompressor 0.6.1 升级

本轮原始证据：[汇总](test-results/2026-10-03-zl-0.6.1/summary.json)、[构建日志](test-results/2026-10-03-zl-0.6.1/build.log)、[CTest 日志](test-results/2026-10-03-zl-0.6.1/ctest.log)、[二进制哈希](test-results/2026-10-03-zl-0.6.1/build-hashes.json)。

- ZLCompressor 固定到 `6d8c79cff62b42b1d5e4cf60c8eda2ca90b7e938`，JUCE 固定到 `e66b82515320d92a2224651f8ecc1c888a8ec3d8`。Highway 和 zldsp_fft 保持 0.6.1 的原固定提交；未引入该版本之后的电平表布局改动。
- JUCE 4,840 个普通文件与固定提交逐项核对通过（允许 Git 检出造成的 CRLF/LF 差异）；桥接补丁重新基于 0.6.1 导出，反向检查通过。
- 从依赖清单读取 ZL 版本/提交号，补全 Windows 资源生成对版本信息的依赖。最终 EXE 的 FileVersion、ProductVersion 和 ZL 运行时版本均为 `0.6.1`，运行时 JUCE 为 `9.0.2`。
- Release/x64/SSE2 构建完成，AudioCore.Tests、PcmProcessing.Tests、ZLBridge.Dsp 三组通过。[DSP 结果](test-results/2026-10-03-zl-0.6.1/dsp.json)确认压缩输出 −15.00515 dB、600 次参数切换输出有限、处理期间 C++ new 计数 0、预设低延迟约束与延后加载通过、DSP 延迟 0 帧。
- 新增 FFT 历史缓冲回归：48 kHz / 750 Hz 合成信号经过 Normal→High→Normal 对应的 4096→16384→4096 历史缓冲切换；覆盖 FIFO 回绕读取、清空旧历史、High 模式三种 FFT 长度的频率/幅度一致性。这不是完整 GUI 设置交互测试。
- [实际无手机 UI](test-results/2026-10-03-zl-0.6.1/ui.json)和[模拟无任何设备 UI](test-results/2026-10-03-zl-0.6.1/ui-empty.json)均通过，含合成信号分析器绘制、展开高级设置和 1000×780 最小窗口；无手机时“开始监听”“修复 USB”禁用。[默认窗口](test-results/2026-10-03-zl-0.6.1/ui.png)与[最小窗口](test-results/2026-10-03-zl-0.6.1/ui-empty-minimum.png)已检查。
- [静音 WASAPI 测试](test-results/2026-10-03-zl-0.6.1/wasapi.json)：3 次暂停/恢复，处理 410,528 帧，欠载 0、缺失帧 0，period 480 帧，最大处理段 112 μs。

本轮未连接手机采集，未进行有声听感或长时间稳定性测试；不能用合成测试替代这些验收。下方保留 2026-09-24 的历史数据。

### 2026-09-24：初次集成

原始结果目录：`docs/test-results/2026-09-24-zl-integration/`。

| 检查 | 结果与边界 |
|---|---|
| Release 构建 | clang-cl 22.1.0 + MSVC STL 14.51 + SDK 10.0.28000，SSE2、静态 CRT；Standalone 成功 |
| CTest | AudioCore.Tests、PcmProcessing.Tests、ZLBridge.Dsp 三组通过 |
| Native/PCM | 原协议/ABI/策略回归；PCM 极值、通道、块边界、8191 帧分块、NaN/Inf、失败清零、溢出与统计 |
| ZL 旁路 | 1/127/480/1024/4096 帧逐样本检查，通道顺序、边界哨兵通过；拒绝 4097，接受零帧 |
| 稳态压缩 | −6.02 dB 输入、Threshold −18、Ratio 4；实测 −15.00515 dB，等于预期；左右 2:1 比例保持 |
| Attack | 同样阶跃 10 ms 后：1 ms attack 的增益 −8.985 dB，100 ms attack 为 −4.191 dB；速度方向正确 |
| Release | 同样高→低阶跃 100 ms 后：20 ms release 恢复至 0 dB，500 ms 为 −2.557 dB |
| Knee / Makeup | 阈值处 Knee 0 近 0 dB 衰减，Knee 12 为 −2.25 dB；Makeup 6 dB 实测增加 6.0000005 dB |
| 参数实时路径 | 六项参数共切换 600 次，输出有限；这些处理调用中 C++ new 计数 0 |
| 导入约束 | 带 lookahead/oversample/external side 的完整状态归一化为 0；加载回调先交给协调层 |
| WASAPI + ZL | 默认 Realtek 端点静音合成信号，3 次暂停/恢复；410,528 帧、欠载 0、缺失 0，最大处理段 59 μs |
| 原旁路 WASAPI | 拆分后 AudioProbe 3 次静音暂停/恢复，欠载 0，period 480、endpoint 1056、autoconvert 0 |
| 界面 | 原 Editor 创建、合成信号离屏绘制、手机/输出列表；小桌面按原界面最小纵横比缩放，避免裁切底部参数 |

Attack/Release 检查验证控制方向和阶跃差异，未声称精确标定所有模式的时间常数。
分配检查只覆盖 C++ new（包括 aligned new），未拦截直接 malloc，也未覆盖全部分析器、所有原版模式和用户手势。
`max_render_us` 是读取 PCM、转换、DSP、最终增益的处理段，不包括 WASAPI GetBuffer/ReleaseBuffer、排队等待和全链路延迟。

## 手机短测

当天后续的新增界面调整另见 [UI 主题与空状态验收](test-results/2026-09-24-ui-theme/README.md)：最终构建三组回归通过，覆盖实际无手机、模拟无任何设备、展开高级设置及 1000×780 最小窗口截图。该轮没有重新采集手机音频。

用户已授权静音；手机亮屏、解锁并播放音频。iPhone 15 Pro Max / iOS 26 系列，输出为本机 Realtek，未使用 pureaural。
启动时间 10:46:48，进程于 10:47:58 退出。`--capture-muted 60` 的计时包含连接；实际处理约 56.85 秒源音频。窗口隐藏，Editor 实例存在。

| 指标 | 结果 |
|---|---:|
| 音频包 | 2,667 |
| 视频包 / 字节 | 0 / 0 |
| 收到 PCM 帧 | 2,731,008 |
| 已处理源 PCM 帧 | 2,728,896 |
| 停止时队列剩余 | 2,112 帧（主动停止后不再渲染，不计作运行中丢帧） |
| 欠载 / 缺失帧 / 丢帧 | 0 / 0 / 0 |
| 非有限值 / 最终剪裁 | 0 / 0（测试静音，不代表有声时不会剪裁） |
| 最大处理段 | 105 μs |
| 最大源包间隔 | 22,935 μs |
| ZL 报告的 DSP 延迟 | 0 帧 |
| 左 / 右设备输出 | −120 / −120 dB（静音） |
| 停止总耗时 | 8,018 ms：输出 9、收尾 253、关闭 0、恢复 7,756 |
| 最终状态 | 已停止、USB 已释放，helper 成功，普通 USB 配置连续确认 |

`capture.json`、每秒 `capture.jsonl` 和 `usb-capture.log` 保留完整证据。
`capture-build-hashes.json` 对应真机测试构建；后续仅补充窗口缩放/测试刷新、游戏预设显式开启压缩及自测/打包规则，最终包哈希另存于 `final-build-hashes.json`。未将短测冒充最终包长测。

## 尚待验证与后续阶段

- 最终包可见窗口下反复拖动、缩放/最小化、分析器切换、原版预设浏览器交互和重启状态的完整人工验收。
- 连接过程中关闭、异常预设、输出端点移除、拔线、重复启停等新应用生命周期故障注入。
- 真机 B 站暂停≥10秒后恢复：本轮只有合成暂停覆盖，既有手机暂停证据属于旧后端基线。
- 15 分钟新旧对照、30 分钟 PUBG、至少 2 小时运行、pureaural/索尼 H6 Air 实际设备与有声验收。
- 当前最大包间隔和队列极值为会话累计，欠载时间使用系统单调时钟；尚未做滑动窗口分位数、内存/句柄长期采样或异步采样率漂移补偿。
- 独占模式、最低稳定缓冲、自动重连/连接、开机启动尚未实施。

旧 15 分钟测试出现过 1 次欠载，仍保留在原稳定性记录中；本次短测的 0 次不能证明该问题已经消失，也不能证明两小时游戏稳定。
