param([string]$SourceRoot = 'D:\GreenSoftware\iPhoneMirror-v1.8.3-win-x64\audio-only-source')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot
$snapshotRoot = Join-Path $projectRoot 'third_party/iphone-mirror-baseline'
if (Test-Path (Join-Path $snapshotRoot 'manifest.json')) { throw 'Baseline already imported; do not overwrite provenance.' }
$files = @('LICENSE','THIRD_PARTY_NOTICES.md','docs/USB_AUDIO_ONLY.md',
 'src/Core/src/Protocol/QuickTimePacket.cpp','src/Core/src/Protocol/QuickTimePacket.h',
 'src/Core/src/Protocol/QuickTimeSession.cpp','src/Core/src/Protocol/QuickTimeSession.h',
 'src/Core/src/Protocol/Plist.cpp','src/Core/src/Protocol/Plist.h',
 'src/Core/src/Media/CoreMedia.cpp','src/Core/src/Media/CoreMedia.h',
 'src/Core/src/Transport/LibUsb0Transport.cpp','src/Core/src/Transport/LibUsb0Transport.h',
 'src/Core/src/Transport/LibUsb0Readiness.h','src/Core/src/Transport/AppleUsbIdentityCache.h',
 'src/Core/src/Transport/UsbInterfaceTransitionPolicy.h','src/Core/src/Transport/QtUsbTransport.h',
 'src/Core/src/Transport/Socket.cpp','src/Core/src/Transport/Socket.h',
 'src/Core/src/Transport/UsbMuxClient.cpp','src/Core/src/Transport/UsbMuxClient.h',
 'src/Core/src/Device/AppleUsbDiscovery.cpp','src/Core/src/Device/AppleUsbDiscovery.h',
 'src/Core/src/Device/DeviceManager.cpp','src/Core/src/Device/DeviceManager.h',
 'src/Core/src/Capture/CaptureSession.cpp','src/Core/src/Capture/UsbConfigurationRestorePolicy.h',
 'src/Core/src/Audio/WasapiRenderer.cpp','src/Core/src/Audio/WasapiRenderer.h',
 'src/Core/src/Logging.cpp','src/Core/src/Logging.h',
 'src/Core/tools/UsbConfigurationSwitch.cpp','src/Core/tools/UsbAudioOnly.cpp',
 'src/Core/tests/CoreTests.cpp')
$manifest = foreach ($relative in $files) {
    $source = Join-Path $SourceRoot $relative
    $target = Join-Path $snapshotRoot $relative
    New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $target
    [ordered]@{path=$relative; sha256=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash}
}
$head = (& git -C $SourceRoot rev-parse HEAD).Trim()
[ordered]@{repository='https://github.com/RayrenSX/iPhoneMirror'; baseCommit=$head; includesUncommittedAudioOnlyChanges=$true; files=@($manifest)} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $snapshotRoot 'manifest.json') -Encoding utf8
& git -c core.safecrlf=false -C $SourceRoot diff --binary --no-ext-diff -- src/Core | Set-Content (Join-Path $snapshotRoot 'local-core.patch') -Encoding utf8
foreach ($relative in $files | Where-Object { $_ -like 'src/Core/src/*' -and $_ -notmatch '/(CaptureSession|DeviceManager|WasapiRenderer)\.' }) {
    $target = Join-Path $projectRoot ($relative -replace '^src/Core/', 'src/AudioCore/')
    New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $SourceRoot $relative) -Destination $target
}
New-Item -ItemType Directory -Path (Join-Path $projectRoot 'src/AudioBridge.UsbRestore') -Force | Out-Null
Copy-Item (Join-Path $SourceRoot 'src/Core/tools/UsbConfigurationSwitch.cpp') (Join-Path $projectRoot 'src/AudioBridge.UsbRestore/main.cpp')
New-Item -ItemType Directory -Path (Join-Path $projectRoot 'tests/fixtures') -Force | Out-Null
Copy-Item (Join-Path $SourceRoot 'src/Core/tests/fixtures/quicktime_video_hack') (Join-Path $projectRoot 'tests/fixtures/quicktime_video_hack') -Recurse
New-Item -ItemType Directory -Path (Join-Path $projectRoot 'third_party/libusb-win32') -Force | Out-Null
Copy-Item (Join-Path $SourceRoot 'third_party/libusb-win32/*') (Join-Path $projectRoot 'third_party/libusb-win32') -Recurse
$runtimeRoot = Join-Path $projectRoot 'third_party/libusb-win32/bin/x64'
New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
Copy-Item (Join-Path $SourceRoot 'src/DriverInstaller/Assets/libusb-win32-1.2.6.0/amd64/libusb0.dll') $runtimeRoot
Copy-Item (Join-Path $SourceRoot 'LICENSE') (Join-Path $projectRoot 'LICENSE')
Write-Output "Imported $($files.Count) source files and audio protocol fixtures."
