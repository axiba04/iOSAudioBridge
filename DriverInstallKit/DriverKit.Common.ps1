# Shared checks for the Windows x64 installer. Compatible with Windows PowerShell 5.1.
$ErrorActionPreference = 'Stop'

function Assert-KitPayload([string]$Directory) {
    $base = [IO.Path]::GetFullPath($Directory).TrimEnd('\') + '\'
    $manifest = Get-Content -LiteralPath (Join-Path $base 'payload.json') -Raw | ConvertFrom-Json
    if ($manifest.schema -ne 1 -or $manifest.libusb_version -ne '1.4.0.2') { throw 'Unsupported kit manifest.' }
    foreach ($entry in $manifest.files) {
        $path = [IO.Path]::GetFullPath((Join-Path $base $entry.path))
        if (-not $path.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) { throw 'Payload path escapes the kit.' }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Payload hash mismatch: $($entry.path)" }
    }
    # Apple INF/SYS/DLL members can be catalog-only signed on a clean machine.
    # Their exact hashes were verified against the signed catalog when assembling
    # the kit; PnPUtil verifies the complete Apple package during installation.
    foreach ($relative in @('AppleUSB/AppleUsb.inf','AppleUSB/AppleKmdfFilter.sys','AppleUSB/AppleLowerFilter.sys','AppleUSB/AppleUsbFilter.dll')) {
        if (@($manifest.files | Where-Object path -eq $relative).Count -ne 1) { throw "Missing manifest entry: $relative" }
    }
    foreach ($relative in @('AppleMobileDeviceSupport64.msi','AppleUSB/appleusb.cat','libusb/libusb0.cat','libusb/amd64/libusb0.sys','libusb/amd64/libusb0.dll','libusb/amd64/install-filter.exe','libusb/x86/libusb0_x86.dll')) {
        if (@($manifest.files | Where-Object path -eq $relative).Count -ne 1) { throw "Missing manifest entry: $relative" }
        $signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $base $relative)
        if ($signature.Status -ne 'Valid') { throw "Signature check failed: $relative ($($signature.Status))" }
        $expected = if ($relative -like '*.cat') { 'O=Microsoft Corporation' } elseif ($relative -like 'Apple*') { 'O=Apple Inc\.' } else { 'O=(DonTech ApS|Microsoft Corporation)' }
        if ($signature.SignerCertificate.Subject -notmatch $expected) { throw "Unexpected signer: $relative" }
    }
    if (@($manifest.files | Where-Object path -eq 'AppleUSB/AppleUsb.inf').Count -ne 1) { throw 'Missing Apple INF manifest entry.' }
    return $manifest
}

function Get-DeviceValue([string]$InstanceId, [string]$Key) {
    if ($Key -in 'DEVPKEY_Device_UpperFilters','DEVPKEY_Device_LowerFilters') {
        # An unfiltered device may not have these properties at all.
        Get-PnpDeviceProperty -InstanceId $InstanceId -ErrorAction Stop | Where-Object KeyName -eq $Key | ForEach-Object { $_.Data }
        return
    }
    (Get-PnpDeviceProperty -InstanceId $InstanceId -KeyName $Key -ErrorAction Stop).Data
}

function Test-KitMobileInstance([string]$InstanceId) {
    # Same capture PID allowlist as AudioCore/Device/AppleUsbDiscovery.h.
    if ($InstanceId -notmatch '^USB\\VID_05AC&PID_([0-9A-F]{4})\\[A-Z0-9_&-]+$') { return $false }
    return $Matches[1] -in @('1290','1291','1292','1293','1294','1297','1299','129A','129C','129D','129E','129F','12A0','12A1','12A2','12A3','12A4','12A5','12A6','12A8','12A9','12AA','12AB','12AC')
}

