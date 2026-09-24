# libusb-win32 驱动升级记录

日期：2026-09-24。系统为 Windows 11 Enterprise LTSC，build 26100。
本次保持 Apple USB 方案，原有设备级 libusb filter 从 1.2.6.0 切换到 1.4.0.2。
Windows 已加载新内核驱动；隔离程序的三次静音采集及重新构建后的日常包复测均通过。
这是本机兼容性短测，尚未完成新版驱动的长时间游戏、重启后运行和实际降级演练。

## 版本与变更范围

| 组件 | 原版本 | 当前版本 / 状态 |
|---|---|---|
| Apple USB Composite 驱动 | 538.0.0.0 | 保持原版本 |
| Apple Mobile Device Service | 560.60.2.13 | 保持原版本 |
| libusb0.sys | 1.2.6.0 | 1.4.0.2，系统服务 Running |
| System32 / SysWOW64 libusb0.dll | 1.2.6.0 | 1.4.0.2 |
| 仓库与日常程序 x64 libusb0.dll | 1.2.6.0 | 1.4.0.2 |
| Apple 父设备绑定 | usbccgp；Upper=libusb0；Lower=AppleLowerFilter | 保持原样 |

仅配置了这台 iPhone 的设备级 libusb filter，没有全局类过滤器或其他使用 libusb0 的设备。
本次没有重绑 Apple 设备、安装发行包内的示例 INF、安装测试证书或关闭 Windows 签名检查。
注册表显示 Secure Boot=1、内存完整性配置=1；这两项配置记录不能替代运行时保护状态验证。
服务显示名称可能仍含旧版本文字，以实际文件版本和哈希为准。

原先约一小时游戏正常、疑似 USB-C 晃动后中断，是用户对旧驱动的使用反馈；不作为新版驱动的长测结果。

## 发行包与签名

