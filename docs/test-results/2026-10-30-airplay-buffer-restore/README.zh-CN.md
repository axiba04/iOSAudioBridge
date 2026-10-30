# AirPlay 默认缓冲恢复验收

2026-10-30，按用户要求将默认起播预填充从 1024 恢复为 4096 帧，高水位从 2048 恢复为 6144 帧。调用方显式提供的缓冲配置仍然有效；iOS USB、Android USB / Wi-Fi 配置沿用已有值。

通过 `scripts/Build-AudioBridge.ps1` 重新编译并更新 `dist/AudioBridge/`。七组 CTest 全部通过：AudioCore.Tests、PcmProcessing.Tests、Scrcpy.Tests、ZLBridge.Dsp、AirPlay.Runtime、AirPlay.Tests、AirPlay.ReceiverTests。

运行日志确认 AirPlay 接收类采用 `startup_frames=4096 high_water_frames=6144`。发布 EXE 的 SHA-256 与测试构建一致，完整压缩包内的运行文件逐项匹配发布哈希清单。更新包为 `dist/AudioBridge-v0.3.0-preview-win-x64.zip`，校验值在同名 `.sha256` 文件中。

本目录包含构建日志、CTest 日志、运行日志、DSP 结果、发布哈希和摘要。本轮没有进行 iPhone/iPad 实机听音或网络抖动测试，不将自动回归结果作为断续现象已消失的证明。
