param(
    [string]$ToolRoot = 'D:/GreenSoftware/iPhoneMirror/audio-only-work',
    [int]$Jobs = 4,
    [switch]$SkipTests,
    [switch]$NoPublish,
    [switch]$Fresh
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$llvm=Join-Path $root '.tools/llvm-22.1.0/bin'
if(-not(Test-Path "$llvm/clang-cl.exe")){throw 'Run scripts/Prepare-ZLDependencies.ps1 first (LLVM 22.1.0 required).'}
$vc=Get-ChildItem "$ToolRoot/msvc/VC/Tools/MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1
$sdkRoot="$ToolRoot/msvc/Windows Kits/10"
$sdk=Get-ChildItem "$sdkRoot/Include" -Directory | Sort-Object Name -Descending | Select-Object -First 1
$env:Path="$llvm;$($vc.FullName)/bin/Hostx64/x64;$sdkRoot/bin/$($sdk.Name)/x64;$ToolRoot/ninja;$env:Path"
$env:INCLUDE="$($vc.FullName)/include;$($sdk.FullName)/ucrt;$($sdk.FullName)/shared;$($sdk.FullName)/um;$($sdk.FullName)/winrt;$($sdk.FullName)/cppwinrt"
$env:LIB="$($vc.FullName)/lib/x64;$sdkRoot/Lib/$($sdk.Name)/ucrt/x64;$sdkRoot/Lib/$($sdk.Name)/um/x64"
$cmake="$ToolRoot/cmake/cmake-3.31.8-windows-x86_64/bin/cmake.exe"
$build=Join-Path $root 'build/zl-bridge'
$configureArgs=@('-S',$root,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_C_COMPILER=clang-cl','-DCMAKE_CXX_COMPILER=clang-cl','-DAB_BUILD_ZL=ON')
if($Fresh){$configureArgs+='--fresh'}
& $cmake @configureArgs
if($LASTEXITCODE){throw 'Configure failed'}
& $cmake --build $build --parallel $Jobs
if($LASTEXITCODE){throw 'Build failed'}
if(-not $SkipTests){
    & (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir $build --output-on-failure
    if($LASTEXITCODE){throw 'Native tests failed'}
}
if($NoPublish){return}
$dist=Join-Path $root 'dist/ZLUsbAudioBridge'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$app=Get-ChildItem $build -Recurse -Filter ZLUsbAudioBridge.exe | Select-Object -First 1
if(-not $app){throw 'Standalone executable missing'}
$pins=Get-Content -LiteralPath "$root/third_party/ZLCompressor.pins.json" -Raw | ConvertFrom-Json
if($app.VersionInfo.FileVersion -ne $pins.ZLCompressor.version){throw 'EXE version does not match the pinned ZLCompressor version'}
Copy-Item -LiteralPath $app.FullName -Destination $dist -Force
foreach($file in 'AudioBridge.UsbRestore.exe','AudioBridge.UsbRepair.exe','libusb0.dll') {Copy-Item -LiteralPath "$build/bin/$file" -Destination $dist -Force}
$vendor=Join-Path $root 'third_party/ZLCompressor'
$licenses=Join-Path $dist 'licenses'
New-Item -ItemType Directory -Force $licenses | Out-Null
Copy-Item -LiteralPath "$vendor/LICENSE.md" -Destination "$dist/LICENSE-AGPL-3.0.md" -Force
Copy-Item -LiteralPath "$root/LICENSE" -Destination "$licenses/AudioBridge-GPL-3.0.txt" -Force
Copy-Item -LiteralPath "$vendor/JUCE/LICENSE.md" -Destination "$licenses/JUCE-LICENSE.md" -Force
Copy-Item -LiteralPath "$vendor/JUCE/JUCE.spdx.json" -Destination "$licenses/JUCE.spdx.json" -Force
Copy-Item -LiteralPath "$vendor/highway/LICENSE" -Destination "$licenses/Highway-LICENSE.txt" -Force
Copy-Item -LiteralPath "$vendor/zldsp_fft/LICENSE.md" -Destination "$licenses/zldsp_fft-LICENSE.md" -Force
foreach($name in 'pamplejuce_LICENSE.txt','material-design-icons_LICENSE.txt') {Copy-Item -LiteralPath "$vendor/licenses/$name" -Destination $licenses -Force}
Copy-Item -LiteralPath "$root/third_party/zl-bridge-licenses/Inter-OFL.txt" -Destination $licenses -Force
foreach($name in 'COPYING_LGPL.txt','COPYING_GPL.txt','AUTHORS.txt','README.txt','CHANGELOG.txt','pins.json','libusb-win32-src-1.4.0.2.zip') {
    $target=Join-Path $licenses 'libusb-win32';New-Item -ItemType Directory -Force $target | Out-Null
    Copy-Item -LiteralPath "$root/third_party/libusb-win32/$name" -Destination $target -Force
}
Get-ChildItem "$vendor/JUCE/modules" -Recurse -File | Where-Object { $_.Name -match '^(LICENSE|COPYING|NOTICE)' } | ForEach-Object {
    $relative=[IO.Path]::GetRelativePath("$vendor/JUCE/modules",$_.FullName)
    $target=Join-Path "$licenses/JUCE-components" $relative
    New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    Copy-Item -LiteralPath $_.FullName -Destination $target -Force
}
Copy-Item -LiteralPath "$root/THIRD_PARTY_NOTICES.md","$root/third_party/ZLCompressor.pins.json","$root/third_party/ZLCompressor.bridge.patch" -Destination $dist -Force
Copy-Item -LiteralPath "$root/docs/zl-bridge-running.zh-CN.md" -Destination "$dist/README.zh-CN.md" -Force
Copy-Item -LiteralPath "$root/docs/zl-bridge-validation.zh-CN.md" -Destination "$dist/VALIDATION.zh-CN.md" -Force
Copy-Item -LiteralPath "$root/docs/driver-upgrade-evaluation.zh-CN.md" -Destination $dist -Force
Copy-Item -LiteralPath "$root/docs/usb-device-repair.zh-CN.md" -Destination $dist -Force
$driverEvidence=Join-Path $dist 'test-results/2026-09-24-driver-upgrade'
New-Item -ItemType Directory -Force -Path $driverEvidence | Out-Null
Copy-Item -LiteralPath "$root/docs/test-results/2026-09-24-driver-upgrade/summary.json" -Destination $driverEvidence -Force
$uiEvidence=Join-Path $dist 'test-results/2026-09-24-ui-theme'
New-Item -ItemType Directory -Force -Path $uiEvidence | Out-Null
Get-ChildItem -LiteralPath "$root/docs/test-results/2026-09-24-ui-theme" -File | Copy-Item -Destination $uiEvidence -Force
$zlUpgradeEvidence=Join-Path $dist 'test-results/2026-10-03-zl-0.6.1'
New-Item -ItemType Directory -Force -Path $zlUpgradeEvidence | Out-Null
Get-ChildItem -LiteralPath "$root/docs/test-results/2026-10-03-zl-0.6.1" -File | Copy-Item -Destination $zlUpgradeEvidence -Force
Get-ChildItem $dist -File | Where-Object { $_.Extension -in '.exe','.dll' } | ForEach-Object {
    [pscustomobject]@{file=$_.Name;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
} | ConvertTo-Json | Set-Content "$dist/build-hashes.json" -Encoding utf8
Write-Output "Published to $dist"