function Get-KitBinding([string]$InstanceId) {
    # One PnP query per device, including optional filter properties. Individual
    # property queries are particularly slow for disconnected device instances.
    $values = @{}
    foreach ($property in Get-PnpDeviceProperty -InstanceId $InstanceId -ErrorAction Stop) { $values[$property.KeyName] = $property.Data }
    foreach ($key in 'DEVPKEY_Device_Service','DEVPKEY_Device_DriverVersion') {
        if (-not $values.ContainsKey($key)) { throw "Missing device property: $key" }
    }
    [pscustomobject]@{
        service=$values['DEVPKEY_Device_Service'];version=$values['DEVPKEY_Device_DriverVersion'];problem=$values['DEVPKEY_Device_ProblemCode']
        upper=@($values['DEVPKEY_Device_UpperFilters'] | Where-Object { $_ });lower=@($values['DEVPKEY_Device_LowerFilters'] | Where-Object { $_ })
    }
}

function Get-KitPhone([string]$InstanceId) {
    if (-not (Test-KitMobileInstance $InstanceId)) { throw 'Not a supported iPhone/iPad composite device instance.' }
    $device = Get-PnpDevice -InstanceId $InstanceId -ErrorAction Stop
    $binding = Get-KitBinding $InstanceId
    # Windows may omit dynamic ProblemCode from an all-properties query.
    if ($null -eq $binding.problem) { $binding.problem = Get-DeviceValue $InstanceId 'DEVPKEY_Device_ProblemCode' }
    $binding | Add-Member -NotePropertyName instance -NotePropertyValue $InstanceId
    $binding | Add-Member -NotePropertyName name -NotePropertyValue $device.FriendlyName
    return $binding
}

function Get-KitPhones {
    Get-PnpDevice -PresentOnly | Where-Object { Test-KitMobileInstance $_.InstanceId } | Sort-Object InstanceId | ForEach-Object { Get-KitPhone $_.InstanceId }
}

function Assert-KitPhone($Phone) {
    $phone = $Phone
    if (-not (Test-KitMobileInstance $phone.instance)) { throw 'Not a supported iPhone/iPad composite device instance.' }
    if ($phone.service -ne 'usbccgp' -or $phone.lower -notcontains 'AppleLowerFilter' -or $phone.problem -ne 0) {
        throw 'The Apple composite parent is not ready. Complete step 1, reconnect/reboot if requested, then retry.'
    }
}

function Select-KitPhone([object[]]$Phones, [string]$InstanceId, [switch]$Interactive) {
    if ($InstanceId) {
        $matches = @($Phones | Where-Object instance -eq $InstanceId)
        if ($matches.Count -ne 1) { throw 'The selected iPhone/iPad is no longer connected. Refresh and select it again.' }
        $phone = $matches[0]
    } else {
        if (-not $Phones.Count) { throw 'Connect at least one iPhone/iPad, unlock it and trust this computer, then retry.' }
        $index = 1
        if ($Phones.Count -gt 1) {
            if (-not $Interactive) { throw 'Multiple iPhone/iPad devices: select an exact -InstanceId or use the interactive installer.' }
            for ($i = 0; $i -lt $Phones.Count; $i++) {
                $status = if ($Phones[$i].upper -contains 'libusb0') { '已登记' } else { '未登记' }
                Write-Host ("[{0}] {1} [{2}]`n    {3}" -f ($i+1),$Phones[$i].name,$status,$Phones[$i].instance)
            }
            $answer = Read-Host '输入本次要登记的设备编号（每次一台），Q 取消'
            if ($answer -eq 'Q') { throw 'Selection cancelled; no device changed.' }
            if (-not [int]::TryParse($answer,[ref]$index) -or $index -lt 1 -or $index -gt $Phones.Count) { throw 'Invalid device selection; no device changed.' }
        }
        $phone = $Phones[$index-1]
    }
    Assert-KitPhone $phone
    return $phone
}

