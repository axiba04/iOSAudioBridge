param(
    [ValidateSet('Check','Upgrade','Restore')][string]$Mode = 'Check',
    [string]$BackupDirectory,
    [switch]$Elevated
)
$ErrorActionPreference = 'Stop'
$exitCode = 1
$transcribing = $false
$mutex = $null
$locked = $false
try {
    if (-not [Environment]::Is64BitProcess -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64') { throw 'Use Windows x64 (Intel/AMD), not x86 or ARM64.' }
    . (Join-Path $PSScriptRoot 'DriverKit.Common.ps1')
    . (Join-Path $PSScriptRoot 'DriverKit.Upgrade.ps1')
    Write-Host '正在校验驱动安装包，请稍候……'
    $manifest = Assert-KitPayload $PSScriptRoot
    $files = @(Get-UpgradeFiles $manifest $PSScriptRoot)
    if ($Mode -ne 'Restore') {
        $state = Get-UpgradeFileState $files
        if ($Mode -eq 'Upgrade' -and $state -eq 'Current') {
            Write-Host '系统文件已经是 1.4.0.2，无需重复升级。可运行 03-Check.cmd 检查连接状态。'
            $exitCode = 0
            return
        }
        Assert-UpgradeIdle
        if ($Mode -eq 'Upgrade' -and $BackupDirectory) {
            $BackupDirectory = (Resolve-Path -LiteralPath $BackupDirectory).Path
            $backup = Read-UpgradeBackup $BackupDirectory $files
            $phone = @(Get-UpgradeBackupPhones $backup)
        } else { $phone = @(Get-UpgradePhones) }
        Assert-UpgradeBinding $phone
        if ($Mode -eq 'Check') {
            Write-Host ("只读检查通过。驱动文件状态：{0}（Legacy=1.2.6.0，Current=1.4.0.2）。" -f $state)
            Write-Host '未备份、停用或更换任何驱动。'
            $exitCode = 0
            return
        }
    }
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        if ($Elevated) { throw 'Administrator elevation was not obtained.' }
        $arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $PSCommandPath + '" -Mode ' + $Mode + ' -Elevated'
        if ($BackupDirectory) { $arguments += ' -BackupDirectory "' + [IO.Path]::GetFullPath($BackupDirectory) + '"' }
        # This is an interactive console: it must show the unplug prompt and the
        # backup selection. Background MSI/filter helpers remain hidden.
        $child = Start-Process "$env:SystemRoot/System32/WindowsPowerShell/v1.0/powershell.exe" -ArgumentList $arguments -Verb RunAs -WindowStyle Normal -Wait -PassThru
        $exitCode = $child.ExitCode
        return
    }
    $mutex = New-Object Threading.Mutex($false, 'Global\iOSAudioBridge-LibUsbUpgrade')
    try { $locked = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked = $true }
    if (-not $locked) { throw 'Another libusb upgrade/restore is running.' }
    $logDirectory = Join-Path $PSScriptRoot ('logs/' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $Mode + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
    New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
    Start-Transcript -LiteralPath (Join-Path $logDirectory 'operation.log') | Out-Null
    $transcribing = $true
    Assert-UpgradeIdle
    if ($Mode -eq 'Upgrade') {
        # Repeat preflight in the elevated process, before creating the backup.
        if ($BackupDirectory) {
            $backup = Read-UpgradeBackup $BackupDirectory $files
            $phone = @(Get-UpgradeBackupPhones $backup)
            Assert-UpgradeBinding $phone
        } else {
            $phone = @(Get-UpgradePhones)
            $BackupDirectory = Save-UpgradeBackup $phone $files
        }
        Write-Host "旧版备份已完成：$BackupDirectory"
    } else {
        if (-not $BackupDirectory) {
            $candidates = @()
            $backupRoot = Get-UpgradeBackupRoot
            if (Test-Path -LiteralPath $backupRoot) {
                foreach ($folder in Get-ChildItem -LiteralPath $backupRoot -Directory | Sort-Object Name -Descending) {
                    try {
                        $record = Read-UpgradeBackup $folder.FullName $files
                        $candidates += [pscustomobject]@{directory=$folder.FullName;created=$record.created_at;phone=(@((Get-UpgradeBackupPhones $record).name) -join ', ')}
                    } catch { Write-Warning "跳过无效备份：$($folder.Name)" }
                }
            }
            if (-not $candidates.Count) { throw 'No valid backup made by this upgrade script exists on this computer.' }
            for ($i = 0; $i -lt $candidates.Count; $i++) { Write-Host ("[{0}] {1}  {2}" -f ($i+1),$candidates[$i].created,$candidates[$i].phone) }
            $selection = 1
            if ($candidates.Count -gt 1) {
                $answer = Read-Host '输入要恢复的备份编号'
                if (-not [int]::TryParse($answer, [ref]$selection) -or $selection -lt 1 -or $selection -gt $candidates.Count) { throw 'Invalid backup selection.' }
            }
            $BackupDirectory = $candidates[$selection-1].directory
        }
        $BackupDirectory = (Resolve-Path -LiteralPath $BackupDirectory).Path
        $backup = Read-UpgradeBackup $BackupDirectory $files
        $phone = @(Get-UpgradeBackupPhones $backup)
        Assert-UpgradeBinding $phone
        Write-Host "将恢复此备份中的系统驱动：$BackupDirectory"
    }
    Write-Host ("本次更换的是共用驱动，涉及 {0} 台已登记设备（包括当前未连接的设备）：" -f $phone.Count)
    foreach ($device in $phone) { Write-Host ("  {0}  {1}" -f $device.name,$device.instance) }
    Write-Host '现在拔下所有 iPhone / iPad 的 USB 线，并保持断开，直到窗口通知可以连接。' -ForegroundColor Yellow
    $answer = Read-Host '拔线后按回车继续；输入 Q 取消'
    if ($answer -eq 'Q') { Write-Host '已取消，系统驱动文件未改动。'; $exitCode = 0; return }
    $result = Invoke-UpgradeTransaction $Mode $BackupDirectory $manifest $PSScriptRoot
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $logDirectory 'result.json') -Encoding UTF8
    if (-not $result.success) {
        Write-Host ("操作未完成：{0}" -f $result.error) -ForegroundColor Red
        if ($result.original_restored) { Write-Host '已自动恢复原版 1.2.6.0。现在可以插回手机。' }
        elseif (-not $result.files_modified) { Write-Host '系统驱动文件未更换。查看日志；如 Windows 要求重启，先重启再尝试。' }
        else { Write-Host '自动回退未完成。请保持手机断开，用 05-RestoreLibUsb.cmd 选择此备份恢复。' -ForegroundColor Yellow }
        $result | Format-List | Out-Host
        throw "See $logDirectory and $BackupDirectory for recovery details."
    }
    Write-Host ("已完成：系统 libusb 版本 {0}。现在可以插回 iPhone 并解锁。" -f $result.installed_version) -ForegroundColor Green
    if ($Mode -eq 'Upgrade') {
        Write-Host '接着运行 03-Check.cmd，使用带 1.4.0.2 DLL 的新版软件做静音采集和停止恢复测试。'
    } else {
        Write-Host '回退仅恢复系统三份 libusb 文件。03-Check.cmd 固定检查 1.4.0.2，回退后不适用。'
        Write-Host "应用需要旧 DLL 时，在应用关闭后，将 $BackupDirectory\libusb0-x64.dll 复制到应用目录并命名为 libusb0.dll。"
    }
    Write-Host "请保留备份：$BackupDirectory"
    $exitCode = 0
} catch {
    Write-Host ('错误：' + $_.Exception.Message) -ForegroundColor Red
    if ($BackupDirectory) { Write-Host "备份位置：$BackupDirectory" }
    $exitCode = 1
} finally {
    if ($transcribing) { Stop-Transcript | Out-Null }
    if ($locked) { $mutex.ReleaseMutex() }
    if ($mutex) { $mutex.Dispose() }
    if ($Elevated) { $null = Read-Host '按回车关闭此窗口' }
    # Also covers early returns (already current, check, cancel and UAC parent).
    exit $exitCode
}