- [官方 1.4.0.2 发布页](https://github.com/mcuee/libusb-win32/releases/tag/release_1.4.0.2)。GitHub API 的发布时间是 2026-01-09。
- 二进制包 `libusb-win32-bin-1.4.0.2.zip` 的 SHA-256：
  `00004c92cdb99be36e17fb2377165eb97e63b48ba895bfc04a642ea9c3e26d94`，与官方资产摘要一致。
- 对应源码提交 `0987f983a89e5e60d3c2db2af46564239c1b0d37`；源码归档、许可证和文件摘要保存在 `third_party/libusb-win32/`，详见 `pins.json`。
- 原 DLL 的 47 个导出在新版中全部保留。此项仅验证接口名兼容，真机测试另行执行。
- 驱动文件的嵌入签名单独进行 `/kp` 校验未通过；随包 Microsoft Windows Hardware Compatibility Publisher 签名 catalog 的内核策略校验通过。
- 仅将该 catalog 注册到驱动 catalog 数据库后，`signtool verify /kp /a` 通过；随后断线替换并启动内核服务成功。

发行包内 `libusb0.inf` 是匹配其他硬件的函数驱动示例，不能作为本机 Apple filter 的安装 INF。
试切换使用 catalog 登记和原服务路径下的文件替换，不运行整包安装器。
catalog 登记是 Windows 支持的签名目录登记操作，见 [Microsoft CryptCATAdminAddCatalog 文档](https://learn.microsoft.com/en-us/windows/win32/api/mscat/nf-mscat-cryptcatadminaddcatalog)。

## 已完成的真机对照

各轮通过相同的 ZL 程序诊断入口运行，输出全程静音，使用当前选择的 WASAPI 输出。
`--capture-muted` 秒数包含连接阶段；音频包计数不是固定时长性能基准。

| 组合 / 轮次 | 参数时长 | 音频包 | 欠载 / 缺失帧 / 丢帧 | USB 停止恢复 |
|---|---:|---:|---|---|
| 原驱动 + 原 DLL | 30 秒 | 1,272 | 0 / 0 / 0 | 通过 |
| 新驱动 + 新 DLL，第 1 轮 | 30 秒 | 1,230 | 0 / 0 / 0 | 通过 |
| 新驱动 + 新 DLL，第 2 轮 | 30 秒 | 1,276 | 0 / 0 / 0 | 通过 |
| 新驱动 + 新 DLL，第 3 轮 | 120 秒 | 5,398 | 0 / 0 / 0 | 通过 |
| 重新构建的日常发布包 | 30 秒 | 1,266 | 0 / 0 / 0 | 通过 |

四轮新版采集均无视频包、非有限样本，最终状态为“已停止，USB 已释放”。
第 3 轮日志记录手机端 `peer_stop`，暂停约 2,130 ms 后恢复，期间保留 USB；
因此该轮最大包间隔约 2.13 秒，不能当作连续播放时的 USB 抖动。
程序已加载 DLL 的路径和版本通过进程模块检查确认。
原始报告和日志在本机 `build/driver-evaluation/` 下，含设备信息，不纳入 Git。
不含设备标识的结果摘要保存在 [测试记录](test-results/2026-09-24-driver-upgrade/summary.json)。

重新编译了 USB 底层与恢复 helper，并使用新版 MSVC 导入库重新链接程序。
AudioCore.Tests、PcmProcessing.Tests、ZLBridge.Dsp 三组回归全部通过。
脚本语法、依赖哈希验证通过；篡改备份的模拟输入在访问系统设备和写入文件之前被拒绝。
最终检查确认设备 ProblemCode=0、Apple 服务正常、系统与日常程序的 libusb 文件版本一致。

## 可重复的操作

便携安装包现已提供 `04-UpgradeLibUsb.cmd` 和 `05-RestoreLibUsb.cmd`，使用系统自带 PowerShell 5.1，
备份位于 ProgramData，无需开发工具。普通使用者参见 [安装包升级说明](../DriverInstallKit/README.zh-CN.md)。
下方保留本次开发评估使用的仓库脚本及其原备份格式；两种备份格式不混用。

以下脚本使用 PowerShell 7。备份和检查为只读；Apply/Restore 需要 Windows 管理员权限。
脚本针对已有 Apple + libusb 设备 filter 的单手机环境，不是通用驱动安装器。

```powershell
# 手机连接时，保存当前设备信息、服务配置、驱动文件及 Apple 驱动包。
# LegacyPayloadDirectory 必须是与已安装旧内核驱动哈希一致的恢复材料。
./scripts/Save-UsbDriverBaseline.ps1 -LegacyPayloadDirectory '<原 libusb 安装素材目录>'

# 下载并校验固定版本；需要 Microsoft 签名的 Windows SDK SignTool。
# 同时创建隔离的测试程序副本。-Offline 可使用已有下载。
./scripts/Prepare-LibUsbDriverTrial.ps1 -SignToolPath '<Windows SDK 的 signtool.exe>'

# 关闭采集程序，拔下手机，保持断开直到命令完成。
$baseline = '<刚生成的 baseline 目录，必须保留完整内容>'
./scripts/Invoke-LibUsbDriverTrial.ps1 -BaselineDirectory $baseline -Mode Check -DeviceDisconnected

# 在管理员 PowerShell 中执行；如 SignTool 不在默认工具路径，也传入 -SignToolPath。
./scripts/Invoke-LibUsbDriverTrial.ps1 -BaselineDirectory $baseline -Mode Apply -DeviceDisconnected
```

Apply 只替换系统 `libusb0.sys`、System32 和 SysWOW64 中的两份 `libusb0.dll`，
并登记独立命名的签名 catalog；核验备份与候选文件哈希、Apple 绑定、filter 范围和运行进程。
每次写入前确认驱动已停止、手机仍断开；加载失败则尝试恢复三个原文件。
成功表示断线切换及驱动加载成功，之后仍需插回手机、解锁播放，完成采集与 USB 恢复验证。
每次操作的日志和 `result.json` 写入 `build/driver-evaluation/libusb-win32-1.4.0.2/operation-*`。

本机在线停用曾返回 3010（要求重启），重新启用返回 50。脚本当时在写驱动文件前退出，旧文件未变。
拔线后旧驱动卸载，断线切换成功；插回后设备保留 Code 22，通过启用原设备恢复，未重启系统。
若遇到相同残留，管理员可对基线 `devices.json` 中那一个 Apple 父设备执行
`pnputil /enable-device '<完整设备实例 ID>'`，再确认 ProblemCode=0。
不要对整类 USB 设备批量启停。若 Windows 仍要求重启，应停止试验，安排重启后再验证。

## 回退

本机原始基线目录：`build/driver-evaluation/baseline-20260924-145422/`，共 34 个备份文件，
包含核对过的 1.2.6.0 安装素材、原系统 DLL/SYS、日常程序 DLL、相关服务配置和 Apple DriverStore 包。
基线仅存本机；清理 `build/` 会一起删除，完成长期验收前应保留。
这不是整个 Apple 软件套件的重装镜像，也未在本次实际执行降级演练。

```powershell
# 先关闭采集、拔线；在管理员 PowerShell 中运行。
$baseline = 'build/driver-evaluation/baseline-20260924-145422'
./scripts/Invoke-LibUsbDriverTrial.ps1 -BaselineDirectory $baseline -Mode Restore -DeviceDisconnected

# Restore 只还原系统三份文件。日常应用 DLL 另从同一基线恢复：
Copy-Item -LiteralPath "$baseline/workspace/dist/ZLUsbAudioBridge/libusb0.dll" `
    -Destination 'dist/ZLUsbAudioBridge/libusb0.dll' -Force
```

Restore 校验备份、卸载新驱动、恢复原文件并启动旧驱动，随后移除本次登记的 catalog。
插回手机后还需重新验证采集；查看 `result.json` 确认每一步是否成功，不能只依据 DLL 文件版本判断恢复完成。
若决定长期回到旧版，还需恢复 Git 中升级前的 libusb 依赖和打包配置，否则下次构建仍会打包 1.4.0.2。
