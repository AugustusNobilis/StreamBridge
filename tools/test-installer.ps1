param([Parameter(Mandatory=$true)][string]$Installer, [Parameter(Mandatory=$true)][string]$ObsExecutable,
      [string]$RunningPortableRoot = '', [switch]$SkipNormal)
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path "$PSScriptRoot/..").Path
$fixture = [IO.Path]::GetFullPath((Join-Path $workspace ('.deps/installer-test-' + [Guid]::NewGuid().ToString('N'))))
if (-not $fixture.StartsWith($workspace + [IO.Path]::DirectorySeparatorChar)) { throw 'Fixture escapes workspace' }
$Installer = (Resolve-Path -LiteralPath $Installer).Path
function Assert([bool]$Value,[string]$Message) { if (-not $Value) { throw $Message }; Write-Output "PASS: $Message" }
function Install([string]$Root) {
    # NSIS /D must be last and consumes the rest of the command line (including spaces).
    $process = Start-Process -FilePath $Installer -ArgumentList "/S /D=$Root" -WindowStyle Hidden -PassThru -Wait
    Write-Host "Installer exit: $($process.ExitCode)"
    return $process.ExitCode
}
function New-ObsRoot([string]$Root,[bool]$Portable) {
    New-Item -ItemType Directory -Force "$Root/bin/64bit","$Root/data/obs-plugins/unrelated" | Out-Null
    Copy-Item -LiteralPath $ObsExecutable -Destination "$Root/bin/64bit/obs64.exe"
    Set-Content "$Root/data/obs-plugins/unrelated/preserve.txt" 'unrelated plugin'
    if ($Portable) { New-Item -ItemType File -Path "$Root/portable_mode.txt" | Out-Null }
}
New-Item -ItemType Directory -Force $fixture | Out-Null
$valid = Join-Path $fixture 'Файлы программ\OBS Тест'
New-ObsRoot $valid $true
# A readable OBS executable does not need to be writable by the plugin installer.
$obsFixture = Get-Item -LiteralPath "$valid/bin/64bit/obs64.exe"
$obsFixture.IsReadOnly = $true
$obsHash = (Get-FileHash -LiteralPath $obsFixture.FullName).Hash
$config = "$valid/config/obs-studio/plugin_config/obs-streambridge"
New-Item -ItemType Directory -Force $config | Out-Null
Set-Content "$config/settings.json" '{"installerTestCanary":true}'
Set-Content "$config/vault.dpapi" 'synthetic vault canary; no user credentials'
$before = (Get-FileHash "$config/vault.dpapi").Hash
Assert ((Install $valid) -eq 0) 'portable installation with Cyrillic, spaces and read-only OBS succeeds'
Assert (Test-Path "$valid/obs-plugins/64bit/obs-streambridge.dll") 'plugin DLL installed'
Assert (Test-Path "$valid/data/obs-plugins/obs-streambridge/browser/streambridge-browser.exe") 'browser runtime installed'
$locked = [IO.File]::Open("$valid/bin/64bit/obs64.exe",'Open','Read','Read')
try { Assert ((Install $valid) -eq 32) 'busy OBS executable reports file-in-use, not permission error' } finally { $locked.Dispose() }
$lockedPlugin = [IO.File]::Open("$valid/obs-plugins/64bit/obs-streambridge.dll",'Open','Read','Read')
try { Assert ((Install $valid) -eq 32) 'busy plugin is rejected before replacement' } finally { $lockedPlugin.Dispose() }
Assert ((Install $valid) -eq 0) 'upgrade succeeds'
Assert (((Get-FileHash "$config/vault.dpapi").Hash) -eq $before) 'upgrade preserves encrypted settings byte for byte'
$uninstaller = "$valid/data/obs-plugins/obs-streambridge/uninstall.exe"
Start-Process -FilePath $uninstaller -ArgumentList '/S' -WindowStyle Hidden -Wait
Assert (-not (Test-Path "$valid/obs-plugins/64bit/obs-streambridge.dll")) 'uninstall removes plugin DLL'
Assert (-not (Test-Path "$valid/data/obs-plugins/obs-streambridge")) 'uninstall removes browser and plugin resources'
Assert (((Get-FileHash "$config/vault.dpapi").Hash) -eq $before) 'default uninstall preserves encrypted settings'
Assert (Test-Path "$valid/data/obs-plugins/unrelated/preserve.txt") 'unrelated OBS plugin is preserved'
Assert (Test-Path "$valid/bin/64bit/obs64.exe") 'OBS executable is preserved'
Assert (((Get-FileHash -LiteralPath $obsFixture.FullName).Hash) -eq $obsHash) 'read-only OBS executable remains byte-identical'
Assert ((Install $valid) -eq 0) 'reinstall succeeds'
Start-Process -FilePath "$valid/data/obs-plugins/obs-streambridge/uninstall.exe" -ArgumentList '/S /PURGESETTINGS' -WindowStyle Hidden -Wait
Assert (-not (Test-Path $config)) 'explicit purge removes only StreamBridge configuration'
Assert (Test-Path "$valid/data/obs-plugins/unrelated/preserve.txt") 'purge preserves unrelated plugin'
$denied = Join-Path $fixture 'protected-folder'
New-ObsRoot $denied $true
$originalAcl = Get-Acl -LiteralPath $denied
$deniedAcl = Get-Acl -LiteralPath $denied
$sid = [Security.Principal.WindowsIdentity]::GetCurrent().User
$denyWrite = [Security.AccessControl.FileSystemAccessRule]::new($sid, 'Write', 'ContainerInherit,ObjectInherit', 'None', 'Deny')
$deniedAcl.AddAccessRule($denyWrite)
try {
    # Only this newly created fixture gets a temporary deny rule, never real OBS.
    Set-Acl -LiteralPath $denied -AclObject $deniedAcl
    Assert ((Install $denied) -eq 5) 'denied write permission reports access denied without silent UAC'
    Assert (-not (Test-Path "$denied/obs-plugins/64bit/obs-streambridge.dll")) 'permission failure leaves plugin uninstalled'
} finally {
    Set-Acl -LiteralPath $denied -AclObject $originalAcl
}
$invalid = Join-Path $fixture 'invalid'
New-Item -ItemType Directory -Force $invalid | Out-Null
Assert ((Install $invalid) -ne 0) 'non-OBS folder rejected'
$conflict = Join-Path $fixture 'conflict'
New-ObsRoot $conflict $true
New-Item -ItemType Directory -Force "$conflict/obs-plugins/64bit" | Out-Null
Set-Content "$conflict/obs-plugins/64bit/obs-streambridge.dll" 'foreign file'
Assert ((Install $conflict) -ne 0) 'unowned conflicting files rejected'
Assert ((Get-Content "$conflict/obs-plugins/64bit/obs-streambridge.dll" -Raw).Trim() -eq 'foreign file') 'conflict is not overwritten'
$wrongArch = Join-Path $fixture 'wrong-architecture'
New-ObsRoot $wrongArch $true
$exeBytes = [IO.File]::ReadAllBytes("$wrongArch/bin/64bit/obs64.exe")
$peOffset = [BitConverter]::ToInt32($exeBytes,60)
$exeBytes[$peOffset + 4] = 0x4c
$exeBytes[$peOffset + 5] = 0x01
[IO.File]::WriteAllBytes("$wrongArch/bin/64bit/obs64.exe",$exeBytes)
Assert ((Install $wrongArch) -ne 0) 'x86 PE rejected'
$exeBytes[$peOffset + 4] = 0x64
$exeBytes[$peOffset + 5] = 0xaa
[IO.File]::WriteAllBytes("$wrongArch/bin/64bit/obs64.exe",$exeBytes)
Assert ((Install $wrongArch) -ne 0) 'ARM64 PE rejected'
if ($RunningPortableRoot) {
    $running = (Resolve-Path -LiteralPath $RunningPortableRoot).Path
    if (-not $running.StartsWith($workspace + [IO.Path]::DirectorySeparatorChar) -or -not (Test-Path "$running/portable_mode.txt")) { throw 'Running test root must be a workspace portable OBS' }
    $process = Get-Process obs64 -ErrorAction SilentlyContinue | Where-Object Path -EQ "$running\bin\64bit\obs64.exe"
    Assert ([bool]$process) 'test portable OBS is running'
    Assert ((Install $running) -ne 0) 'running OBS installation rejected before replacement'
}
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\OBSStreamBridge'
if ($SkipNormal) {
    Write-Output 'SKIP: normal registry lifecycle (existing user installation remains untouched)'
    Write-Output "Installer test fixture retained at $fixture"
    return
}
if (Test-Path $uninstallKey) { throw 'Existing normal installation registry entry: normal test requires an isolated user with no StreamBridge entry' }
$normal = Join-Path $fixture 'normal'
New-ObsRoot $normal $false
try {
    Assert ((Install $normal) -eq 0) 'normal installation succeeds'
    Assert ((Get-ItemProperty $uninstallKey).InstallLocation -eq $normal) 'normal installation registers its own uninstaller'
    Assert ((Install $normal) -eq 0) 'normal upgrade succeeds'
    Start-Process -FilePath "$normal/data/obs-plugins/obs-streambridge/uninstall.exe" -ArgumentList '/S' -WindowStyle Hidden -Wait
    Assert (-not (Test-Path "$normal/obs-plugins/64bit/obs-streambridge.dll")) 'normal uninstall removes plugin'
    Assert (-not (Test-Path $uninstallKey)) 'normal uninstall removes its registry entry'
    Assert (Test-Path "$normal/data/obs-plugins/unrelated/preserve.txt") 'normal uninstall preserves unrelated plugin'
} finally {
    if ((Test-Path $uninstallKey) -and (Get-ItemProperty $uninstallKey).InstallLocation -eq $normal) {
        Remove-Item -LiteralPath $uninstallKey -Force
    }
}
Write-Output "Installer test fixture retained at $fixture"
