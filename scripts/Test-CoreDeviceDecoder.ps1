param([string]$MsysRoot = "$PSScriptRoot/../.tools/msys64")
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$root=Split-Path $PSScriptRoot -Parent
$tools=(Resolve-Path "$MsysRoot/ucrt64/bin").Path
$bash=(Resolve-Path "$MsysRoot/usr/bin/bash.exe").Path
$work=Join-Path $root 'build/coredevice'
New-Item -ItemType Directory -Force $work | Out-Null
$sample=Join-Path $work 'eld-480.mp4'
if(!(Test-Path $sample)){
    # FFmpeg FATE conformance fixture, ASC f8 e6 50 00, matching the phone's offer.
    Invoke-WebRequest -UseBasicParsing 'https://fate-suite.ffmpeg.org/aac/er_eld2100np_48_ep0.mp4' -OutFile $sample
}
$packets=(& "$tools/ffprobe.exe" -v error -select_streams a:0 -show_packets -read_intervals '%+0.5' -of json $sample | ConvertFrom-Json).packets
if($LASTEXITCODE -ne 0){throw 'Cannot read AAC-ELD fixture'}
$inputFile=[IO.File]::OpenRead($sample)
$outputFile=[IO.File]::Create((Join-Path $work 'eld.records'))
try {
    foreach($packet in ($packets | Select-Object -First 30)){
        $size=[int]$packet.size
        $payload=New-Object byte[] $size
        $inputFile.Position=[long]$packet.pos
        if($inputFile.Read($payload,0,$size) -ne $size){throw 'Truncated fixture'}
        $outputFile.Write([byte[]]@(($size -shr 8),($size -band 255),0,0,0,0),0,6)
        $outputFile.Write($payload,0,$size)
    }
} finally {$inputFile.Dispose();$outputFile.Dispose()}
$oldSystem=$env:MSYSTEM;$oldInvoke=$env:CHERE_INVOKING
Push-Location $root
try {
    $env:MSYSTEM='UCRT64';$env:CHERE_INVOKING='1'
    & $bash -lc 'gcc -std=c11 -O2 -Wall -Wextra -Isrc/AudioBridge.CoreDeviceHost tests/CoreDeviceDecoderTests.c src/AudioBridge.CoreDeviceHost/Decoder.c $(pkg-config --cflags --libs libavcodec libavutil) -o build/coredevice/decoder-tests.exe && build/coredevice/decoder-tests.exe build/coredevice/eld.records'
    if($LASTEXITCODE -ne 0){throw 'CoreDevice decoder regression failed'}
} finally {Pop-Location;$env:MSYSTEM=$oldSystem;$env:CHERE_INVOKING=$oldInvoke}
