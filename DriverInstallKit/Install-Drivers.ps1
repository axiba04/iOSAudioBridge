param(
    [ValidateSet('Apple','Filter','Check')][string]$Stage = 'Check',
    [switch]$ValidateOnly,
    [string]$LogDirectory,
    [string]$InstanceId
)
$ErrorActionPreference = 'Stop'
$mutex = $null
$locked = $false
try {
    if (-not [Environment]::Is64BitProcess -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64') { throw 'Use Windows x64 (Intel/AMD), not x86 or ARM64.' }
    . (Join-Path $PSScriptRoot 'DriverKit.Common.ps1')
    $manifest = Assert-KitPayload $PSScriptRoot
    if ($ValidateOnly) {
        Initialize-KitCatalogApi
        Write-Host 'Payload hashes, signatures and native helper compilation passed. No system changes.'
        exit 0
    }
    if ($Stage -eq 'Check') {
        $service = Get-Service 'Apple Mobile Device Service' -ErrorAction SilentlyContinue
        if (-not $service -or $service.Status -ne 'Running') { throw 'Apple Mobile Device Service is not running. Complete step 1 first.' }
        $phones = @(Get-KitPhones)
        if ($InstanceId) { $phones = @(Select-KitPhone $phones $InstanceId) }
        if (-not $phones.Count) { throw 'No supported iPhone/iPad is connected.' }
        Assert-KitFilterScope
        foreach ($file in @(Get-KitDeployments $manifest $PSScriptRoot)) {
            if (-not (Test-Path -LiteralPath $file.destination)) { throw 'libusb files are missing. Complete step 2.' }
            Assert-CompatibleFile $file.destination $file.hash
        }
        if ((Get-CimInstance Win32_SystemDriver -Filter "Name='libusb0'").State -ne 'Running') { throw 'libusb driver is not running. Complete step 2, then reconnect.' }
        $statuses = @(Get-KitPhoneStatus $phones)
        foreach ($item in $statuses) { Write-Host ("{0}: {1}`n  {2}`n  {3}" -f $item.status,$item.name,$item.instance,$item.detail) }
        if (@($statuses | Where-Object status -ne 'READY').Count) { throw 'Some connected devices are not ready; see each device status above.' }
        Write-Host ("READY: {0} connected iPhone/iPad device(s); shared libusb 1.4.0.2 active." -f $statuses.Count)
        Write-Host 'Open ZLUsbAudioBridge, start muted capture, then unmute when ready.'
        exit 0
    }
    if (Get-Process ZLUsbAudioBridge,AudioProbe,iPhoneMirror,'iPhoneMirror.UsbAudioOnly','AudioBridge.UsbRestore' -ErrorAction SilentlyContinue) { throw 'Close all phone capture applications before installing drivers.' }
    if ($Stage -eq 'Filter') {
        $phone = Select-KitPhone @(Get-KitPhones) $InstanceId -Interactive
        $InstanceId = $phone.instance
        Write-Host ("本次登记：{0}`n{1}" -f $phone.name,$InstanceId)
    }
    if (-not $LogDirectory) { $LogDirectory = Join-Path $PSScriptRoot ('logs/' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $Stage) }
    New-Item -ItemType Directory -Force -Path $LogDirectory | Out-Null
    $LogDirectory = (Resolve-Path -LiteralPath $LogDirectory).Path
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        $arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $PSCommandPath + '" -Stage ' + $Stage + ' -LogDirectory "' + $LogDirectory + '"'
        if ($InstanceId) { $arguments += ' -InstanceId "' + $InstanceId + '"' }
        $child = Start-Process -FilePath "$env:SystemRoot/System32/WindowsPowerShell/v1.0/powershell.exe" -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -Wait -PassThru
        if (Test-Path -LiteralPath (Join-Path $LogDirectory 'result.json')) { Get-Content -LiteralPath (Join-Path $LogDirectory 'result.json') | Out-Host }
        Write-Host "Installation log: $LogDirectory"
        exit $child.ExitCode
    }
    # Use the same lock as upgrade/restore: these operations share one driver.
    $mutex = New-Object Threading.Mutex($false, 'Global\iOSAudioBridge-LibUsbUpgrade')
    try { $locked = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked = $true }
    if (-not $locked) { throw 'Another driver installation, upgrade or restore is running.' }
    Start-Transcript -LiteralPath (Join-Path $LogDirectory 'install.log') | Out-Null
    try {
        $reboot = $false
        if ($Stage -eq 'Apple') {
            if (@(Get-PnpDevice -PresentOnly | Where-Object InstanceId -like 'USB\VID_05AC*').Count) { throw 'Unplug the iPhone before step 1. Reconnect it after this step completes.' }
            $service = Get-Service 'Apple Mobile Device Service' -ErrorAction SilentlyContinue
            if (-not $service) {
                $arguments = '/i "' + (Join-Path $PSScriptRoot 'AppleMobileDeviceSupport64.msi') + '" /qn /norestart /L*v "' + (Join-Path $LogDirectory 'apple-msi.log') + '"'
                $msi = Start-Process "$env:SystemRoot/System32/msiexec.exe" -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
                if ($msi.ExitCode -notin 0,3010) { throw "Apple MSI failed: $($msi.ExitCode). See apple-msi.log." }
                $reboot = $msi.ExitCode -eq 3010
            }
            & "$env:SystemRoot/System32/pnputil.exe" /add-driver (Join-Path $PSScriptRoot 'AppleUSB/AppleUsb.inf') /install | Out-Host
            if ($LASTEXITCODE -notin 0,3010) { throw "Apple driver staging failed: $LASTEXITCODE" }
            $reboot = $reboot -or $LASTEXITCODE -eq 3010
            $service = Get-Service 'Apple Mobile Device Service' -ErrorAction Stop
            if ($service.Status -ne 'Running' -and -not $reboot) { Start-Service -Name $service.Name }
            $message = if ($reboot) { 'Apple support installed. Reboot Windows before step 2.' } else { 'Apple support prepared. Connect one iPhone, unlock/trust it, then run step 2.' }
        } else {
            $service = Get-Service 'Apple Mobile Device Service' -ErrorAction Stop
            if ($service.Status -ne 'Running') { throw 'Apple Mobile Device Service must be running before step 2.' }
            $phone = Select-KitPhone @(Get-KitPhones) $InstanceId
            Install-KitFilter $phone $manifest $PSScriptRoot $LogDirectory
            $message = 'Selected iPhone/iPad registered. Reconnect it and run 03-Check.cmd. Repeat step 2 for another device.'
        }
        @{success=$true;stage=$Stage;reboot_required=$reboot;message=$message} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $LogDirectory 'result.json') -Encoding UTF8
        Write-Host $message
    } catch {
        @{success=$false;stage=$Stage;message=$_.Exception.Message} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $LogDirectory 'result.json') -Encoding UTF8
        throw
    } finally { Stop-Transcript | Out-Null }
    if ($reboot) { exit 3010 }
} catch {
    $failure = $_.Exception.Message
    # Selection is rechecked before the elevated transcript starts. Preserve
    # that failure too, so the visible parent can report an unplug during UAC.
    if ($LogDirectory -and (Test-Path -LiteralPath $LogDirectory)) {
        try { @{success=$false;stage=$Stage;message=$failure} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $LogDirectory 'result.json') -Encoding UTF8 } catch {}
    }
    Write-Host ('ERROR: ' + $failure) -ForegroundColor Red
    exit 1
} finally {
    if ($locked) { $mutex.ReleaseMutex() }
    if ($mutex) { $mutex.Dispose() }
}
