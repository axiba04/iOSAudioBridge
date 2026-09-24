# Exercise real backup/copy/recovery code using temporary files. All device,
# service, registry, catalog and native-process calls are replaced below.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. (Join-Path $root 'DriverInstallKit/DriverKit.Common.ps1')
. (Join-Path $root 'DriverInstallKit/DriverKit.Upgrade.ps1')
$scratch = Join-Path $root ('build/driver-upgrade-tests/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $scratch | Out-Null
$passed = [Collections.Generic.List[string]]::new()

function Get-UpgradeBackupRoot { Join-Path $script:caseRoot 'backups' }
function Get-UpgradeMachineId { 'simulated-machine' }
function Initialize-UpgradeBackupRoot { New-Item -ItemType Directory -Force -Path (Get-UpgradeBackupRoot) | Out-Null }
function Export-UpgradeService([string]$Directory) { 'simulated service evidence' | Set-Content -LiteralPath (Join-Path $Directory 'libusb0-service.reg') }
function Get-UpgradeLegacyHashes { $script:oldHashes }
function Get-KitDeployments($Manifest, [string]$Directory) {
    foreach ($name in $script:names) { [pscustomobject]@{source=(Join-Path $script:caseRoot "candidate/$name");destination=(Join-Path $script:caseRoot "system/$name");hash=$script:newHashes[$name]} }
}
function Assert-UpgradeIdle { if ($script:busy) { throw 'simulated active capture' } }
function Assert-KitFilterScope([string]$InstanceId) { if ($script:otherFilter) { throw 'simulated other filter' } }
function Get-KitFilteredNodes { $script:devices }
function Get-KitRegisteredPhones { $script:devices }
function Get-DeviceValue([string]$InstanceId, [string]$Key) {
    $device = @($script:devices | Where-Object instance -eq $InstanceId)
    if ($device.Count -ne 1) { throw 'Unexpected simulated device' }
    $device = $device[0]
    switch ($Key) {
        'DEVPKEY_Device_Service' { if ($script:bindingChanged) { 'wrong-service' } else { $device.service } }
        'DEVPKEY_Device_DriverVersion' { $device.version }
        'DEVPKEY_Device_ProblemCode' { $device.problem }
        'DEVPKEY_Device_UpperFilters' { $device.upper }
        'DEVPKEY_Device_LowerFilters' { $device.lower }
        default { throw 'Unexpected simulated property' }
    }
}
function Get-PnpDeviceProperty {
    [CmdletBinding()]param([string]$InstanceId)
    foreach ($key in 'Service','DriverVersion','UpperFilters','LowerFilters') {
        [pscustomobject]@{KeyName=('DEVPKEY_Device_' + $key);Data=(Get-DeviceValue $InstanceId ('DEVPKEY_Device_' + $key))}
    }
}
function Get-PnpDevice {
    param([switch]$PresentOnly)
    if (-not $PresentOnly) { throw 'Unexpected simulated PnP enumeration' }
    if ($script:connected) { [pscustomobject]@{InstanceId=$script:phone.instance} }
    if ($script:secondConnected) { [pscustomobject]@{InstanceId='USB\VID_05AC&PID_12AB\SIMULATED-IPAD'} }
}
function Get-UpgradeDriverState { $script:driverState }
function Invoke-KitNative([string]$File, [string[]]$Arguments) {
    if ([IO.Path]::GetFileName($File) -ne 'sc.exe' -or $Arguments[1] -ne 'libusb0') { throw 'Unmocked native operation forbidden' }
    $script:events.Add($Arguments[0])
    if ($Arguments[0] -eq 'stop') {
        if ($script:stopFails) { throw 'simulated unload refusal' }
        $script:driverState = 'Stopped'
    } elseif ($Arguments[0] -eq 'start') {
        $hash = (Get-FileHash -LiteralPath (Join-Path $script:caseRoot 'system/libusb0.sys')).Hash
        if ($script:allStartsFail -or ($script:newStartFails -and $hash -eq $script:newHashes['libusb0.sys'])) { throw 'simulated kernel signing/load refusal' }
        $script:driverState = 'Running'
    } else { throw 'Unmocked service action forbidden' }
}
function Register-UpgradeCatalog([string]$Directory) {
    $script:events.Add('register')
    if ($script:catalogFails) { throw 'simulated catalog registration failure' }
    $script:catalog = $true
}
function Remove-UpgradeCatalog([string]$Directory) { $script:events.Add('remove'); $script:catalog = $false }
function Copy-Item {
    [CmdletBinding()]param([string]$LiteralPath,[string]$Destination,[switch]$Force)
    foreach ($path in @($LiteralPath,$Destination)) {
        if (-not [IO.Path]::GetFullPath($path).StartsWith($scratch + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Test copy escaped temporary workspace' }
    }
    if ($script:driverState -ne 'Stopped' -or $script:connected) { throw 'Test attempted a write while the driver/device was active' }
    $script:copyCount++
    $script:events.Add('copy')
    if ($script:copyFailsAt -eq $script:copyCount) {
        'partial write' | Set-Content -LiteralPath $Destination
        throw 'simulated partial copy failure'
    }
    Microsoft.PowerShell.Management\Copy-Item @PSBoundParameters
    if ($script:connectAfterCopy -eq $script:copyCount) { $script:connected = $true }
}
function Assert-Test([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Expect-Failure([scriptblock]$Action, [string]$Message) {
    try { & $Action | Out-Null } catch { if ($_.Exception.Message -like $Message) { return }; throw }
    throw "Expected failure: $Message"
}
function New-Case([string]$Name, [switch]$Multiple) {
    $script:caseRoot = Join-Path $scratch $Name
    New-Item -ItemType Directory -Force -Path (Join-Path $script:caseRoot 'system'),(Join-Path $script:caseRoot 'candidate') | Out-Null
    $script:names = @('libusb0.sys','libusb0-x64.dll','libusb0-x86.dll')
    $script:oldHashes = @{}; $script:newHashes = @{}
    foreach ($name in $script:names) {
        "legacy-$name" | Set-Content -LiteralPath (Join-Path $script:caseRoot "system/$name")
        "candidate-$name" | Set-Content -LiteralPath (Join-Path $script:caseRoot "candidate/$name")
        $script:oldHashes[$name] = (Get-FileHash -LiteralPath (Join-Path $script:caseRoot "system/$name")).Hash
        $script:newHashes[$name] = (Get-FileHash -LiteralPath (Join-Path $script:caseRoot "candidate/$name")).Hash
    }
    $script:phone = [pscustomobject]@{instance='USB\VID_05AC&PID_12A8\SIMULATED-PHONE';service='usbccgp';version='538.0.0.0';upper=@('libusb0');lower=@('AppleLowerFilter');problem=0;name='Simulated iPhone'}
    $script:devices = @($script:phone)
    if ($Multiple) { $script:devices += [pscustomobject]@{instance='USB\VID_05AC&PID_12AB\SIMULATED-IPAD';service='usbccgp';version='538.0.0.0';upper=@('libusb0');lower=@('AppleLowerFilter');problem=45;name='Simulated disconnected iPad'} }
    $script:secondConnected = $false
    $script:driverState = 'Running'; $script:connected = $false; $script:busy = $false; $script:otherFilter = $false; $script:bindingChanged = $false
    $script:stopFails = $false; $script:newStartFails = $false; $script:allStartsFail = $false; $script:catalogFails = $false; $script:catalog = $false
    $script:copyCount = 0; $script:copyFailsAt = 0; $script:connectAfterCopy = 0
    $script:events = [Collections.Generic.List[string]]::new()
    $script:files = @(Get-UpgradeFiles $null $script:caseRoot)
    $script:backupDirectory = Save-UpgradeBackup $script:devices $script:files
}
function Run-Case([string]$Mode = 'Upgrade') { Invoke-UpgradeTransaction $Mode $script:backupDirectory $null $script:caseRoot }

New-Case 'success'
$result = Run-Case
Assert-Test ($result.success -and (Get-UpgradeFileState $files) -eq 'Current' -and $driverState -eq 'Running' -and $catalog) 'Upgrade success state incorrect'
$result = Run-Case 'Restore'
Assert-Test ($result.success -and $result.original_restored -and (Get-UpgradeFileState $files) -eq 'Legacy' -and -not $catalog) 'Explicit restore failed'
$passed.Add('backup, upgrade, and explicit restore')

New-Case 'new-load-failure'
$newStartFails = $true
$result = Run-Case
Assert-Test (-not $result.success -and $result.original_restored -and (Get-UpgradeFileState $files) -eq 'Legacy' -and $driverState -eq 'Running' -and -not $catalog) 'Kernel-load failure did not recover'
$passed.Add('new kernel load failure restores and loads old driver')

foreach ($at in 1,2,3) {
    New-Case "partial-copy-$at"
    $copyFailsAt = $at
    $result = Run-Case
    Assert-Test (-not $result.success -and $result.original_restored -and (Get-UpgradeFileState $files) -eq 'Legacy') 'Partial copy was not recovered'
    $passed.Add("partial copy failure at file $at restores all originals")
}

New-Case 'unload-refused'
$stopFails = $true
$result = Run-Case
Assert-Test (-not $result.success -and -not $result.files_modified -and $copyCount -eq 0 -and (Get-UpgradeFileState $files) -eq 'Legacy') 'Unload refusal allowed writes'
$passed.Add('unload refusal leaves every system file unchanged')

New-Case 'catalog-failure'
$catalogFails = $true
$result = Run-Case
Assert-Test (-not $result.success -and -not $result.files_modified -and $copyCount -eq 0 -and $driverState -eq 'Running') 'Catalog failure did not preserve/restart the old driver'
$passed.Add('catalog failure restarts old driver without file writes')

New-Case 'mid-operation-reconnect'
$connectAfterCopy = 1
$result = Run-Case
Assert-Test (-not $result.success -and -not $result.original_restored -and $copyCount -eq 1 -and $result.rollback_error) 'Reconnection guard failed'
$connected = $false
$result = Run-Case 'Restore'
Assert-Test ($result.success -and (Get-UpgradeFileState $files) -eq 'Legacy') 'Restore did not repair interrupted mixed files'
$passed.Add('reconnection halts copying; explicit restore repairs mixed files after unplug')

New-Case 'damaged-target'
'damaged target' | Set-Content -LiteralPath $files[0].destination
$result = Run-Case 'Restore'
Assert-Test ($result.success -and (Get-UpgradeFileState $files) -eq 'Legacy') 'Restore cannot recover a damaged system file'
$passed.Add('explicit restore repairs damaged target from verified backup')

New-Case 'rollback-load-failure'
$allStartsFail = $true
$result = Run-Case
Assert-Test (-not $result.success -and -not $result.original_restored -and $result.rollback_error -and $catalog) 'Failed rollback was falsely reported as successful'
$passed.Add('rollback load failure is reported and catalog retained for recovery')

New-Case 'corrupt-backup'
'tamper' | Set-Content -LiteralPath (Join-Path $backupDirectory 'libusb0.sys')
Expect-Failure { Run-Case } '*verified original*'
Assert-Test ($events.Count -eq 0) 'Corrupt backup reached service operations'
$passed.Add('corrupt backup rejected before service operations')

New-Case 'wrong-machine'
$record = Get-Content -LiteralPath (Join-Path $backupDirectory 'backup.json') -Raw | ConvertFrom-Json
$record.machine = 'another-machine'
$record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $backupDirectory 'backup.json')
Expect-Failure { Run-Case } '*another computer*'
Assert-Test ($events.Count -eq 0) 'Foreign backup reached service operations'
$passed.Add('another computer backup rejected')

foreach ($guard in 'connected','busy','otherFilter','bindingChanged') {
    New-Case "preflight-$guard"
    Set-Variable -Name $guard -Value $true -Scope Script
    Expect-Failure { Run-Case } '*'
    Assert-Test ($events.Count -eq 0 -and $copyCount -eq 0) "Preflight guard failed: $guard"
    $passed.Add("preflight refuses $guard")
}

New-Case 'unknown-file'
'unknown' | Set-Content -LiteralPath $files[0].destination
Expect-Failure { Get-UpgradeFileState $files } '*Unknown libusb*'
Assert-Test ($events.Count -eq 0) 'Unknown version reached native operations'
$passed.Add('unknown installed version rejected')

New-Case 'mixed-file'
[IO.File]::Copy($files[0].source,$files[0].destination,$true)
Expect-Failure { Get-UpgradeFileState $files } '*Mixed libusb*'
$passed.Add('mixed installed versions rejected for upgrade')

New-Case 'candidate-tamper'
'changed candidate' | Set-Content -LiteralPath $files[0].source
$result = Run-Case
Assert-Test (-not $result.success -and $result.original_restored -and (Get-UpgradeFileState $files) -eq 'Legacy') 'Candidate change was not rejected/recovered'
$passed.Add('candidate change before copy rejected and original kept')

New-Case 'two-registered-devices' -Multiple
$record = Read-UpgradeBackup $backupDirectory $files
Assert-Test ($record.schema -eq 2 -and @(Get-UpgradeBackupPhones $record).Count -eq 2) 'Backup omitted a registered device'
$bindingsBefore = $devices | ConvertTo-Json -Depth 5 -Compress
$result = Run-Case
Assert-Test $result.success 'Multi-device upgrade failed'
$result = Run-Case 'Restore'
Assert-Test ($result.success -and ($devices | ConvertTo-Json -Depth 5 -Compress) -eq $bindingsBefore) 'Restore changed a device binding'
$passed.Add('shared upgrade/restore includes disconnected registered devices and preserves all bindings')

New-Case 'second-device-still-connected' -Multiple
$secondConnected = $true
Expect-Failure { Run-Case } '*Unplug ALL*'
Assert-Test ($events.Count -eq 0 -and $copyCount -eq 0) 'Connected second device allowed shared replacement'
$passed.Add('second connected device prevents shared upgrade')

New-Case 'unregistered-device-connected'
$secondConnected = $true
Expect-Failure { Run-Case } '*Unplug ALL*'
Assert-Test ($events.Count -eq 0) 'New unregistered device escaped unplug check'
$passed.Add('unregistered connected iPad also prevents shared replacement')

New-Case 'changed-registration-set' -Multiple
$devices = @($devices[0])
Expect-Failure { Run-Case } '*Registered device set changed*'
Assert-Test ($events.Count -eq 0) 'Changed registration set reached service operations'
$passed.Add('backup scope mismatch rejected before changing the shared driver')

New-Case 'older-single-device-backup'
$record = Read-UpgradeBackup $backupDirectory $files
$record.schema = 1
$record | Add-Member -NotePropertyName phone -NotePropertyValue $record.phones[0]
$record.PSObject.Properties.Remove('phones')
$record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $backupDirectory 'backup.json')
$result = Run-Case
Assert-Test $result.success 'Original schema-1 backup compatibility broken'
$passed.Add('previous single-device backup remains usable for unchanged scope')

# Check SID-based ACL construction without changing real permissions. Applying
# this descriptor belongs to the elevated entry point, not these simulations.
$acl = New-UpgradeBackupSecurity
Assert-Test $acl.AreAccessRulesProtected 'Backup ACL is not protected'
$rules = @($acl.GetAccessRules($true,$false,[Security.Principal.SecurityIdentifier]))
Assert-Test (@($rules | Where-Object { $_.IdentityReference.Value -eq 'S-1-5-32-544' -and $_.FileSystemRights -eq 'FullControl' }).Count -eq 1) 'Administrator backup access missing'
$passed.Add('protected backup ACL descriptor construction')

$report = [ordered]@{powershell=$PSVersionTable.PSVersion.ToString();passed=@($passed);count=$passed.Count;real_system_driver_changes=$false;scope='Real temporary-file backup/copy/hash operations and ACL descriptor construction; device, service, registry, catalog, ACL assignment and kernel loading are simulated.'}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $scratch 'results.json') -Encoding UTF8
$report | ConvertTo-Json -Depth 4
