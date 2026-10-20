$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

# Same public DDI mirror used by rPlayHub. Everything stays beside the executables.
$destination = Join-Path $PSScriptRoot 'iOS_DDI/Restore'
$base = 'https://github.com/doronz88/DeveloperDiskImage/raw/main/PersonalizedImages/Xcode_iOS_DDI_Personalized'
try {
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    foreach ($name in @('BuildManifest.plist', 'Image.dmg', 'Image.dmg.trustcache')) {
        $target = Join-Path $destination $name
        Write-Host "Downloading $name ..."
        Invoke-WebRequest -UseBasicParsing -Uri "$base/$name" -OutFile "$target.part"
        Move-Item -LiteralPath "$target.part" -Destination $target -Force
    }
    Write-Host "DDI ready: $destination"
    Write-Host 'Next: select CoreDevice USB in iOSAudioBridge and start listening.'
} catch {
    Write-Host "DDI download failed: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host 'Check your internet connection, then run download-ddi.cmd again.'
    exit 1
}
