param(
 [string]$ToolRoot = 'D:\GreenSoftware\iPhoneMirror-v1.8.3-win-x64\audio-only-work',
 [switch]$NativeOnly,
 [switch]$SkipTests,
 [switch]$FrameworkDependent
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot
$buildRoot=Join-Path $projectRoot 'build/native'
$cmakeCommand=Get-Command cmake -ErrorAction SilentlyContinue
$cmake=if($cmakeCommand){$cmakeCommand.Source}else{Join-Path $ToolRoot 'cmake/cmake-3.31.8-windows-x86_64/bin/cmake.exe'}
if(-not (Get-Command cl -ErrorAction SilentlyContinue)) {
 $vc=Get-ChildItem (Join-Path $ToolRoot 'msvc/VC/Tools/MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
 $sdkRoot=Join-Path $ToolRoot 'msvc/Windows Kits/10'
 $sdk=Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1
 $env:Path="$($vc.FullName)/bin/Hostx64/x64;$sdkRoot/bin/$($sdk.Name)/x64;$ToolRoot/ninja;$env:Path"
 $env:INCLUDE="$($vc.FullName)/include;$($sdk.FullName)/ucrt;$($sdk.FullName)/shared;$($sdk.FullName)/um;$($sdk.FullName)/winrt;$($sdk.FullName)/cppwinrt"
 $env:LIB="$($vc.FullName)/lib/x64;$sdkRoot/Lib/$($sdk.Name)/ucrt/x64;$sdkRoot/Lib/$($sdk.Name)/um/x64"
}
& $cmake -S $projectRoot -B $buildRoot -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl
if($LASTEXITCODE){throw 'CMake configure failed'}
& $cmake --build $buildRoot --parallel 4
if($LASTEXITCODE){throw 'Native build failed'}
if(-not $SkipTests){& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir $buildRoot --output-on-failure;if($LASTEXITCODE){throw 'Native tests failed'}}
if($NativeOnly){return}
$dotnetCommand=Get-Command dotnet -ErrorAction SilentlyContinue
$dotnet=if($dotnetCommand){$dotnetCommand.Source}else{Join-Path $ToolRoot 'dotnet/dotnet.exe'}
$env:DOTNET_CLI_HOME=Join-Path $projectRoot '.dotnet-home'
$env:NUGET_PACKAGES=Join-Path $projectRoot '.nuget/packages'
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1'
$env:DOTNET_CLI_TELEMETRY_OPTOUT='1'
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE='false'
$selfContained=if($FrameworkDependent){'false'}else{'true'}
& $dotnet publish (Join-Path $projectRoot 'src/iOSAudioBridge.App/iOSAudioBridge.App.csproj') -c Release -r win-x64 --self-contained $selfContained -o (Join-Path $projectRoot 'dist/iOSAudioBridge')
if($LASTEXITCODE){throw 'WPF publish failed'}
Copy-Item (Join-Path $buildRoot 'bin/AudioCore.dll'),(Join-Path $buildRoot 'bin/AudioBridge.UsbRestore.exe'),(Join-Path $buildRoot 'bin/AudioProbe.exe'),(Join-Path $buildRoot 'bin/libusb0.dll') (Join-Path $projectRoot 'dist/iOSAudioBridge') -Force
Copy-Item (Join-Path $projectRoot 'LICENSE'),(Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md') (Join-Path $projectRoot 'dist/iOSAudioBridge') -Force
Copy-Item (Join-Path $projectRoot 'docs/QUICKSTART.zh-CN.md') (Join-Path $projectRoot 'dist/iOSAudioBridge/使用说明.md') -Force
$licenseRoot=Join-Path $projectRoot 'dist/iOSAudioBridge/licenses'
New-Item -ItemType Directory -Path $licenseRoot -Force | Out-Null
Copy-Item (Join-Path $projectRoot 'third_party/libusb-win32/*.txt'),(Join-Path $projectRoot 'third_party/libusb-win32/*src*.zip') $licenseRoot -Force
if(-not $FrameworkDependent) {
 $runtimeInfo=Get-Content (Join-Path $projectRoot 'dist/iOSAudioBridge/iOSAudioBridge.runtimeconfig.json') -Raw | ConvertFrom-Json
 foreach($framework in $runtimeInfo.runtimeOptions.includedFrameworks) {
  $package=Join-Path $env:NUGET_PACKAGES "$($framework.name.ToLowerInvariant()).runtime.win-x64/$($framework.version)"
  $target=Join-Path $licenseRoot $framework.name
  New-Item -ItemType Directory -Path $target -Force | Out-Null
  Get-ChildItem $package -File | Where-Object { $_.Name -match '^(LICENSE|THIRD-PARTY-NOTICES)' } | Copy-Item -Destination $target -Force
 }
}
Write-Output "Published to $projectRoot/dist/iOSAudioBridge"
