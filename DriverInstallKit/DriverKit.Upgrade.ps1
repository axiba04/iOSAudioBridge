# Functions only; dot-sourcing this file never changes a device or driver.
$ErrorActionPreference = 'Stop'

function Get-UpgradeLegacyHashes {
    # Exact 1.2.6.0 files from the signed, verified legacy distribution.
    @{
        'libusb0.sys' = '8058F2AFE6EF96A7D2DED432997FD8655970C9EA75A938EE4557D6A2CB4CC989'
        'libusb0-x64.dll' = '4F18B5D2C28AA66B648C8683C6D09B52B92CBBEE85984BBEFAD5F38A64BC2A14'
        'libusb0-x86.dll' = '00CACA07869B19D10B370552AC7CC2F6F2EE246FC15DB11650F6CD3F4EF9B666'
    }
}

function Get-UpgradeFiles($Manifest, [string]$Directory) {
    $deployments = @(Get-KitDeployments $Manifest $Directory)
    if ($deployments.Count -ne 3) { throw 'Expected exactly three system libusb files.' }
    $names = @('libusb0.sys','libusb0-x64.dll','libusb0-x86.dll')
    $legacy = Get-UpgradeLegacyHashes
    for ($i = 0; $i -lt 3; $i++) {
        [pscustomobject]@{name=$names[$i];destination=$deployments[$i].destination;source=$deployments[$i].source;new_hash=$deployments[$i].hash;old_hash=$legacy[$names[$i]]}
    }
}

function Get-UpgradeFileState([object[]]$Files) {
    $old = 0; $new = 0
    foreach ($file in $Files) {
        if (-not (Test-Path -LiteralPath $file.destination)) { throw 'A system libusb file is missing; use Restore with a valid backup or repair the existing installation.' }
        $hash = (Get-FileHash -LiteralPath $file.destination -Algorithm SHA256).Hash
        if ($hash -eq $file.old_hash) { $old++ } elseif ($hash -eq $file.new_hash) { $new++ } else { throw 'Unknown libusb file. This kit upgrades only the verified 1.2.6.0 distribution.' }
    }
    if ($old -eq 3) { return 'Legacy' }
    if ($new -eq 3) { return 'Current' }
    throw 'Mixed libusb versions; restore a verified backup before upgrading.'
}

function Assert-UpgradeIdle {
    if (Get-Process ZLUsbAudioBridge,AudioProbe,iPhoneMirror,'iPhoneMirror.UsbAudioOnly','AudioBridge.UsbRestore' -ErrorAction SilentlyContinue) {
        throw 'Close all phone capture applications before upgrading or restoring.'
    }
}

function Get-UpgradePhones {
    $phones = @(Get-KitRegisteredPhones)
    Assert-UpgradeBinding $phones
    return $phones
}

function Get-UpgradeBackupPhones($Record) {
    $phones = @(if ($Record.schema -eq 1) { $Record.phone } else { $Record.phones })
    if (-not $phones.Count -or @($phones.instance | Sort-Object -Unique).Count -ne $phones.Count) { throw 'Backup device list is missing or contains duplicates.' }
    foreach ($phone in $phones) {
        if (-not (Test-KitMobileInstance $phone.instance) -or $phone.service -ne 'usbccgp' -or $phone.upper -notcontains 'libusb0') { throw 'Invalid Apple filter backup.' }
    }
    return $phones
}

function Assert-UpgradeBinding([object[]]$Phone) {
    Assert-KitFilterScope
    $expectedIds = @($Phone.instance | Sort-Object -Unique)
    $currentIds = @((Get-KitFilteredNodes).instance | Sort-Object -Unique)
    if (-not $Phone.Count -or $expectedIds.Count -ne $Phone.Count -or ($expectedIds -join '|') -ne ($currentIds -join '|')) {
        throw 'Registered device set changed or is empty. This shared-driver operation requires a backup covering every registered device; no bindings will be changed.'
    }
    foreach ($device in $Phone) {
        if (-not (Test-KitMobileInstance $device.instance) -or $device.service -ne 'usbccgp' -or $device.upper -notcontains 'libusb0') { throw 'Invalid Apple filter backup.' }
        $binding = Get-KitBinding $device.instance
        foreach ($key in 'service','version','upper','lower') {
            $actual = @($binding.$key | Where-Object { $_ }) -join '|'
            $expected = @($device.$key | Where-Object { $_ }) -join '|'
            if ($actual -ne $expected) { throw "Apple binding changed: $key. No automatic rebind will be attempted." }
        }
    }
}