function Assert-CompatibleFile([string]$Path, [string]$ExpectedHash) {
    if ((Test-Path -LiteralPath $Path) -and (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $ExpectedHash) {
        throw "Another libusb version/file is installed: $Path. This fresh-install kit will not overwrite it; use the backed-up upgrade procedure."
    }
}

function Get-KitDeployments($Manifest, [string]$Directory) {
    foreach ($pair in @(
        @('libusb/amd64/libusb0.sys','System32/drivers/libusb0.sys'),
        @('libusb/amd64/libusb0.dll','System32/libusb0.dll'),
        @('libusb/x86/libusb0_x86.dll','SysWOW64/libusb0.dll')
    )) {
        $entry = @($Manifest.files | Where-Object path -eq $pair[0])
        if ($entry.Count -ne 1) { throw 'Required driver payload missing.' }
        [pscustomobject]@{ source = (Join-Path $Directory $pair[0]); destination = (Join-Path $env:SystemRoot $pair[1]); hash = $entry[0].sha256 }
    }
}

function Get-KitUsbNodes {
    foreach ($hardware in Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Enum\USB') {
        foreach ($node in Get-ChildItem -LiteralPath $hardware.PSPath) {
            $props = Get-ItemProperty -LiteralPath $node.PSPath
            [pscustomobject]@{instance=('USB\' + $hardware.PSChildName + '\' + $node.PSChildName);service=$props.Service;upper=@($props.UpperFilters | Where-Object { $_ });lower=@($props.LowerFilters | Where-Object { $_ })}
        }
    }
}

function Get-KitFilteredNodes {
    Get-KitUsbNodes | Where-Object { $_.upper -contains 'libusb0' -or $_.lower -contains 'libusb0' }
}

function Assert-KitUsbFilterNodes([object[]]$Nodes) {
    foreach ($node in $Nodes) {
        if ($node.service -eq 'libusb0') { throw 'A USB device is bound directly to libusb0; the shared driver requires a separate review.' }
        if ($node.upper -contains 'libusb0' -or $node.lower -contains 'libusb0') {
            if (-not (Test-KitMobileInstance $node.instance) -or $node.service -ne 'usbccgp' -or
                $node.lower -notcontains 'AppleLowerFilter' -or $node.lower -contains 'libusb0' -or
                @($node.upper).Count -ne 1 -or $node.upper[0] -ne 'libusb0') {
                throw 'An incompatible device uses the shared libusb filter. Only supported Apple composite parents with the expected filter stack are allowed.'
            }
        }
    }
}

function Assert-KitFilterScope([string]$InstanceId) {
    Assert-KitUsbFilterNodes @(Get-KitUsbNodes)
    foreach ($node in Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Control\Class') {
        $props = Get-ItemProperty -LiteralPath $node.PSPath
        if (@($props.UpperFilters) -contains 'libusb0' -or @($props.LowerFilters) -contains 'libusb0') { throw 'A global libusb class filter exists.' }
    }
    if (@(Get-CimInstance Win32_PnPEntity -Filter "Service='libusb0'").Count) {
        throw 'A device is bound directly to libusb0. Resolve its driver setup separately.'
    }
    $driver = Get-CimInstance Win32_SystemDriver -Filter "Name='libusb0'"
    if ($driver -and [IO.Path]::GetFullPath($driver.PathName) -ne [IO.Path]::GetFullPath((Join-Path $env:SystemRoot 'System32/drivers/libusb0.sys'))) {
        throw 'Unexpected libusb0 service path.'
    }
    if (Test-Path 'HKLM:\SYSTEM\CurrentControlSet\Services\libusb0') {
        if ((Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Services\libusb0').Type -ne 1) { throw 'Existing libusb0 service is not a kernel driver.' }
    }
}

function Get-KitRegisteredPhones {
    Assert-KitFilterScope
    Get-KitFilteredNodes | Sort-Object instance | ForEach-Object { Get-KitPhone $_.instance }
}

function Get-KitPhoneStatus([object[]]$Phones) {
    foreach ($phone in $Phones) {
        $status = 'READY'
        $detail = 'Apple parent healthy; libusb filter registered.'
        try { Assert-KitPhone $phone } catch { $status = 'NOT_READY'; $detail = $_.Exception.Message }
        if ($status -eq 'READY' -and $phone.upper -notcontains 'libusb0') { $status = 'NOT_REGISTERED'; $detail = 'Run 02-iPhoneFilter.cmd and select this device.' }
        [pscustomobject]@{status=$status;name=$phone.name;instance=$phone.instance;detail=$detail}
    }
}

function Assert-KitOtherBindings([object[]]$Before, [string]$SelectedInstance) {
    $after = @(Get-KitFilteredNodes | Where-Object instance -ne $SelectedInstance | Sort-Object instance)
    $expected = @($Before | Sort-Object instance)
    if (($after | ConvertTo-Json -Depth 4 -Compress) -ne ($expected | ConvertTo-Json -Depth 4 -Compress)) { throw 'Another registered device binding changed during installation; inspect the device logs.' }
}

function Register-KitCatalog([string]$Directory) {
    $catalogName = 'iOSAudioBridge-libusb0-1.4.0.2.cat'
    $catalog = Join-Path $Directory 'libusb/libusb0.cat'
    $registered = Join-Path $env:SystemRoot "System32/CatRoot/{F750E6C3-38EE-11D1-85E5-00C04FC295EE}/$catalogName"
    Assert-CompatibleFile $registered (Get-FileHash -LiteralPath $catalog).Hash
    Initialize-KitCatalogApi
    [AudioBridge.DriverKitCatalog]::Register($catalog, $catalogName)
}

function Initialize-KitCatalogApi {
    if ('AudioBridge.DriverKitCatalog' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
namespace AudioBridge {
    public static class DriverKitCatalog {
        [DllImport("wintrust.dll", SetLastError=true)]
        static extern bool CryptCATAdminAcquireContext(out IntPtr context, ref Guid subsystem, uint flags);
        [DllImport("wintrust.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
        static extern IntPtr CryptCATAdminAddCatalog(IntPtr context, string path, string name, uint flags);
        [DllImport("wintrust.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
        static extern bool CryptCATAdminRemoveCatalog(IntPtr context, string name, uint flags);
        [DllImport("wintrust.dll", SetLastError=true)]
        static extern bool CryptCATAdminReleaseCatalogContext(IntPtr context, IntPtr catalog, uint flags);
        [DllImport("wintrust.dll", SetLastError=true)]
        static extern bool CryptCATAdminReleaseContext(IntPtr context, uint flags);
        public static void Register(string path, string name) {
            var subsystem = new Guid("F750E6C3-38EE-11D1-85E5-00C04FC295EE");
            IntPtr context;
            if (!CryptCATAdminAcquireContext(out context, ref subsystem, 0))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            try {
                var catalog = CryptCATAdminAddCatalog(context, path, name, 0);
                if (catalog == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
                CryptCATAdminReleaseCatalogContext(context, catalog, 0);
            } finally { CryptCATAdminReleaseContext(context, 0); }
        }
        public static void Remove(string name) {
            if (String.IsNullOrEmpty(name) || name != System.IO.Path.GetFileName(name))
                throw new ArgumentException("Catalog removal requires a basename.");
            var subsystem = new Guid("F750E6C3-38EE-11D1-85E5-00C04FC295EE");
            IntPtr context;
            if (!CryptCATAdminAcquireContext(out context, ref subsystem, 0))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            try {
                if (!CryptCATAdminRemoveCatalog(context, name, 0))
                    throw new Win32Exception(Marshal.GetLastWin32Error());
            } finally { CryptCATAdminReleaseContext(context, 0); }
        }
    }
}
'@
}

function Invoke-KitNative([string]$File, [string[]]$Arguments) {
    & $File @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "$([IO.Path]::GetFileName($File)) returned $LASTEXITCODE. See the installation log." }
}

function Install-KitFilter($Phone, $Manifest, [string]$Directory, [string]$LogDirectory) {
    # Resolve the selected instance again after elevation; never silently switch
    # to a different phone if a cable was moved while UAC was open.
    $Phone = Select-KitPhone @(Get-KitPhones) $Phone.instance
    Assert-KitFilterScope $Phone.instance
    if (@($Phone.upper | Where-Object { $_ -ne 'libusb0' }).Count -or $Phone.lower -contains 'libusb0') { throw 'Unexpected filter stack on selected device.' }
    $others = @(Get-KitFilteredNodes | Where-Object instance -ne $Phone.instance)
    $files = @(Get-KitDeployments $Manifest $Directory)
    foreach ($file in $files) { Assert-CompatibleFile $file.destination $file.hash }
    if ($others.Count) {
        foreach ($file in $files) { if (-not (Test-Path -LiteralPath $file.destination)) { throw 'A shared driver file is missing; repair the existing installation before registering another device.' } }
        if ((Get-CimInstance Win32_SystemDriver -Filter "Name='libusb0'").State -ne 'Running') { throw 'The existing shared driver is not running. Inspect the registered devices before adding another.' }
    }
    if ($Phone.upper -contains 'libusb0') {
        foreach ($file in $files) { if (-not (Test-Path -LiteralPath $file.destination)) { throw 'An existing filter has missing files; repair it separately.' } }
        if ((Get-CimInstance Win32_SystemDriver -Filter "Name='libusb0'").State -ne 'Running') { throw 'Existing filter is not running. Reconnect/reboot and inspect the device.' }
        Write-Host 'libusb 1.4.0.2 is already registered on this iPhone/iPad. No changes made.'
        return
    }
    $Phone | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $LogDirectory 'phone-before.json') -Encoding UTF8
    Register-KitCatalog $Directory
    foreach ($file in $files) {
        if (-not (Test-Path -LiteralPath $file.destination)) { [IO.File]::Copy($file.source, $file.destination, $false) }
        Assert-CompatibleFile $file.destination $file.hash
    }
    if (-not (Get-CimInstance Win32_SystemDriver -Filter "Name='libusb0'")) {
        Invoke-KitNative "$env:SystemRoot/System32/sc.exe" @('create','libusb0','type=','kernel','start=','demand','binPath=','system32\drivers\libusb0.sys','DisplayName=','libusb-win32 1.4.0.2')
    }
    if ((Get-CimInstance Win32_SystemDriver -Filter "Name='libusb0'").State -ne 'Running') {
        # Let Windows enforce kernel signing policy before adding the device filter.
        Invoke-KitNative "$env:SystemRoot/System32/sc.exe" @('start','libusb0')
    }
    if ((Get-CimInstance Win32_SystemDriver -Filter "Name='libusb0'").State -ne 'Running') { throw 'Windows did not load the driver; the iPhone filter has not been changed.' }
    $installer = Join-Path $Directory 'libusb/amd64/install-filter.exe'
    try {
        # -di is an exact device instance, not a hardware class or a VID/PID wildcard.
        Invoke-KitNative $installer @('i',('-di=' + $Phone.instance))
        $upper = @(Get-DeviceValue $Phone.instance 'DEVPKEY_Device_UpperFilters' | Where-Object { $_ })
        $lower = @(Get-DeviceValue $Phone.instance 'DEVPKEY_Device_LowerFilters' | Where-Object { $_ })
        if (@($upper | Where-Object { $_ -eq 'libusb0' }).Count -ne 1 -or ((@($upper | Where-Object { $_ -ne 'libusb0' })) -join '|') -ne ($Phone.upper -join '|') -or ($lower -join '|') -ne ($Phone.lower -join '|') -or (Get-DeviceValue $Phone.instance 'DEVPKEY_Device_Service') -ne 'usbccgp') {
            throw 'Unexpected device binding after filter installation.'
        }
        Assert-KitOtherBindings $others $Phone.instance
    } catch {
        # Only undo the filter added to this exact phone. Keep signed system files
        # for diagnosis/retry; never remove an existing shared kernel service here.
        try {
            Invoke-KitNative $installer @('u',('-di=' + $Phone.instance))
            $after = @(Get-DeviceValue $Phone.instance 'DEVPKEY_Device_UpperFilters' | Where-Object { $_ })
            if (($after -join '|') -ne ($Phone.upper -join '|')) { throw 'Filter rollback did not match the saved state.' }
        } catch { Write-Warning "Filter rollback needs attention: $($_.Exception.Message)" }
        throw
    }
    Write-Host 'Filter registered for the selected device. Reconnect/unlock it, then run 03-Check.cmd. Run step 2 again to register another iPhone/iPad.'
}
