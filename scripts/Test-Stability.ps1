param(
    [ValidateRange(1,7200)][int]$Seconds = 900,
    [int]$DeviceIndex = 0,
    [int]$OutputIndex = 2
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$runtime = Join-Path $root 'build/zl-bridge/bin'
$files = 'AudioCore.dll','AudioProbe.exe','AudioBridge.UsbRestore.exe','libusb0.dll'
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath (Join-Path $runtime $file))) {
        throw "Missing $file. Run scripts/Build-ZLBridge.ps1 first."
    }
}
$result = Join-Path $root ('docs/test-results/' + (Get-Date -Format 'yyyy-MM-dd-HHmmss') + '-stability')
New-Item -ItemType Directory -Path $result | Out-Null
$manifest = foreach ($file in $files) {
    $path = Join-Path $runtime $file
    [ordered]@{file=$file; bytes=(Get-Item -LiteralPath $path).Length; sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
}
[ordered]@{started=(Get-Date -Format o); seconds=$Seconds; device_index=$DeviceIndex; output_index=$OutputIndex; muted=$true; files=@($manifest)} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $result 'manifest.json') -Encoding UTF8
& (Join-Path $runtime 'AudioProbe.exe') --devices | Set-Content -LiteralPath (Join-Path $result 'devices-before.txt') -Encoding UTF8
& (Join-Path $runtime 'AudioProbe.exe') --outputs | Set-Content -LiteralPath (Join-Path $result 'outputs.txt') -Encoding UTF8
$previousLog = $env:IPHONE_MIRROR_LOG_FILE
try {
    $env:IPHONE_MIRROR_LOG_FILE = Join-Path $result 'capture.log'
    $probe = Start-Process -FilePath (Join-Path $runtime 'AudioProbe.exe') -ArgumentList @('--capture-muted',$Seconds,$DeviceIndex,$OutputIndex) -WorkingDirectory $runtime -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $result 'capture.txt') -RedirectStandardError (Join-Path $result 'stderr.txt')
    $timer = [Diagnostics.Stopwatch]::StartNew()
    Write-Output "result_directory=$result probe_pid=$($probe.Id)"
    while (-not $probe.HasExited) {
        $probe.Refresh()
        if ($probe.HasExited) { break }
        [pscustomobject]@{elapsed_s=[math]::Round($timer.Elapsed.TotalSeconds,2); private_bytes=$probe.PrivateMemorySize64; working_set_bytes=$probe.WorkingSet64; handles=$probe.HandleCount; threads=$probe.Threads.Count; cpu_s=$probe.TotalProcessorTime.TotalSeconds} |
            Export-Csv -LiteralPath (Join-Path $result 'resources.csv') -NoTypeInformation -Append -Encoding UTF8
        Start-Sleep -Seconds 5
    }
    $probe.WaitForExit()
    $exitCode = $probe.ExitCode
    & (Join-Path $runtime 'AudioProbe.exe') --devices | Set-Content -LiteralPath (Join-Path $result 'devices-after.txt') -Encoding UTF8
    [ordered]@{finished=(Get-Date -Format o); elapsed_s=$timer.Elapsed.TotalSeconds; exit_code=$exitCode} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $result 'completion.json') -Encoding UTF8
    Get-Content -LiteralPath (Join-Path $result 'capture.txt') -Tail 2
    Write-Output "exit_code=$exitCode result_directory=$result"
    exit $exitCode
} finally {
    $env:IPHONE_MIRROR_LOG_FILE = $previousLog
}