function Assert-UpgradeDisconnected($Phone) {
    # All iPhone/iPad cables, including a newly connected unregistered device.
    if (@(Get-PnpDevice -PresentOnly | Where-Object { Test-KitMobileInstance $_.InstanceId }).Count) { throw 'Unplug ALL iPhone/iPad devices and keep them disconnected until the operation finishes.' }
}

function Get-UpgradeDriverState {
    $driver = Get-CimInstance Win32_SystemDriver -Filter "Name='libusb0'"
    if (-not $driver) { throw 'The existing libusb0 service is missing.' }
    return $driver.State
}

function Stop-UpgradeDriver($Phone) {
    Assert-UpgradeBinding $Phone
    Assert-UpgradeDisconnected $Phone
    if ((Get-UpgradeDriverState) -ne 'Stopped') { Invoke-KitNative "$env:SystemRoot/System32/sc.exe" @('stop','libusb0') }
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        if ((Get-UpgradeDriverState) -eq 'Stopped') { return }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Driver did not unload. No loaded driver files will be overwritten; restart Windows with the phone unplugged, then retry.'
}

function Start-UpgradeDriver($Phone) {
    Assert-UpgradeDisconnected $Phone
    Invoke-KitNative "$env:SystemRoot/System32/sc.exe" @('start','libusb0')
    if ((Get-UpgradeDriverState) -ne 'Running') { throw 'Windows did not load the kernel driver.' }
    Assert-UpgradeBinding $Phone
}

