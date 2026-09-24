$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. (Join-Path $root 'DriverInstallKit/DriverKit.Common.ps1')
$scratch = Join-Path $root ('build/driver-kit-tests/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $scratch | Out-Null
$passed = [Collections.Generic.List[string]]::new()
function Expect-Rejection([string]$Name, [scriptblock]$Action, [string]$Message) {
    try { & $Action } catch {
        if ($_.Exception.Message -notlike $Message) { throw }
        $passed.Add($Name)
        return
    }
    throw "Expected rejection did not occur: $Name"
}

Expect-Rejection 'no phone' { Select-KitPhone @() } '*at least one iPhone/iPad*'
$instance = 'USB\VID_05AC&PID_12A8\SIMULATED-PHONE'
$phone = [pscustomobject]@{instance=$instance;service='usbccgp';lower=@('AppleLowerFilter');problem=0}
Expect-Rejection 'ambiguous phone selection' { Select-KitPhone @($phone,$phone) } '*Multiple iPhone/iPad*'
Expect-Rejection 'wrong parent binding' { Select-KitPhone @([pscustomobject]@{instance=$instance;service='libusb0';lower=@('AppleLowerFilter');problem=0}) } '*composite parent is not ready*'
Expect-Rejection 'unhealthy phone' { Select-KitPhone @([pscustomobject]@{instance=$instance;service='usbccgp';lower=@('AppleLowerFilter');problem=22}) } '*composite parent is not ready*'
$null = Select-KitPhone @($phone)
$passed.Add('healthy Apple parent accepted')
& {
    function Get-PnpDeviceProperty {
        [CmdletBinding()]param([string]$InstanceId)
        [pscustomobject]@{KeyName='DEVPKEY_Device_Service';Data='usbccgp'}
    }
    if (@(Get-DeviceValue 'test-phone' 'DEVPKEY_Device_UpperFilters').Count -ne 0) { throw 'Missing filter property was not treated as empty.' }
}
$passed.Add('fresh phone without an UpperFilters property')

$testFile = Join-Path $scratch 'old-driver.sys'
Set-Content -LiteralPath $testFile -Value 'existing driver must not be overwritten'
$before = (Get-FileHash -LiteralPath $testFile).Hash
Expect-Rejection 'existing incompatible file' { Assert-CompatibleFile $testFile ('0' * 64) } '*will not overwrite*'
if ((Get-FileHash -LiteralPath $testFile).Hash -ne $before) { throw 'Guard modified an existing file.' }
Assert-CompatibleFile $testFile $before
$passed.Add('existing matching file accepted without changes')
Assert-CompatibleFile (Join-Path $scratch 'not-installed.sys') $before
$passed.Add('missing file accepted for fresh install')

@{schema=1;libusb_version='1.4.0.2';files=@(@{path='old-driver.sys';sha256=('0' * 64)})} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $scratch 'payload.json')
Expect-Rejection 'corrupt payload before signatures or system access' { Assert-KitPayload $scratch } '*Payload hash mismatch*'
@{schema=1;libusb_version='1.4.0.2';files=@(@{path='../outside.sys';sha256=('0' * 64)})} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $scratch 'payload.json')
Expect-Rejection 'payload path traversal' { Assert-KitPayload $scratch } '*escapes the kit*'

foreach ($file in @(Get-ChildItem -LiteralPath (Join-Path $root 'DriverInstallKit') -Filter '*.ps1') + @(Get-Item -LiteralPath (Join-Path $root 'scripts/Build-DriverInstallKit.ps1'))) {
    $tokens = $null; $parseErrors = $null
    $null = [System.Management.Automation.Language.Parser]::ParseFile($file.FullName, [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count) { throw "Parser errors: $($file.Name): $parseErrors" }
}
$passed.Add('installer and packager script parsing')
$report = @{powershell=$PSVersionTable.PSVersion.ToString();passed=@($passed);system_modified=$false;scope='Input guards only; does not exercise MSI installation, catalog registration, kernel loading or filter changes.'}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $scratch 'results.json') -Encoding UTF8
$report | ConvertTo-Json -Depth 4
