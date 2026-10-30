param(
    [string]$OutputDirectory,
    [string]$LegacyPayloadDirectory
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $root ('build/driver-evaluation/baseline-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new output directory to preserve earlier baselines.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null

function Save-Json($Name, $Value) {
    ConvertTo-Json -InputObject $Value -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputDirectory $Name) -Encoding utf8
}
function Copy-EvidenceFile([string]$Source, [string]$Relative) {
    $target = Join-Path $OutputDirectory $Relative
    New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $target
    $signature = Get-AuthenticodeSignature -LiteralPath $Source
    [pscustomobject]@{
        source = $Source; backup = $Relative
        version = (Get-Item -LiteralPath $Source).VersionInfo.FileVersion
        sha256 = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
        signature_status = [string]$signature.Status
        signer = $signature.SignerCertificate.Subject
    }
}

# Read-only inspection: no device restart, registry import, service change or USB request.
$present = @(Get-PnpDevice -PresentOnly)
$devices = @($present | Where-Object { $_.InstanceId -match '^USB\\VID_05AC' -or $_.FriendlyName -match 'libusb' } | ForEach-Object {
    $device = $_
    $properties = @(Get-PnpDeviceProperty -InstanceId $device.InstanceId)
    $data = @{}
    foreach ($property in $properties) { $data[$property.KeyName] = $property.Data }
    [pscustomobject]@{
        instance_id = $device.InstanceId; name = $device.FriendlyName; status = $device.Status
        class = $device.Class; service = $data['DEVPKEY_Device_Service']
        problem_code = $data['DEVPKEY_Device_ProblemCode']
        driver_version = $data['DEVPKEY_Device_DriverVersion']
        driver_date = $data['DEVPKEY_Device_DriverDate']; inf = $data['DEVPKEY_Device_DriverInfPath']
        upper_filters = @($data['DEVPKEY_Device_UpperFilters'])
        lower_filters = @($data['DEVPKEY_Device_LowerFilters'])
        parent = $data['DEVPKEY_Device_Parent']
    }
})
Save-Json 'devices.json' $devices
if (-not @($devices | Where-Object instance_id -match '^USB\\VID_05AC').Count) { throw 'No connected Apple USB device. Incomplete snapshot retained for diagnosis.' }

$drivers = @(Get-CimInstance Win32_SystemDriver | Where-Object Name -match '^(libusb0|AppleLowerFilter|AppleKmdfFilter|usbaapl64|usbaapl)$')
Save-Json 'driver-services.json' @($drivers | Select-Object Name,State,StartMode,PathName)
$appleService = Get-CimInstance Win32_Service -Filter "Name='Apple Mobile Device Service'"
Save-Json 'apple-service.json' ($appleService | Select-Object Name,State,StartMode,PathName)
$os = Get-CimInstance Win32_OperatingSystem
$secureBoot = Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\SecureBoot\State' -ErrorAction SilentlyContinue
$hvci = Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity' -ErrorAction SilentlyContinue
Save-Json 'system.json' ([ordered]@{
    captured_at = (Get-Date -Format o); os = $os.Caption; version = $os.Version; build = $os.BuildNumber
    secure_boot_registry = $secureBoot.UEFISecureBootEnabled; memory_integrity_configured = $hvci.Enabled
    elevated = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
})

# Preserve all configured libusb device filters, including disconnected/non-Apple devices.
# Updating libusb0.sys affects every instance using the same service.
$filters = @(Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Enum\USB' | ForEach-Object {
    Get-ChildItem -LiteralPath $_.PSPath | ForEach-Object {
        $entry = Get-ItemProperty -LiteralPath $_.PSPath
        if (@($entry.UpperFilters) -contains 'libusb0' -or @($entry.LowerFilters) -contains 'libusb0') {
            [pscustomobject]@{ registry_path = $_.Name; upper_filters = @($entry.UpperFilters); lower_filters = @($entry.LowerFilters) }
        }
    }
})
Save-Json 'libusb-device-filters.json' $filters
$classFilters = @(Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Control\Class' | ForEach-Object {
    $entry = Get-ItemProperty -LiteralPath $_.PSPath
    if (@($entry.UpperFilters) -contains 'libusb0' -or @($entry.LowerFilters) -contains 'libusb0') {
        [pscustomobject]@{ registry_path = $_.Name; upper_filters = @($entry.UpperFilters); lower_filters = @($entry.LowerFilters) }
    }
})
Save-Json 'libusb-class-filters.json' $classFilters
foreach ($name in @('libusb0','AppleLowerFilter','AppleKmdfFilter','Apple Mobile Device Service')) {
    if (Test-Path -LiteralPath "HKLM:\SYSTEM\CurrentControlSet\Services\$name") {
        & reg.exe export "HKLM\SYSTEM\CurrentControlSet\Services\$name" (Join-Path $OutputDirectory ($name + '.reg')) /y | Out-Null
        if ($LASTEXITCODE) { throw "Registry export failed: $name" }
    }
}