function Copy-UpgradeFile($Phone, [string]$Source, [string]$Destination, [string]$Hash) {
    Assert-UpgradeIdle
    Assert-UpgradeBinding $Phone
    Assert-UpgradeDisconnected $Phone
    if ((Get-UpgradeDriverState) -ne 'Stopped') { throw 'Driver loaded during operation; file replacement stopped.' }
    if ((Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash -ne $Hash) { throw 'Source hash changed before copy.' }
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
    if ((Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash -ne $Hash) { throw 'Installed driver hash mismatch.' }
}

function Get-UpgradeBackupRoot { Join-Path $env:ProgramData 'iOSAudioBridge/DriverBackups' }
function Get-UpgradeMachineId { (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Cryptography' -Name MachineGuid).MachineGuid }

function Assert-UpgradeBackupPath([string]$Directory) {
    $root = [IO.Path]::GetFullPath((Get-UpgradeBackupRoot)).TrimEnd('\')
    $full = [IO.Path]::GetFullPath($Directory)
    if (-not $full.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Backup must be inside this computer''s ProgramData driver backup directory.' }
    $node = Get-Item -LiteralPath $full
    while ($node) {
        if ($node.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Backup path contains a junction or symbolic link.' }
        $node = $node.Parent
    }
    return $full
}

function New-UpgradeBackupSecurity {
    # Backups remain readable in the normal console but only administrators and
    # SYSTEM can change them. No credentials or phone audio are stored.
    $acl = New-Object Security.AccessControl.DirectorySecurity
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($sid in @('S-1-5-18','S-1-5-32-544','S-1-5-32-545')) {
        $rights = if ($sid -eq 'S-1-5-32-545') { 'ReadAndExecute' } else { 'FullControl' }
        $rule = New-Object Security.AccessControl.FileSystemAccessRule((New-Object Security.Principal.SecurityIdentifier($sid)), $rights, 'ContainerInherit,ObjectInherit', 'None', 'Allow')
        $acl.AddAccessRule($rule)
    }
    return $acl
}

function Initialize-UpgradeBackupRoot {
    $root = Get-UpgradeBackupRoot
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    # Check every ancestor before applying the ACL or writing backup files.
    $node = Get-Item -LiteralPath $root
    while ($node) {
        if ($node.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Backup root contains a junction or symbolic link.' }
        $node = $node.Parent
    }
    # Persist SID-based rules directly. Set-Acl can attempt NTAccount/domain
    # name resolution even when every rule already uses a well-known SID.
    $acl = New-UpgradeBackupSecurity
    if ($PSVersionTable.PSEdition -eq 'Desktop') { [IO.Directory]::SetAccessControl($root, $acl) }
    else { [IO.FileSystemAclExtensions]::SetAccessControl((New-Object IO.DirectoryInfo($root)), $acl) }
}

function Export-UpgradeService([string]$Directory) {
    Invoke-KitNative "$env:SystemRoot/System32/reg.exe" @('export','HKLM\SYSTEM\CurrentControlSet\Services\libusb0',(Join-Path $Directory 'libusb0-service.reg'),'/y')
}

function Save-UpgradeBackup([object[]]$Phone, [object[]]$Files) {
    if ((Get-UpgradeFileState $Files) -ne 'Legacy') { throw 'Backup creation requires a complete verified 1.2.6.0 installation.' }
    Assert-UpgradeBinding $Phone
    Initialize-UpgradeBackupRoot
    $directory = Join-Path (Get-UpgradeBackupRoot) ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
    New-Item -ItemType Directory -Path $directory | Out-Null
    $directory = Assert-UpgradeBackupPath $directory
    foreach ($file in $Files) {
        $destination = Join-Path $directory $file.name
        [IO.File]::Copy($file.destination, $destination, $false)
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $file.old_hash) { throw 'Backup copy verification failed.' }
    }
    Export-UpgradeService $directory
    $record = [ordered]@{schema=2;complete=$true;created_at=(Get-Date -Format o);machine=(Get-UpgradeMachineId);source_version='1.2.6.0';target_version='1.4.0.2';phones=@($Phone);files=@($Files | Select-Object name,old_hash);service_export_sha256=(Get-FileHash -LiteralPath (Join-Path $directory 'libusb0-service.reg')).Hash}
    $record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $directory 'backup.json') -Encoding UTF8
    return $directory
}

function Read-UpgradeBackup([string]$Directory, [object[]]$Files) {
    $directory = Assert-UpgradeBackupPath $Directory
    $record = Get-Content -LiteralPath (Join-Path $directory 'backup.json') -Raw | ConvertFrom-Json
    if ($record.schema -notin 1,2 -or -not $record.complete -or $record.source_version -ne '1.2.6.0' -or $record.target_version -ne '1.4.0.2' -or $record.machine -ne (Get-UpgradeMachineId)) { throw 'Incomplete backup or backup from another computer.' }
    $null = Get-UpgradeBackupPhones $record
    if (@($record.files).Count -ne 3) { throw 'Backup file list is incomplete.' }
    foreach ($file in $Files) {
        $entry = @($record.files | Where-Object name -eq $file.name)
        if ($entry.Count -ne 1 -or $entry[0].old_hash -ne $file.old_hash -or (Get-FileHash -LiteralPath (Join-Path $directory $file.name)).Hash -ne $file.old_hash) { throw 'Backup does not contain the verified original 1.2.6.0 files.' }
    }
    if ((Get-FileHash -LiteralPath (Join-Path $directory 'libusb0-service.reg')).Hash -ne $record.service_export_sha256) { throw 'Backup service evidence changed.' }
    return $record
}

function Register-UpgradeCatalog([string]$Directory) {
    # Separate name: never remove a catalog installed by another installer.
    $name = 'iOSAudioBridge-libusb0-upgrade-1.4.0.2.cat'
    $source = Join-Path $Directory 'libusb/libusb0.cat'
    $registered = Join-Path $env:SystemRoot "System32/CatRoot/{F750E6C3-38EE-11D1-85E5-00C04FC295EE}/$name"
    Assert-CompatibleFile $registered (Get-FileHash -LiteralPath $source).Hash
    Initialize-KitCatalogApi
    [AudioBridge.DriverKitCatalog]::Register($source, $name)
}

function Remove-UpgradeCatalog([string]$Directory) {
    $name = 'iOSAudioBridge-libusb0-upgrade-1.4.0.2.cat'
    $registered = Join-Path $env:SystemRoot "System32/CatRoot/{F750E6C3-38EE-11D1-85E5-00C04FC295EE}/$name"
    if (Test-Path -LiteralPath $registered) {
        Assert-CompatibleFile $registered (Get-FileHash -LiteralPath (Join-Path $Directory 'libusb/libusb0.cat')).Hash
        Initialize-KitCatalogApi
        [AudioBridge.DriverKitCatalog]::Remove($name)
    }
}

function Invoke-UpgradeTransaction([ValidateSet('Upgrade','Restore')][string]$Mode, [string]$BackupDirectory, $Manifest, [string]$Directory) {
    $files = @(Get-UpgradeFiles $Manifest $Directory)
    $backup = Read-UpgradeBackup $BackupDirectory $files
    $phone = @(Get-UpgradeBackupPhones $backup)
    Assert-UpgradeIdle
    Assert-UpgradeBinding $phone
    Assert-UpgradeDisconnected $phone
    if ($Mode -eq 'Upgrade' -and (Get-UpgradeFileState $files) -ne 'Legacy') { throw 'System files changed after backup; upgrade refused.' }
    $result = [ordered]@{mode=$Mode;started_at=(Get-Date -Format o);success=$false;files_modified=$false;original_restored=$false;reconnect_required=$true}
    $stopped = $false
    try {
        Stop-UpgradeDriver $phone
        $stopped = $true
        if ($Mode -eq 'Upgrade') { Register-UpgradeCatalog $Directory }
        foreach ($file in $files) {
            $source = if ($Mode -eq 'Upgrade') { $file.source } else { Join-Path $BackupDirectory $file.name }
            $hash = if ($Mode -eq 'Upgrade') { $file.new_hash } else { $file.old_hash }
            # Mark before copying so even a failed/partial first copy is restored.
            $result.files_modified = $true
            Copy-UpgradeFile $phone $source $file.destination $hash
        }
        Start-UpgradeDriver $phone
        if ($Mode -eq 'Restore') { Remove-UpgradeCatalog $Directory; $result.original_restored = $true }
        $result.success = $true
        $result.installed_version = if ($Mode -eq 'Upgrade') { '1.4.0.2' } else { '1.2.6.0' }
    } catch {
        $result.error = $_.Exception.Message
        if ($result.files_modified) {
            try {
                # Recheck the backup after a failure before trusting it for recovery.
                $null = Read-UpgradeBackup $BackupDirectory $files
                Stop-UpgradeDriver $phone
                foreach ($file in $files) { Copy-UpgradeFile $phone (Join-Path $BackupDirectory $file.name) $file.destination $file.old_hash }
                Start-UpgradeDriver $phone
                $result.original_restored = $true
                $result.installed_version = '1.2.6.0'
            } catch { $result.rollback_error = $_.Exception.Message }
        } elseif ($stopped) {
            try { Start-UpgradeDriver $phone } catch { $result.restart_error = $_.Exception.Message }
        }
        if (-not $result.files_modified -or $result.original_restored) {
            try { Remove-UpgradeCatalog $Directory } catch { $result.catalog_cleanup_error = $_.Exception.Message }
        }
    } finally {
        $result.finished_at = (Get-Date -Format o)
        $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $BackupDirectory ('result-' + $Mode + '-' + [Guid]::NewGuid().ToString('N') + '.json')) -Encoding UTF8
    }
    return [pscustomobject]$result
}
