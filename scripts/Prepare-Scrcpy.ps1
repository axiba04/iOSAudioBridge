param([string]$Archive)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$pins=Get-Content -LiteralPath "$root/third_party/scrcpy.pins.json" -Raw | ConvertFrom-Json
$cache=Join-Path $root '.tools/scrcpy-download'
New-Item -ItemType Directory -Force -Path $cache | Out-Null
if(-not $Archive){
    $Archive=Join-Path $cache "scrcpy-win64-v$($pins.version).zip"
    if(-not(Test-Path -LiteralPath $Archive)){
        & curl.exe -fL --retry 2 -o $Archive $pins.windows_url
        if($LASTEXITCODE){throw 'scrcpy download failed'}
    }
}
if((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash -ne $pins.windows_sha256){throw 'scrcpy archive SHA256 mismatch'}
Expand-Archive -LiteralPath $Archive -DestinationPath $cache -Force
$source=Join-Path $cache "scrcpy-win64-v$($pins.version)"
if((Get-FileHash -LiteralPath "$source/scrcpy-server" -Algorithm SHA256).Hash -ne $pins.server_sha256){throw 'scrcpy-server SHA256 mismatch'}
$runtime=Join-Path $root '.tools/scrcpy/Android'
New-Item -ItemType Directory -Force -Path $runtime | Out-Null
foreach($file in 'adb.exe','AdbWinApi.dll','AdbWinUsbApi.dll','scrcpy-server','LICENSE.txt'){
    $from=Join-Path $source $file
    $to=Join-Path $runtime $file
    # The shared adb daemon can be running from here. Leave identical files alone.
    if((Test-Path -LiteralPath $to) -and (Get-FileHash -LiteralPath $from).Hash -eq (Get-FileHash -LiteralPath $to).Hash){continue}
    Copy-Item -LiteralPath $from -Destination $to -Force
}
Copy-Item -LiteralPath "$root/third_party/scrcpy.pins.json" -Destination $runtime -Force
Write-Output "Prepared scrcpy $($pins.version) audio runtime: $runtime"
