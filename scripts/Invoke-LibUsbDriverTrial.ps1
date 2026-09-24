param(
    [Parameter(Mandatory)][string]$BaselineDirectory,
    [ValidateSet('Check','Apply','Restore')][string]$Mode = 'Check',
    [string]$SignToolPath,
    [switch]$DeviceDisconnected
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$trial = Join-Path $root 'build/driver-evaluation/libusb-win32-1.4.0.2'
$package = Join-Path $trial 'unpacked/libusb-win32-bin-1.4.0.2'
$BaselineDirectory = (Resolve-Path -LiteralPath $BaselineDirectory).Path
$complete = Get-Content -LiteralPath "$BaselineDirectory/snapshot-complete.json" -Raw | ConvertFrom-Json
if (-not $complete.legacy_payload_verified) { throw 'A verified legacy installer payload is required for rollback.' }
$baseline = @(Get-Content -LiteralPath "$BaselineDirectory/files.json" -Raw | ConvertFrom-Json)
foreach ($entry in @(Get-Content -LiteralPath "$BaselineDirectory/backup-hashes.json" -Raw | ConvertFrom-Json)) {
    if ((Get-FileHash -LiteralPath (Join-Path $BaselineDirectory $entry.path) -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Backup changed: $($entry.path)" }
}
$prepared = Get-Content -LiteralPath "$trial/preparation.json" -Raw | ConvertFrom-Json
foreach ($entry in $prepared.files) {
    if ((Get-FileHash -LiteralPath (Join-Path $package $entry.path) -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Candidate changed: $($entry.path)" }
}
if (-not $SignToolPath) { $SignToolPath = Join-Path $root '.tools/sdk-signature-check/signtool.exe' }
$signer = Get-AuthenticodeSignature -LiteralPath $SignToolPath
if ($signer.Status -ne 'Valid' -or $signer.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') { throw 'A Microsoft-signed SignTool is required.' }

$devices = @(Get-Content -LiteralPath "$BaselineDirectory/devices.json" -Raw | ConvertFrom-Json)
$parents = @($devices | Where-Object { $_.service -eq 'usbccgp' -and $_.upper_filters -contains 'libusb0' -and $_.instance_id -match '^USB\\VID_05AC&' })
if ($parents.Count -ne 1) { throw 'This trial supports exactly one recorded Apple composite parent.' }
$parent = $parents[0]
$instance = $parent.instance_id
function Read-DeviceProperty([string]$Key) {
    (Get-PnpDeviceProperty -InstanceId $instance -KeyName $Key).Data
}
function Assert-AppleBinding {
    if ((Read-DeviceProperty 'DEVPKEY_Device_Service') -ne 'usbccgp') { throw 'Apple parent binding changed.' }
    if ((Read-DeviceProperty 'DEVPKEY_Device_DriverVersion') -ne $parent.driver_version) { throw 'Apple driver version changed.' }
    foreach ($item in @(@('DEVPKEY_Device_UpperFilters','upper_filters'),@('DEVPKEY_Device_LowerFilters','lower_filters'))) {
        $actual = @(Read-DeviceProperty $item[0]) -join '|'
        $expected = @($parent.($item[1])) -join '|'
        if ($actual -ne $expected) { throw "Apple filter list changed: $($item[0])" }
    }
}
Assert-AppleBinding
function Assert-ConnectionState {
    $present = @(Get-PnpDevice -PresentOnly | Where-Object InstanceId -eq $instance).Count -gt 0
    if ($DeviceDisconnected) {
        if ($present) { throw 'Keep the recorded iPhone physically disconnected until the operation finishes.' }
    } elseif (-not $present) { throw 'Recorded iPhone is not connected. Use -DeviceDisconnected only after unplugging it.' }
}
Assert-ConnectionState
if (-not $DeviceDisconnected -and (Read-DeviceProperty 'DEVPKEY_Device_ProblemCode') -ne 0) { throw 'Apple parent has a PnP problem; resolve it before a trial.' }
if (Get-Process ZLUsbAudioBridge,AudioProbe,iPhoneMirror,'iPhoneMirror.UsbAudioOnly','AudioBridge.UsbRestore' -ErrorAction SilentlyContinue) { throw 'Close USB capture applications before switching drivers.' }

# The kernel service is shared. Refuse global filters or additional devices.
$filterNodes = @(Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Enum\USB' | ForEach-Object {
    Get-ChildItem -LiteralPath $_.PSPath | ForEach-Object {
        $props = Get-ItemProperty -LiteralPath $_.PSPath
        if (@($props.UpperFilters) -contains 'libusb0' -or @($props.LowerFilters) -contains 'libusb0') { $_.Name }
    }
})
if ($filterNodes.Count -ne 1 -or -not $filterNodes[0].EndsWith('\' + $instance, [StringComparison]::OrdinalIgnoreCase)) { throw 'libusb0 filters affect other devices; this one-device trial cannot proceed.' }
foreach ($key in Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Control\Class') {
    $props = Get-ItemProperty -LiteralPath $key.PSPath
    if (@($props.UpperFilters) -contains 'libusb0' -or @($props.LowerFilters) -contains 'libusb0') { throw 'A global libusb class filter is configured.' }
}
$other = @(Get-CimInstance Win32_PnPEntity -Filter "Service='libusb0'" | Where-Object { $_.Present -and $_.PNPDeviceID -ne $instance })
if ($other.Count) { throw 'Other devices are bound to libusb0.' }
$service = Get-CimInstance Win32_SystemDriver -Filter "Name='libusb0'"
$kernelPath = Join-Path $env:SystemRoot 'System32/drivers/libusb0.sys'
if ([IO.Path]::GetFullPath($service.PathName) -ne [IO.Path]::GetFullPath($kernelPath)) { throw 'Unexpected libusb service ImagePath.' }

$deployments = @(foreach ($pair in @(
    @('System32/drivers/libusb0.sys','bin/amd64/libusb0.sys','system-drivers/libusb0.sys'),
    @('System32/libusb0.dll','bin/amd64/libusb0.dll','System32/libusb0.dll'),
    @('SysWOW64/libusb0.dll','bin/x86/libusb0_x86.dll','SysWOW64/libusb0.dll')
)) {
    $destination = Join-Path $env:SystemRoot $pair[0]
    $old = @($baseline | Where-Object backup -eq $pair[2])
    if ($old.Count -ne 1) { throw "Missing rollback file: $($pair[2])" }
    $new = @($prepared.files | Where-Object path -eq $pair[1])
    if ($new.Count -ne 1) { throw "Missing candidate file: $($pair[1])" }
    $currentHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
    $expected = if ($Mode -in 'Restore','Check') { @($old[0].sha256,$new[0].sha256) } else { @($old[0].sha256) }
    if ($currentHash -notin $expected) { throw "System file differs from recorded versions: $destination" }
    [pscustomobject]@{ destination = $destination; old = (Join-Path $BaselineDirectory $pair[2]); candidate = (Join-Path $package $pair[1]); old_hash = $old[0].sha256; new_hash = $new[0].sha256; current_hash = $currentHash }
})
if ($Mode -eq 'Check' -and @($deployments | Where-Object { $_.current_hash -eq $_.old_hash }).Count -ne 3 -and @($deployments | Where-Object { $_.current_hash -eq $_.new_hash }).Count -ne 3) { throw 'System libusb files contain mixed versions; use Restore to recover the baseline.' }
Write-Output "Preflight passed: one Apple parent, unchanged Apple binding, verified backup and candidate. Mode=$Mode"
if ($Mode -eq 'Check') { return }
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Apply/Restore requires Windows administrator elevation.' }

$logDirectory = Join-Path $trial ('operation-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $Mode)
New-Item -ItemType Directory -Path $logDirectory | Out-Null
Start-Transcript -LiteralPath (Join-Path $logDirectory 'operation.log') | Out-Null
$disabled = $false
$modified = $false
$outcome = [ordered]@{ mode = $Mode; device_disconnected = [bool]$DeviceDisconnected; started_at = (Get-Date -Format o); success = $false; restored_after_failure = $false }
$catalogName = 'iOSAudioBridge-libusb0-trial-1.4.0.2.cat'
$registeredCatalog = Join-Path $env:SystemRoot "System32/CatRoot/{F750E6C3-38EE-11D1-85E5-00C04FC295EE}/$catalogName"
function Run-Pnp([string[]]$Arguments) {
    & "$env:SystemRoot/System32/pnputil.exe" @Arguments
    if ($LASTEXITCODE -ne 0) { throw "PnPUtil returned $LASTEXITCODE. A reboot may be required; no automatic reboot will be performed." }
}
function Stop-TargetDriver {
    Assert-ConnectionState
    if (-not $DeviceDisconnected) {
        $script:disabled = $true
        Run-Pnp -Arguments @('/disable-device',$instance)
    }
    if ((Get-CimInstance Win32_SystemDriver -Filter "Name='libusb0'").State -ne 'Stopped') {
        & sc.exe stop libusb0
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        if ((Get-CimInstance Win32_SystemDriver -Filter "Name='libusb0'").State -eq 'Stopped') { return }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'libusb0 did not unload; no driver files will be replaced while loaded.'
}
function Assert-SafeToCopy {
    Assert-ConnectionState
    if ((Get-CimInstance Win32_SystemDriver -Filter "Name='libusb0'").State -ne 'Stopped') { throw 'libusb0 is loaded; file replacement is prohibited.' }
}
function Start-TargetDevice {
    if ($DeviceDisconnected) {
        Assert-ConnectionState
        & sc.exe start libusb0
        if ($LASTEXITCODE) { throw "Kernel driver start failed: $LASTEXITCODE" }
        if ((Get-CimInstance Win32_SystemDriver -Filter "Name='libusb0'").State -ne 'Running') { throw 'Kernel driver did not enter Running state.' }
        Assert-AppleBinding
        $outcome.device_reconnect_required = $true
        return
    }
    Run-Pnp -Arguments @('/enable-device',$instance)
    $script:disabled = $false
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        if ((Read-DeviceProperty 'DEVPKEY_Device_ProblemCode') -eq 0 -and (Get-CimInstance Win32_SystemDriver -Filter "Name='libusb0'").State -eq 'Running') {
            Assert-AppleBinding
            return
        }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Apple device did not return with a running libusb driver.'
}
try {
    if ($Mode -eq 'Apply') {
        # Register only the signed catalog. The bundled function-driver INF
        # targets unrelated hardware and must not be installed or rebound.
        $catalog = Join-Path $logDirectory $catalogName
        Copy-Item -LiteralPath "$package/bin/libusb0.cat" -Destination $catalog
        if ((Test-Path -LiteralPath $registeredCatalog) -and (Get-FileHash -LiteralPath $registeredCatalog).Hash -ne (Get-FileHash -LiteralPath $catalog).Hash) { throw 'A different catalog already uses the trial catalog name.' }
        & $SignToolPath catdb /v $catalog
        if ($LASTEXITCODE) { throw 'Driver catalog registration failed; current driver remains installed.' }
        $outcome.catalog_registered = $catalogName
        & $SignToolPath verify /kp /a /v "$package/bin/amd64/libusb0.sys"
        if ($LASTEXITCODE) { throw 'Registered catalog does not satisfy kernel signing policy; current driver remains installed.' }
    } else {
        & $SignToolPath verify /kp /a /v $deployments[0].old
        if ($LASTEXITCODE) { throw 'Rollback kernel signature verification failed.' }
    }
    Stop-TargetDriver
    foreach ($file in $deployments) {
        Assert-SafeToCopy
        $source = if ($Mode -eq 'Restore') { $file.old } else { $file.candidate }
        $expectedHash = if ($Mode -eq 'Restore') { $file.old_hash } else { $file.new_hash }
        $modified = $true
        Copy-Item -LiteralPath $source -Destination $file.destination -Force
        if ((Get-FileHash -LiteralPath $file.destination -Algorithm SHA256).Hash -ne $expectedHash) { throw "Installed hash mismatch: $($file.destination)" }
    }
    Start-TargetDevice
    if ($Mode -eq 'Restore' -and (Test-Path -LiteralPath $registeredCatalog)) {
        & $SignToolPath catdb /r /v $catalogName
        if ($LASTEXITCODE) { throw 'Driver restored, but trial catalog removal failed.' }
    }
    $outcome.success = $true
    $outcome.installed_version = (Get-Item -LiteralPath $kernelPath).VersionInfo.FileVersion
} catch {
    $outcome.error = $_.Exception.Message
    if ($modified) {
        try {
            Stop-TargetDriver
            foreach ($file in $deployments) {
                Assert-SafeToCopy
                Copy-Item -LiteralPath $file.old -Destination $file.destination -Force
                if ((Get-FileHash -LiteralPath $file.destination -Algorithm SHA256).Hash -ne $file.old_hash) { throw 'Rollback hash mismatch.' }
            }
            Start-TargetDevice
            $outcome.restored_after_failure = $true
        } catch { $outcome.rollback_error = $_.Exception.Message }
    }
} finally {
    if ($disabled) {
        try { Run-Pnp -Arguments @('/enable-device',$instance) } catch { $outcome.enable_error = $_.Exception.Message }
    }
    if ($Mode -eq 'Apply' -and -not $outcome.success -and (-not $modified -or $outcome.restored_after_failure) -and $outcome.catalog_registered -and (Test-Path -LiteralPath $registeredCatalog)) {
        & $SignToolPath catdb /r /v $catalogName
        $outcome.catalog_cleanup_exit = $LASTEXITCODE
    }
    $outcome.finished_at = (Get-Date -Format o)
    $outcome | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $logDirectory 'result.json') -Encoding utf8
    Stop-Transcript | Out-Null
}
if (-not $outcome.success) { throw "Driver trial did not complete. See $logDirectory/result.json" }
Write-Output "Driver operation complete: $logDirectory"
