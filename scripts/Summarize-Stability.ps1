param([Parameter(Mandatory)][string]$ResultDirectory)
$ErrorActionPreference = 'Stop'
$manifest = Get-Content -LiteralPath (Join-Path $ResultDirectory 'manifest.json') -Raw | ConvertFrom-Json
$rows = @(foreach ($line in Get-Content -LiteralPath (Join-Path $ResultDirectory 'capture.txt')) {
    if ($line -match '^t=(\d+) .*\| audio=(\d+) video=(\d+) queue=(\d+) underruns=(\d+) dropped=(\d+) state=(\d+) output_active=(\d+) source_idle=(\d+)$') {
        [pscustomobject]@{t=[int]$Matches[1]; audio=[long]$Matches[2]; video=[long]$Matches[3]; queue=[int]$Matches[4]; underruns=[long]$Matches[5]; dropped=[long]$Matches[6]; state=[int]$Matches[7]; active=[int]$Matches[8]; idle=[int]$Matches[9]}
    }
})
if (-not $rows.Count) { throw 'No status samples found' }
$resources = @(Import-Csv -LiteralPath (Join-Path $ResultDirectory 'resources.csv'))
$warm = @($resources | Where-Object { [double]$_.elapsed_s -ge 60 -and [double]$_.elapsed_s -le $manifest.seconds })
$log = Get-Content -LiteralPath (Join-Path $ResultDirectory 'capture.log') -Raw
$completionPath = Join-Path $ResultDirectory 'completion.json'
$completion = if (Test-Path -LiteralPath $completionPath) { Get-Content -LiteralPath $completionPath -Raw | ConvertFrom-Json } else { $null }
$stalls = 0
for ($i=1; $i -lt $rows.Count; $i++) {
    if ($rows[$i-1].audio -gt 0 -and $rows[$i].audio -eq $rows[$i-1].audio) { $stalls++ }
}
$afterAudio = @($rows | Where-Object { $_.audio -gt 0 })
$summary = [ordered]@{
    requested_seconds = $manifest.seconds
    status_samples = $rows.Count
    last_sample_second = $rows[-1].t
    completed = [bool]$completion
    exit_code = if ($completion) { $completion.exit_code } else { $null }
    last_audio_packets = $rows[-1].audio
    max_video_packets = ($rows | Measure-Object video -Maximum).Maximum
    max_underruns = ($rows | Measure-Object underruns -Maximum).Maximum
    first_underrun_sample = ($rows | Where-Object underruns -gt 0 | Select-Object -First 1).t
    max_dropped_frames = ($rows | Measure-Object dropped -Maximum).Maximum
    queue_min_after_audio = ($afterAudio | Measure-Object queue -Minimum).Minimum
    queue_max_after_audio = ($afterAudio | Measure-Object queue -Maximum).Maximum
    unchanged_audio_intervals = $stalls
    idle_samples_after_audio = @($afterAudio | Where-Object idle -eq 1).Count
    non_streaming_samples_after_audio = @($afterAudio | Where-Object state -ne 3).Count
    inactive_output_samples_after_audio = @($afterAudio | Where-Object active -ne 1).Count
    session_errors = ([regex]::Matches($log,'audio_session_error')).Count
    usb_open_count = ([regex]::Matches($log,'libusb0_quicktime_open claim=success')).Count
    usb_activate_count = ([regex]::Matches($log,'operation=activate exit_code=0')).Count
    pauses = ([regex]::Matches($log,'audio_source_paused')).Count
    resumes = ([regex]::Matches($log,'audio_source_resumed')).Count
    restore_confirmed = $log.Contains('result=normal disable_requested=')
    cleanup_failure = $log -match 'audio_stop_timing[^\r\n]*failure=true'
    private_mb_after_warmup_min = if ($warm.Count) { [math]::Round((($warm | Measure-Object private_bytes -Minimum).Minimum / 1MB),3) } else { $null }
    private_mb_after_warmup_max = if ($warm.Count) { [math]::Round((($warm | Measure-Object private_bytes -Maximum).Maximum / 1MB),3) } else { $null }
    warmup_first_handles = if ($warm.Count) { [int]$warm[0].handles } else { $null }
    last_handles = [int]$resources[-1].handles
    cpu_seconds = [double]$resources[-1].cpu_s
    cpu_percent_one_core = [math]::Round(100 * [double]$resources[-1].cpu_s / [double]$resources[-1].elapsed_s,3)
}
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ResultDirectory 'summary.json') -Encoding UTF8
$summary | ConvertTo-Json
