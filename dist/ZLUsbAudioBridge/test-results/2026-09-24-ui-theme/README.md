# 新增界面主题与空状态验收

2026-09-24，Windows x64 Release。对应二进制哈希见 `build-hashes.json`。

- `ctest.log`：AudioCore.Tests、PcmProcessing.Tests、ZLBridge.Dsp 三组通过。
- `current.json`：实际枚举到 0 个手机、3 个音频输出；“开始监听”禁用，手机提示为“未检测到 iPhone / iPad”。
- `empty.json`：通过 `--ui-smoke --ui-empty` 清空本次测试的设备列表，模拟 0 个手机、0 个音频输出；两个空状态提示正确，“开始监听”禁用。
- `current.png` / `empty.png`：1100×800 主界面；上下背景与 ZL 默认主题一致，空状态文字、下拉箭头及禁用状态可辨认。
- `*-advanced.png`：展开监听音量和三行诊断信息，底部状态栏继续显示连接提示及静音/音量。
- `*-minimum.png`：1000×780 最小窗口展开高级设置，新增控件及原版压缩器参数无裁切。

截图经过人工查看。UI smoke 使用合成信号驱动原版分析器，不启动 USB 采集或 WASAPI 输出；图中曲线不是手机实测音频。报告的 `start_enabled` 记录截图时的按钮状态。

改动范围是独立程序的界面、设备选择状态和诊断入口；本轮没有修改压缩器 DSP、PCM 处理或驱动代码。未进行真机试听，也未把默认主题截图视为所有自定义配色和显示缩放比例的完整验收。
