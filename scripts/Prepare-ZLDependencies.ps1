param([switch]$SkipLLVM)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vendor=Join-Path $root 'third_party/ZLCompressor'
$airplay=Join-Path $root 'third_party/iPhoneMirror-AirPlay'
$airplayPins=Get-Content -LiteralPath "$airplay/pins.json" -Raw | ConvertFrom-Json
foreach($entry in $airplayPins.files) {
    $file=Join-Path $airplay $entry.path
    if(-not(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw "AirPlay dependency missing or changed: $($entry.path)"
    }
}
# Patched source snapshots are tracked by the parent repository; nested Git
# metadata is neither present nor needed in a fresh checkout.
foreach($file in 'CMakeLists.txt','source/PluginProcessor.cpp','JUCE/CMakeLists.txt','highway/CMakeLists.txt','zldsp_fft/src/zldsp_fft_rfft.hpp') {
    if(-not(Test-Path -LiteralPath (Join-Path $vendor $file) -PathType Leaf)) {
        throw "Vendored source missing: $file. Restore third_party/ZLCompressor from this repository."
    }
}
if(-not $SkipLLVM) {
    $llvm=Join-Path $root '.tools/llvm-22.1.0'
    if(-not(Test-Path "$llvm/bin/clang-cl.exe")) {
        $archive=Join-Path $root '.tools/LLVM-22.1.0-win64.exe'
        New-Item -ItemType Directory -Force (Split-Path $archive) | Out-Null
        $sha='B31D5F54942E017CB878E594529723DD629CC7B54C9BF7A331E2DC01E8EA5E75'
        if(-not(Test-Path $archive)) {
            & curl.exe -L --fail --retry 2 --output $archive 'https://github.com/llvm/llvm-project/releases/download/llvmorg-22.1.0/LLVM-22.1.0-win64.exe'
            if($LASTEXITCODE){throw 'LLVM download failed'}
        }
        if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $sha){throw 'LLVM archive checksum mismatch'}
        $sevenZip=(Get-Command 7z.exe -ErrorAction Stop).Source
        & $sevenZip x $archive "-o$llvm" -y
        if($LASTEXITCODE){throw 'Portable LLVM extraction failed'}
    }
    & "$llvm/bin/clang-cl.exe" --version
    if($LASTEXITCODE){throw 'LLVM executable failed'}
}
Write-Output 'Vendored ZL sources are present. No system installer was executed.'
