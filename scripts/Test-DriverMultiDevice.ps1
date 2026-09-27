# Real installer selection, registration, idempotence and rollback logic;
# all PnP/registry/native/catalog calls below are simulated. No driver changes.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. (Join-Path $root 'DriverInstallKit/DriverKit.Common.ps1')
$scratch = Join-Path $root ('build/driver-multidevice-tests/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$passed = [Collections.Generic.List[string]]::new()
$firstId = 'USB\VID_05AC&PID_12A8\SIMULATED-IPHONE'
$secondId = 'USB\VID_05AC&PID_12AB\SIMULATED-IPAD'
function Assert-Test([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Expect-Failure([scriptblock]$Action, [string]$Message) {
    try { & $Action | Out-Null } catch { if ($_.Exception.Message -like $Message) { return }; throw }
    throw "Expected failure: $Message"
}
function Get-KitUsbNodes { $script:devices | Select-Object instance,service,upper,lower }
function Get-PnpDevice {
    [CmdletBinding()]param([switch]$PresentOnly,[string]$InstanceId)
    foreach ($device in $script:devices) {
        if ($PresentOnly -and -not $device.present) { continue }
        if ($InstanceId -and $InstanceId -ne $device.instance) { continue }
        [pscustomobject]@{InstanceId=$device.instance;FriendlyName=$device.name}
    }
}
function Get-DeviceValue([string]$InstanceId,[string]$Key) {
    $device = @($script:devices | Where-Object instance -eq $InstanceId)
    if ($device.Count -ne 1) { throw 'Unexpected simulated instance' }
    switch ($Key) {
        'DEVPKEY_Device_Service' { $device[0].service }
        'DEVPKEY_Device_DriverVersion' { '538.0.0.0' }
        'DEVPKEY_Device_ProblemCode' { $device[0].problem }
        'DEVPKEY_Device_UpperFilters' { $device[0].upper }
        'DEVPKEY_Device_LowerFilters' { $device[0].lower }
        default { throw 'Unexpected simulated property' }
    }
}
function Get-PnpDeviceProperty {
    [CmdletBinding()]param([string]$InstanceId)
    # Match Windows omitting dynamic ProblemCode from all-properties queries.
    foreach ($key in 'Service','DriverVersion','UpperFilters','LowerFilters') {
        [pscustomobject]@{KeyName=('DEVPKEY_Device_' + $key);Data=(Get-DeviceValue $InstanceId ('DEVPKEY_Device_' + $key))}
    }
}
function Get-ChildItem {
    param([string]$Path)
    if ($Path -ne 'HKLM:\SYSTEM\CurrentControlSet\Control\Class') { throw 'Unmocked registry enumeration forbidden' }
    [pscustomobject]@{PSPath='simulated-usb-class'}
}
function Get-ItemProperty {
    param([string]$Path,[string]$LiteralPath)
    if ($LiteralPath -eq 'simulated-usb-class') {
        [pscustomobject]@{UpperFilters=$(if ($script:globalFilter) { @('libusb0') } else { @() });LowerFilters=@()}
    } elseif ($Path -eq 'HKLM:\SYSTEM\CurrentControlSet\Services\libusb0') { [pscustomobject]@{Type=1} }
    else { throw 'Unmocked registry read forbidden' }
}
function Test-Path {
    [CmdletBinding()]param([string]$Path,[string]$LiteralPath,[string]$PathType)
    if ($Path -eq 'HKLM:\SYSTEM\CurrentControlSet\Services\libusb0') { return $true }
    Microsoft.PowerShell.Management\Test-Path @PSBoundParameters
}
function Get-CimInstance {
    param([string]$ClassName,[string]$Filter)
    if ($ClassName -eq 'Win32_PnPEntity') { return }
    if ($ClassName -ne 'Win32_SystemDriver' -or $Filter -ne "Name='libusb0'") { throw 'Unmocked CIM call forbidden' }
    [pscustomobject]@{State=$script:driverState;PathName=(Join-Path $env:SystemRoot 'System32/drivers/libusb0.sys')}
}
function Get-KitDeployments($Manifest,[string]$Directory) { $script:files }
function Register-KitCatalog([string]$Directory) { $script:events.Add('catalog') }
function Invoke-KitNative([string]$File,[string[]]$Arguments) {
    if ($File -eq "$env:SystemRoot/System32/sc.exe" -and ($Arguments -join '|') -eq 'start|libusb0') {
        $script:events.Add('start:libusb0')
        if ($script:failStart) { throw 'simulated driver startup failure' }
        if (-not $script:stayStopped) { $script:driverState = 'Running' }
        return
    }
    if ([IO.Path]::GetFileName($File) -ne 'install-filter.exe' -or $Arguments.Count -ne 2 -or $Arguments[1] -notlike '-di=*') { throw 'Unscoped native operation forbidden' }
    $id = $Arguments[1].Substring(4)
    $target = @($script:devices | Where-Object instance -eq $id)
    if ($target.Count -ne 1) { throw 'Native operation targeted unknown device' }
    $script:events.Add($Arguments[0] + ':' + $id)
    if ($Arguments[0] -eq 'i') {
        $target[0].upper = @('libusb0')
        if ($script:failInstall) { throw 'simulated failure after filter added' }
    } elseif ($Arguments[0] -eq 'u') { $target[0].upper = @() }
    else { throw 'Unexpected native verb' }
}
function Read-Host([string]$Prompt) { return $script:answer }
function New-Case([string]$Name) {
    $script:caseDirectory = Join-Path $scratch $Name
    New-Item -ItemType Directory -Path $script:caseDirectory | Out-Null
    $script:devices = @(
        [pscustomobject]@{instance=$firstId;name='Apple Mobile Device USB Composite Device';service='usbccgp';upper=@('libusb0');lower=@('AppleLowerFilter');problem=0;present=$true},
        [pscustomobject]@{instance=$secondId;name='Apple Mobile Device USB Composite Device';service='usbccgp';upper=@();lower=@('AppleLowerFilter');problem=0;present=$true}
    )
    $script:globalFilter = $false; $script:failInstall = $false; $script:answer = '2'
    $script:driverState = 'Running'; $script:failStart = $false; $script:stayStopped = $false
    $script:events = [Collections.Generic.List[string]]::new()
    $script:files = @(foreach ($name in 'libusb0.sys','libusb0-x64.dll','libusb0-x86.dll') {
        $path = Join-Path $script:caseDirectory $name
        "verified current $name" | Set-Content -LiteralPath $path
        [pscustomobject]@{source=$path;destination=$path;hash=(Get-FileHash -LiteralPath $path).Hash}
    })
}
function Register-Second { Install-KitFilter (Select-KitPhone @(Get-KitPhones) $secondId) $null $caseDirectory $caseDirectory }

New-Case 'exact-selection'
$phone = Select-KitPhone @(Get-KitPhones) $secondId
Assert-Test ($phone.instance -eq $secondId) 'Exact selection chose the other same-name device'
Assert-Test ($phone.problem -eq 0) 'Dynamic ProblemCode fallback failed'
$phone = Select-KitPhone @((Get-KitPhones) | Sort-Object instance -Descending) $secondId
Assert-Test ($phone.instance -eq $secondId) 'Reordered discovery changed the selected device'
$passed.Add('exact instance selection survives identical names and changed enumeration order')
$phone = Select-KitPhone @(Get-KitPhones) -Interactive
Assert-Test ($phone.instance -eq $secondId) 'Interactive menu chose wrong device'
$passed.Add('numbered menu selects the requested device')
foreach ($badAnswer in '0','3','abc','Q') {
    $answer = $badAnswer
    Expect-Failure { Select-KitPhone @(Get-KitPhones) -Interactive } '*'
}
Assert-Test ($events.Count -eq 0) 'Cancelled/invalid selection mutated a device'
$passed.Add('cancel and invalid menu selections cause no native operations')

New-Case 'device-disappeared'
$selected = Select-KitPhone @(Get-KitPhones) $secondId
$devices[1].present = $false
Expect-Failure { Install-KitFilter $selected $null $caseDirectory $caseDirectory } '*no longer connected*'
Assert-Test ($events.Count -eq 0) 'Missing target fell back to another device'
$passed.Add('unplugged selection fails without substituting another connected device')

New-Case 'register-second'
$original = $devices[0] | ConvertTo-Json -Depth 4 -Compress
$hashes = @($files | ForEach-Object { (Get-FileHash -LiteralPath $_.destination).Hash })
Register-Second
Assert-Test ($devices[1].upper -contains 'libusb0' -and ($devices[0] | ConvertTo-Json -Depth 4 -Compress) -eq $original) 'Registering the second device changed the first'
Assert-Test (@($events | Where-Object { $_ -like 'i:*' }).Count -eq 1 -and $events -contains ('i:' + $secondId)) 'Registration targeted multiple devices'
Assert-Test (-not (Compare-Object $hashes @($files | ForEach-Object { (Get-FileHash -LiteralPath $_.destination).Hash }))) 'Registration rewrote shared driver files'
Assert-Test (@(Get-KitPhoneStatus @(Get-KitPhones) | Where-Object status -eq 'READY').Count -eq 2) 'Check did not report both registered devices'
$passed.Add('register second device only, preserve first binding and shared files, report both READY')
$events.Clear()
Register-Second
Assert-Test ($events.Count -eq 0) 'Repeated registration performed native changes'
$passed.Add('repeated registration is a no-op')

New-Case 'first-offline'
$devices[0].present = $false
Register-Second
Assert-Test ($devices[0].upper -contains 'libusb0' -and $devices[1].upper -contains 'libusb0') 'Offline registration was lost'
$passed.Add('existing disconnected device remains registered when adding another')

New-Case 'first-offline-driver-stopped'
$devices[0].present = $false
$driverState = 'Stopped'
$original = $devices[0] | ConvertTo-Json -Depth 4 -Compress
$hashes = @($files | ForEach-Object { (Get-FileHash -LiteralPath $_.destination).Hash })
Register-Second
Assert-Test ($driverState -eq 'Running' -and $devices[1].upper -contains 'libusb0') 'Stopped shared driver prevented new registration'
Assert-Test (($devices[0] | ConvertTo-Json -Depth 4 -Compress) -eq $original) 'Starting shared driver changed offline registration'
Assert-Test ($events.IndexOf('start:libusb0') -ge 0 -and $events.IndexOf('start:libusb0') -lt $events.IndexOf('i:' + $secondId)) 'Filter registered before driver startup'
Assert-Test (-not (Compare-Object $hashes @($files | ForEach-Object { (Get-FileHash -LiteralPath $_.destination).Hash }))) 'Starting shared driver rewrote existing files'
$passed.Add('start stopped shared driver before registering iPad; preserve offline device and shared files')

foreach ($failure in 'start-error','stays-stopped') {
    New-Case $failure
    $devices[0].present = $false
    $driverState = 'Stopped'
    $failStart = $failure -eq 'start-error'
    $stayStopped = $failure -eq 'stays-stopped'
    $original = $devices | ConvertTo-Json -Depth 4 -Compress
    $message = if ($failStart) { '*simulated driver startup failure*' } else { '*Windows did not load the driver*' }
    Expect-Failure { Register-Second } $message
    Assert-Test ($events -contains 'start:libusb0' -and @($events | Where-Object { $_ -like 'i:*' -or $_ -like 'u:*' }).Count -eq 0) 'Failed startup reached filter installation'
    Assert-Test (($devices | ConvertTo-Json -Depth 4 -Compress) -eq $original) 'Failed startup changed device bindings'
    $passed.Add("driver startup failure leaves all device bindings intact: $failure")
}

New-Case 'failed-registration'
$original = $devices[0] | ConvertTo-Json -Depth 4 -Compress
$failInstall = $true
Expect-Failure { Register-Second } '*simulated failure*'
Assert-Test ($devices[1].upper.Count -eq 0 -and ($devices[0] | ConvertTo-Json -Depth 4 -Compress) -eq $original -and $events -contains ('u:' + $secondId)) 'Rollback touched the wrong device'
$passed.Add('partial registration failure rolls back only the selected device')

New-Case 'mixed-readiness'
$statuses = @(Get-KitPhoneStatus @(Get-KitPhones))
Assert-Test ($statuses[0].status -eq 'READY' -and $statuses[1].status -eq 'NOT_REGISTERED') 'Missing filter was hidden by another ready device'
$passed.Add('per-device checks distinguish ready and unregistered devices')

foreach ($kind in 'accessory','nonApple','childInterface','unknownPid','directDriver','lowerFilter','extraUpper','missingAppleLower','globalFilter') {
    New-Case ('incompatible-' + $kind)
    switch ($kind) {
        'accessory' { $devices[0].instance = 'USB\VID_05AC&PID_110A\ADAPTER' }
        'nonApple' { $devices[0].instance = 'USB\VID_1234&PID_5678\OTHER' }
        'childInterface' { $devices[0].instance = 'USB\VID_05AC&PID_12A8&MI_00\CHILD' }
        'unknownPid' { $devices[0].instance = 'USB\VID_05AC&PID_FFFF\OTHER' }
        'directDriver' { $devices[0].service = 'libusb0' }
        'lowerFilter' { $devices[0].lower = @('AppleLowerFilter','libusb0') }
        'extraUpper' { $devices[0].upper = @('libusb0','unknownFilter') }
        'missingAppleLower' { $devices[0].lower = @() }
        'globalFilter' { $globalFilter = $true }
    }
    Expect-Failure { Register-Second } '*'
    Assert-Test ($events.Count -eq 0) "Unsafe shared scope reached native install: $kind"
    $passed.Add("reject shared scope: $kind")
}

New-Case 'apple-accessory-enumeration'
$devices[1].instance = 'USB\VID_05AC&PID_110A\ADAPTER'
Assert-Test (@(Get-KitPhones).Count -eq 1) 'Apple audio adapter offered as capture target'
$passed.Add('Apple accessory omitted from device selection')

New-Case 'old-system-files'
$driverState = 'Stopped'
'old version' | Set-Content -LiteralPath $files[0].destination
Expect-Failure { Register-Second } '*will not overwrite*'
Assert-Test ($events.Count -eq 0 -and $devices[1].upper.Count -eq 0) 'Old shared driver allowed a new registration'
$passed.Add('old shared driver requires upgrade before registering another device')

New-Case 'missing-shared-file'
$driverState = 'Stopped'
$files[0].destination = Join-Path $caseDirectory 'missing-driver.sys'
Expect-Failure { Register-Second } '*shared driver file is missing*'
Assert-Test ($events.Count -eq 0) 'Missing shared file triggered a live driver repair'
$passed.Add('missing shared file blocks new registration without attempting live repair')

$report = [ordered]@{powershell=$PSVersionTable.PSVersion.ToString();passed=@($passed);count=$passed.Count;real_system_driver_changes=$false;scope='Real selection, scope guards, per-device status and installation/rollback code; PnP, registry, services, catalog and install-filter execution are simulated. Shared-file hash checks use workspace temporary files.'}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $scratch 'results.json') -Encoding UTF8
$report | ConvertTo-Json -Depth 4
