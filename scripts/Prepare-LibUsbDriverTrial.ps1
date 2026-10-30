param(
    [string]$SignToolPath,
    [switch]$Offline
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$version = '1.4.0.2'
$name = "libusb-win32-bin-$version.zip"
$sha256 = '00004c92cdb99be36e17fb2377165eb97e63b48ba895bfc04a642ea9c3e26d94'
$url = "https://github.com/mcuee/libusb-win32/releases/download/release_$version/$name"
$directory = Join-Path $root "build/driver-evaluation/libusb-win32-$version"
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$archive = Join-Path $directory $name
if (-not (Test-Path -LiteralPath $archive)) {
    if ($Offline) { throw "Missing offline archive: $archive" }
    Invoke-WebRequest -Uri $url -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $sha256) { throw 'Official archive SHA-256 mismatch.' }
Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $directory 'unpacked') -Force
$package = Join-Path $directory "unpacked/libusb-win32-bin-$version"
if (-not $SignToolPath) { $SignToolPath = Join-Path $root '.tools/sdk-signature-check/signtool.exe' }
if (-not (Test-Path -LiteralPath $SignToolPath)) { throw 'Supply -SignToolPath from the Microsoft Windows SDK; kernel-policy verification is required.' }
$signToolSignature = Get-AuthenticodeSignature -LiteralPath $SignToolPath
if ($signToolSignature.Status -ne 'Valid' -or $signToolSignature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') { throw 'SignTool must have a valid Microsoft signature.' }

$files = @(foreach ($relative in @('bin/amd64/libusb0.sys','bin/amd64/libusb0.dll','bin/x86/libusb0_x86.dll','bin/amd64/install-filter.exe','bin/libusb0.cat','bin/libusb0.inf')) {
    $path = Join-Path $package $relative
    $signature = Get-AuthenticodeSignature -LiteralPath $path
    if ($relative -notlike '*.inf' -and $signature.Status -ne 'Valid') { throw "Invalid signature: $relative ($($signature.Status))" }
    [pscustomobject]@{ path = $relative; sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash; version = (Get-Item -LiteralPath $path).VersionInfo.FileVersion; signature = [string]$signature.Status; signer = $signature.SignerCertificate.Subject }
})
& $SignToolPath verify /kp /v "$package/bin/amd64/libusb0.sys" 2>&1 | Set-Content "$directory/kernel-embedded-signature.txt"
$embeddedExit = $LASTEXITCODE
& $SignToolPath verify /kp /v /c "$package/bin/libusb0.cat" "$package/bin/amd64/libusb0.sys" 2>&1 | Set-Content "$directory/kernel-catalog-signature.txt"
if ($LASTEXITCODE) { throw 'Kernel driver failed verification with its Microsoft catalog.' }
& $SignToolPath verify /pa /v /c "$package/bin/libusb0.cat" "$package/bin/libusb0.inf" 2>&1 | Set-Content "$directory/inf-catalog-signature.txt"
if ($LASTEXITCODE) { throw 'INF does not match the signed catalog.' }

$readobj = Join-Path $root '.tools/llvm-22.1.0/bin/llvm-readobj.exe'
function Get-Exports([string]$Path) {
    $output = & $readobj --coff-exports $Path
    if ($LASTEXITCODE) { throw "PE export inspection failed: $Path" }
    @($output | Select-String '^  Name: ' | ForEach-Object { $_.Line.Substring(8) })
}
$oldExports = @(Get-Exports (Join-Path $root 'third_party/libusb-win32/bin/x64/libusb0.dll'))
$newExports = @(Get-Exports "$package/bin/amd64/libusb0.dll")
$missing = @($oldExports | Where-Object { $_ -notin $newExports })
if ($missing.Count -or -not $newExports.Count) { throw "Missing exports: $($missing -join ', ')" }

# A separate application copy avoids changing the daily-use package during evaluation.
$runtime = Join-Path $directory 'runtime'
New-Item -ItemType Directory -Force -Path $runtime | Out-Null
Get-ChildItem -LiteralPath (Join-Path $root 'dist/AudioBridge') | Copy-Item -Destination $runtime -Recurse -Force
foreach ($file in @('AudioCore.dll','AudioProbe.exe')) {
    Copy-Item -LiteralPath (Join-Path $root "build/zl-bridge/bin/$file") -Destination $runtime -Force
}
Copy-Item -LiteralPath "$package/bin/amd64/libusb0.dll" -Destination "$runtime/libusb0.dll" -Force
$licenses = Join-Path $runtime 'licenses/libusb-win32-trial'
New-Item -ItemType Directory -Force -Path $licenses | Out-Null
foreach ($file in @('COPYING_LGPL.txt','COPYING_GPL.txt','AUTHORS.txt','CHANGELOG.txt')) {
    Copy-Item -LiteralPath (Join-Path $package $file) -Destination $licenses -Force
}
$report = [ordered]@{
    prepared_at = (Get-Date -Format o); version = $version; archive_url = $url; archive_sha256 = $sha256
    files = $files; embedded_kernel_policy_exit = $embeddedExit; catalog_kernel_policy_verified = $true
    catalog_registration_required = ($embeddedExit -ne 0); old_exports = $oldExports; new_exports = $newExports
    runtime = $runtime; state = 'Prepared only; no system driver or device settings changed.'
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$directory/preparation.json" -Encoding utf8
Write-Output "Prepared libusb ${version}: $directory"
Write-Output "Exports preserved: $($oldExports.Count). Kernel catalog verified; system registration and live testing still required."