$files = [Collections.Generic.List[object]]::new()
foreach ($driver in $drivers) {
    $path = [Environment]::ExpandEnvironmentVariables($driver.PathName).Trim('"')
    if ($path.StartsWith('\SystemRoot\', [StringComparison]::OrdinalIgnoreCase)) { $path = Join-Path $env:SystemRoot $path.Substring(12) }
    $files.Add((Copy-EvidenceFile $path ('system-drivers/' + [IO.Path]::GetFileName($path))))
}
foreach ($relative in @('System32/libusb0.dll','SysWOW64/libusb0.dll')) {
    $path = Join-Path $env:SystemRoot $relative
    if (Test-Path -LiteralPath $path) { $files.Add((Copy-EvidenceFile $path $relative)) }
}
foreach ($relative in @('third_party/libusb-win32/bin/x64/libusb0.dll','dist/AudioBridge/libusb0.dll')) {
    $path = Join-Path $root $relative
    if (Test-Path -LiteralPath $path) { $files.Add((Copy-EvidenceFile $path ('workspace/' + $relative))) }
}
if ($appleService) {
    $path = $appleService.PathName.Trim('"')
    $files.Add((Copy-EvidenceFile $path ('apple-service/' + [IO.Path]::GetFileName($path))))
}

# Copy the exact signed DriverStore package by matching its published INF hash.
# No elevation or driver installation is needed for this read-only backup.
$packages = @()
foreach ($inf in @($devices.inf | Where-Object { $_ -match '^oem\d+\.inf$' } | Sort-Object -Unique)) {
    $published = Join-Path $env:SystemRoot "INF/$inf"
    $hash = (Get-FileHash -LiteralPath $published -Algorithm SHA256).Hash
    $matches = @(Get-ChildItem (Join-Path $env:SystemRoot 'System32/DriverStore/FileRepository') -Filter '*.inf' -Recurse -File |
        Where-Object { $_.Length -eq (Get-Item -LiteralPath $published).Length } |
        Where-Object { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash -eq $hash })
    if ($matches.Count -ne 1) { throw "Cannot uniquely locate DriverStore package for $inf" }
    $package = $matches[0].Directory.FullName
    $target = Join-Path $OutputDirectory "driver-packages/$inf"
    New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
    Copy-Item -LiteralPath $package -Destination $target -Recurse
    $packages += [pscustomobject]@{ published_inf = $inf; original_inf = $matches[0].Name; source = $package; backup = "driver-packages/$inf" }
}
Save-Json 'driver-packages.json' $packages
if ($LegacyPayloadDirectory) {
    if (-not (Test-Path -LiteralPath (Join-Path $LegacyPayloadDirectory 'amd64/libusb0.sys'))) { throw 'Legacy payload does not contain amd64/libusb0.sys.' }
    Copy-Item -LiteralPath $LegacyPayloadDirectory -Destination (Join-Path $OutputDirectory 'legacy-installer-payload') -Recurse
    $installed = $files | Where-Object backup -eq 'system-drivers/libusb0.sys'
    $payloadHash = (Get-FileHash -LiteralPath (Join-Path $LegacyPayloadDirectory 'amd64/libusb0.sys') -Algorithm SHA256).Hash
    if ($installed.sha256 -ne $payloadHash) { throw 'Legacy installer payload differs from the installed kernel driver.' }
}
Save-Json 'files.json' @($files)
$manifest = @(Get-ChildItem -LiteralPath $OutputDirectory -Recurse -File | ForEach-Object {
    [pscustomobject]@{ path = [IO.Path]::GetRelativePath($OutputDirectory, $_.FullName); bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
})
Save-Json 'backup-hashes.json' $manifest
Save-Json 'snapshot-complete.json' ([ordered]@{ completed_at = (Get-Date -Format o); files = $manifest.Count; legacy_payload_verified = [bool]$LegacyPayloadDirectory; purpose = 'Evidence and recovery materials; restore procedure must be validated separately.' })
Write-Output "Driver baseline saved: $OutputDirectory"
