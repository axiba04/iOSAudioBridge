param([switch]$SkipLLVM)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vendor=Join-Path $root 'third_party/ZLCompressor'
$commit='0e19ffcf3dcf7304ca2f650cd28267959bd40a21'
if(-not(Test-Path -LiteralPath $vendor)) {
    & git clone --filter=blob:none --no-checkout https://github.com/ZL-Audio/ZLCompressor.git $vendor
    if($LASTEXITCODE){throw 'ZL clone failed'}
    & git -C $vendor checkout --detach $commit
    if($LASTEXITCODE){throw 'ZL checkout failed'}
}
$actual=& git -C $vendor rev-parse HEAD
if($actual -ne $commit){throw "Vendor HEAD differs from pinned commit: $actual"}
& git -C $vendor submodule update --init --recursive --depth 1
if($LASTEXITCODE){throw 'Pinned submodule setup failed'}
$patch=Join-Path $root 'third_party/ZLCompressor.bridge.patch'
& git -C $vendor apply --reverse --check $patch 2>$null
if($LASTEXITCODE){
    & git -C $vendor apply --check $patch
    if($LASTEXITCODE){throw 'Vendor patch conflicts with local changes; no files were overwritten'}
    & git -C $vendor apply $patch
    if($LASTEXITCODE){throw 'Applying bridge patch failed'}
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
Write-Output 'Pinned ZL dependencies are ready. No system installer was executed.'
