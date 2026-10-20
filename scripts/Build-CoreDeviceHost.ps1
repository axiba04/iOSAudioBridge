param(
    [string]$MsysRoot = "$PSScriptRoot/../.tools/msys64",
    [string]$DdiRoot
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$bash=(Resolve-Path (Join-Path $MsysRoot 'usr/bin/bash.exe')).Path
$oldSystem=$env:MSYSTEM; $oldInvoke=$env:CHERE_INVOKING
Push-Location $root
try {
    $env:MSYSTEM='UCRT64';$env:CHERE_INVOKING='1'
    & $bash -lc 'bash scripts/Build-CoreDeviceHost.sh'
    if($LASTEXITCODE -ne 0){throw 'CoreDevice host build failed'}
    if($DdiRoot){
        if(!(Test-Path (Join-Path $DdiRoot 'Restore/BuildManifest.plist'))){throw 'DdiRoot must contain Restore/BuildManifest.plist'}
        New-Item -ItemType Directory -Force "$root/build/coredevice-runtime/iOS_DDI" | Out-Null
        Copy-Item -LiteralPath (Join-Path $DdiRoot 'Restore') -Destination "$root/build/coredevice-runtime/iOS_DDI" -Recurse -Force
    }
} finally {Pop-Location;$env:MSYSTEM=$oldSystem;$env:CHERE_INVOKING=$oldInvoke}
