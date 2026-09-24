# 驱动安装与升级（Windows x64）

用于 iOSAudioBridge / ZLUsbAudioBridge，适用于 Intel / AMD 的 64 位 Windows。
不适用于 ARM64。使用系统自带的 Windows PowerShell 5.1，无需另装 PowerShell 7、.NET 或完整 iTunes。
安装材料可离线使用；Windows 的证书信任检查可能需要联网。

## 安装顺序

先把整个文件夹解压到本地可写目录，保留所有子目录。不要直接在压缩软件中运行。

1. **先拔下所有 iPhone / iPad，关闭投屏和音频采集程序。双击 `01-AppleSupport.cmd`。**
   同意 Windows 管理员授权。缺少 Apple Mobile Device Service 时安装随包 MSI，
   然后将 `AppleUSB` 包加入 Windows 驱动库；已有 Apple 服务时跳过 MSI，不强行降级。
   返回 0 表示此步骤完成；如果显示 `3010` / `reboot_required: true`，先重启电脑。
2. **连接要使用的 iPhone / iPad，解锁并选择“信任这台电脑”，双击 `02-iPhoneFilter.cmd`。**
   只有一台时自动选择；多台时按编号选择本次要登记的设备，然后同意管理员授权。
   列表显示设备名称、登记状态和完整设备标识，同名设备按标识区分。
   脚本检查 Apple 复合设备正常后，安装或复用 libusb 1.4.0.2，
   只给选中设备添加 filter。不会替换 Apple 的父设备驱动。
3. **拔插一次刚登记的设备，再次解锁，然后双击 `03-Check.cmd`。**
   逐台显示 `READY`、`NOT_REGISTERED`（尚未登记）或 `NOT_READY`（Apple 设备状态异常）。
   全部已连接设备通过时显示总计 `READY`；有设备未就绪时返回非零退出码，已就绪设备仍单独列出。
4. 打开软件目录里的 `ZLUsbAudioBridge.exe`，选择手机和输出设备，先静音启动。
   音频计数正常后再取消静音；点击停止，确认提示“USB 已释放”。

安装包不含主程序。运行软件不需要常驻打开安装窗口。

## 同一电脑登记多台设备

Apple 支持和 libusb 驱动文件在电脑上共用一套，**每台 iPhone / iPad 单独登记 filter**。
第一台完成后，给第二台、第三台重复运行 `02-iPhoneFilter.cmd` 即可，无需重复运行第一步。
可以逐台插入，也可以全部连接后每次选择一台。登记过的设备拔线后仍会保留登记；重复登记同一台会跳过。
新增设备不会清除其他设备的 filter；失败时只尝试撤回本次给选中设备添加的 filter。
添加设备前仍需关闭所有采集程序。

需要指定设备的自动化操作可传完整实例标识（与菜单中的标识一致）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Install-Drivers.ps1 `
    -Stage Filter -InstanceId 'USB\VID_05AC&PID_12A8\<设备标识>'
```

`-Stage Check -InstanceId '...'` 可只查看某一台已连接设备；默认检查全部已连接设备。
选择后若目标拔线，脚本会停止，不会改选另一台。Apple 耳机转接头等配件不会出现在登记列表。
仍拒绝其他厂商设备、未知 Apple 产品、全局 libusb filter、libusb 函数驱动和不符合预期的 filter 叠加。

**多设备登记不代表同时采集多路音频。** 软件每个窗口仍只选择一台设备；
多设备登记与切换尚待两台真实设备验收，多窗口同时采集也未验证。

## 已装过驱动的电脑

可以先双击 `03-Check.cmd`。若显示 `READY`，直接使用软件，无需重装。
例如本次已升级到 1.4.0.2 的电脑就属于这种情况。

`02-iPhoneFilter.cmd` 用于首次安装，检测到旧版本时会拒绝覆盖。
已有 1.2.6.0 的电脑使用下方独立的升级入口。
不要再运行旧 `iPhoneMirror.Driver.exe` 的“一键安装”，它内置 1.2.6.0。

