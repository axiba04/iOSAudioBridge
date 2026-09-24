param([string]$OutputPath)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$kit = Join-Path $root 'DriverInstallKit'
. (Join-Path $kit 'DriverKit.Common.ps1')
$null = Assert-KitPayload $kit
if (-not $OutputPath) { $OutputPath = Join-Path $root 'dist/DriverInstallKit-x64-1.4.0.2.zip' }
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
if (-not $OutputPath.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Build output must be inside this workspace.' }
New-Item -ItemType Directory -Force -Path (Split-Path $OutputPath) | Out-Null
# Explicit allowlist: local device logs and per-machine identifiers never enter the archive.
$names = @('01-AppleSupport.cmd','02-iPhoneFilter.cmd','03-Check.cmd','04-UpgradeLibUsb.cmd','05-RestoreLibUsb.cmd','Install-Drivers.ps1','Update-LibUsb.ps1','DriverKit.Common.ps1','DriverKit.Upgrade.ps1','README.zh-CN.md','payload.json','AppleMobileDeviceSupport64.msi','AppleUSB','libusb','licenses')
$paths = @($names | ForEach-Object { Join-Path $kit $_ })
Compress-Archive -LiteralPath $paths -DestinationPath $OutputPath -CompressionLevel Optimal -Force
[pscustomobject]@{path=$OutputPath;bytes=(Get-Item -LiteralPath $OutputPath).Length;sha256=(Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash}