## 从 1.2.6.0 升级到 1.4.0.2

1. **关闭所有投屏/音频采集程序，双击 `04-UpgradeLibUsb.cmd`。**
   同意管理员授权，后续步骤在新打开的交互窗口中进行。
2. 脚本核对旧版文件和所有已登记设备的绑定（包含当前未连接的设备），
   自动备份系统三份 libusb 文件、服务配置和设备清单。
   看到“旧版备份已完成”和拔线提示后，**拔下所有 iPhone / iPad，再按回车继续**。
3. 保持全部断开，等待更换文件和内核驱动加载验证。窗口通知完成后，重新连接设备并解锁。
4. 运行 `03-Check.cmd`，看到 `READY` 后，用新版软件测试静音采集与停止恢复。

无需另外安装 PowerShell 7、SignTool、LLVM 或下载旧版安装素材。
脚本使用包内的 1.4.0.2 文件，从当前系统直接备份旧版文件，保留 Apple 驱动和原设备 filter 绑定。
Windows 自行执行内核签名策略检查；脚本不关闭保护、不自动重启，也不在线停用手机。

只支持本项目已验证的 **1.2.6.0 三份系统文件**，通过 SHA-256 精确识别。
允许多台使用预期 Apple filter 的 iPhone / iPad 共用驱动；同版本号的其他构建、混合版本、
全局 filter 或不符合上述要求的其他 libusb 设备会被拒绝。
已经是本包的 1.4.0.2 时会直接退出，不重复备份或切换驱动。
脚本只更新系统驱动和 System32/SysWOW64 的 DLL；使用的软件包也应带 1.4.0.2 的应用 DLL。

## 回退与中断恢复

备份自动保存在 `%ProgramData%\iOSAudioBridge\DriverBackups\<时间-编号>`，通常是
`C:\ProgramData\iOSAudioBridge\DriverBackups`，不会随安装包或仓库 build 目录一起删除。
正常用户可以读取备份，管理员和 SYSTEM 才能修改。不要在确认稳定前清理该目录。
操作日志位于安装包 `logs/`；成功创建的备份中还会记录每次更换的 `result-*.json`。

- 更换文件或新驱动加载失败时，脚本尝试恢复并加载原驱动；窗口会明确报告是否完成。
- 手动回退：运行 **`05-RestoreLibUsb.cmd`**，有多份备份时选择对应时间；按提示拔线后继续。
  仅接受本电脑、通过完整性校验的备份。回退不卸载 Apple 软件、不重绑设备。
- 升级和回退都是对**整台电脑的共用驱动**操作，不能只升级或回退其中一台手机。
  需要所有 iPhone / iPad 都断开；新增未登记设备插入时也会停止更换。
  备份后若增删了设备登记或改变 Apple 绑定，脚本会拒绝继续使用该备份，需单独核对变化。
  新脚本仍能读取旧版单设备备份，但仅限登记范围仍与该备份一致。
- 操作中误插手机时，脚本会停止继续复制；先拔线，再运行回退入口恢复，避免继续使用混合文件。
- 若升级在写文件前因驱动无法停止而退出，可保持手机断开并按 Windows 要求重启。
  原文件完整时，管理员也可以从安装包目录用同一份备份继续：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Update-LibUsb.ps1 `
    -Mode Upgrade -BackupDirectory 'C:\ProgramData\iOSAudioBridge\DriverBackups\<时间-编号>'
```

回退完成后系统恢复 1.2.6.0，此时 `03-Check.cmd` 固定检查 1.4.0.2，因此不会显示 READY。
需要回退应用 DLL 时，先关闭应用，将所选备份中的 `libusb0-x64.dll` 复制到应用目录并改名为 `libusb0.dll`，再验证采集。
旧仓库升级脚本产生的 `baseline-*` 备份格式不同，仍使用原 `Invoke-LibUsbDriverTrial.ps1 -Mode Restore` 恢复。

## 包内保留了什么

| 内容 | 作用 |
|---|---|
| `AppleMobileDeviceSupport64.msi` | Apple Mobile Device Support 19.4.0.10，安装 Apple 服务等组件 |
| `AppleUSB/` 的五个文件 | Apple 538.0.0.0 复合设备驱动包；与本机原有驱动文件哈希一致 |
| `libusb/` | 1.4.0.2 的 x64 驱动、DLL、精确设备安装工具，x86 兼容 DLL，以及 Microsoft 签名 catalog |
| 五个 `.cmd` 和四个 `.ps1` | 首次安装、状态检查、备份升级及回退 |
| `payload.json` | 版本、来源与文件 SHA-256；脚本先校验哈希和签名 |
| `licenses/` | 原始许可证、作者信息和 libusb 对应源码归档 |

MSI 中的 `usbaapl64` 与单独的 `AppleUSB` 复合驱动不是同一个包，所以两者都保留。
不包含旧驱动管理器、ARM64/x86 内核驱动、示例函数驱动 INF、调试工具或完整 iTunes。
仍保留 x86 用户态 DLL，是为了与系统 SysWOW64 的兼容运行库一致；不是支持 32 位 Windows。

## 安装失败时

- 查看窗口中的错误和 `logs/<时间>-<步骤>/result.json`、`install.log`；Apple MSI 另有 `apple-msi.log`。
- 手机未正常识别时先解决 Apple 连接：使用支持数据传输的线，解锁并信任电脑；可参考
  [Apple 官方识别排查](https://support.apple.com/en-us/108643)。
- 设备服务不是 `usbccgp`、Apple filter 缺失或设备有错误时，第二步会停止；不要用 Zadig 强制替换 Apple 父设备。
- 新内核驱动先由 Windows 加载，通过后才添加手机 filter。加载失败时不会给手机添加 filter；
  已复制的签名文件和 catalog 可能保留，便于检查与重试。
- 添加 filter 后检查失败时，脚本尝试撤回本次给该手机添加的 filter，结果以日志为准。
  此处不卸载 Apple 软件或已有共享服务。要求重启时手动重启，再重新检查。

## 验证范围与来源

本机已完成 1.4.0.2 驱动加载、4 轮静音采集和 USB 恢复测试。
本安装包另行检查了素材哈希、签名、PowerShell 5.1 兼容性及本机只读状态。
独立升级脚本使用临时文件模拟升级与回退，并覆盖复制中断、内核加载失败、误插手机、损坏备份和错误电脑等情形；
当前 1.4.0.2 环境的“无需升级”分支也已实际检查。
多设备模拟覆盖精确选择、同名设备、目标拔线、保留离线登记、重复登记、仅撤回目标设备，
以及共用驱动升级时漏拔第二台、登记范围变化和旧备份兼容性。
这些模拟不等同于新升级脚本在真实 1.2.6.0 电脑上的完整切换验收，本机未为测试而降级。
**尚未在全新 Windows 电脑上执行完整安装，不能把已有环境的检查当作首次安装验收。**

Apple 文件原样取自旧包并保留原签名，产品版权及条款归 Apple。
libusb 来源：[官方 1.4.0.2 发布页](https://github.com/mcuee/libusb-win32/releases/tag/release_1.4.0.2)。
仅注册官方 Microsoft 签名 catalog，使用 [Windows catalog API](https://learn.microsoft.com/en-us/windows/win32/api/mscat/nf-mscat-cryptcatadminaddcatalog)；
不导入测试证书，也不关闭签名检查。脚本调用的上游工具使用 `-di=<完整设备实例>`，不安装全局 USB 类 filter。
自编安装脚本按 GPL-3.0 提供；libusb 的源代码与原始许可保存在 `licenses/`。
