param([Parameter(Mandatory=$true)][string]$Installer, [Parameter(Mandatory=$true)][string]$ObsExecutable,
      [string]$RunningPortableRoot = '', [switch]$SkipNormal,
      [ValidateRange(1,1800)][int]$OperationTimeoutSeconds = 120)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lifecycle-process.ps1"
$workspace = (Resolve-Path "$PSScriptRoot/..").Path
$fixture = [IO.Path]::GetFullPath((Join-Path $workspace ('.deps/installer-test-' + [Guid]::NewGuid().ToString('N'))))
if (-not $fixture.StartsWith($workspace + [IO.Path]::DirectorySeparatorChar)) { throw 'Fixture escapes workspace' }
$Installer = (Resolve-Path -LiteralPath $Installer).Path
function Assert([bool]$Value,[string]$Message) { if (-not $Value) { throw $Message }; Write-Lifecycle "PASS: $Message" }
function Install([string]$Root, [int]$ExpectedExitCode = 0) {
    # NSIS /D must be last and consumes spaces without quotes.
    Invoke-LifecycleProcess -FilePath $Installer -Arguments "/S /D=$Root" -Name install -TimeoutSeconds $OperationTimeoutSeconds -ExpectedExitCode $ExpectedExitCode
}
function Uninstall([string]$Root, [switch]$Purge, [int]$ExpectedExitCode = 0) {
    $arguments = if ($Purge) { '/S /PURGESETTINGS' } else { '/S' }
    $null = Invoke-LifecycleProcess -FilePath "$Root/data/obs-plugins/obs-streambridge/uninstall.exe" -Arguments $arguments -Name uninstall -TimeoutSeconds $OperationTimeoutSeconds -ExpectedExitCode $ExpectedExitCode -Uninstaller
}
function New-ObsRoot([string]$Root,[bool]$Portable) {
    New-Item -ItemType Directory -Force "$Root/bin/64bit","$Root/data/obs-plugins/unrelated" | Out-Null
    Copy-Item -LiteralPath $ObsExecutable -Destination "$Root/bin/64bit/obs64.exe"
    Set-Content "$Root/data/obs-plugins/unrelated/preserve.txt" 'unrelated plugin'
    if ($Portable) { New-Item -ItemType File -Path "$Root/portable_mode.txt" | Out-Null }
}
Initialize-Lifecycle $fixture
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
$markerBytes = [IO.File]::ReadAllBytes("$valid/data/obs-plugins/obs-streambridge/install.ini")
Assert ($markerBytes[0] -eq 255 -and $markerBytes[1] -eq 254) 'installation root marker is UTF-16 independent of Windows ANSI code page'
Assert (([Text.Encoding]::Unicode.GetString($markerBytes)) -match [regex]::Escape("Root=$valid")) 'Unicode marker preserves the exact Cyrillic root'
Assert (Test-Path "$valid/obs-plugins/64bit/obs-streambridge.dll") 'plugin DLL installed'
Assert (Test-Path "$valid/data/obs-plugins/obs-streambridge/browser/streambridge-browser.exe") 'browser runtime installed'
$locked = [IO.File]::Open("$valid/bin/64bit/obs64.exe",'Open','Read','Read')
try { Assert ((Install $valid 32) -eq 32) 'busy OBS executable reports file-in-use, not permission error' } finally { $locked.Dispose() }
$lockedPlugin = [IO.File]::Open("$valid/obs-plugins/64bit/obs-streambridge.dll",'Open','Read','Read')
try { Assert ((Install $valid 32) -eq 32) 'busy plugin is rejected before replacement' } finally { $lockedPlugin.Dispose() }
Assert ((Install $valid) -eq 0) 'upgrade succeeds'
Assert (((Get-FileHash "$config/vault.dpapi").Hash) -eq $before) 'upgrade preserves encrypted settings byte for byte'
Uninstall $valid
Assert (-not (Test-Path "$valid/obs-plugins/64bit/obs-streambridge.dll")) 'uninstall removes plugin DLL'
Assert (-not (Test-Path "$valid/data/obs-plugins/obs-streambridge")) 'uninstall removes browser and plugin resources'
Assert (((Get-FileHash "$config/vault.dpapi").Hash) -eq $before) 'default uninstall preserves encrypted settings'
Assert (Test-Path "$valid/data/obs-plugins/unrelated/preserve.txt") 'unrelated OBS plugin is preserved'
Assert (Test-Path "$valid/bin/64bit/obs64.exe") 'OBS executable is preserved'
Assert (((Get-FileHash -LiteralPath $obsFixture.FullName).Hash) -eq $obsHash) 'read-only OBS executable remains byte-identical'
Assert ((Install $valid) -eq 0) 'reinstall succeeds'
Uninstall $valid -Purge
Assert (-not (Test-Path $config)) 'explicit purge removes only StreamBridge configuration'
Assert (Test-Path "$valid/data/obs-plugins/unrelated/preserve.txt") 'purge preserves unrelated plugin'

# Reuse one isolated installation, restore markers after every rejection, and
# compare every other file byte-for-byte. Even /PURGESETTINGS must fail safely.
$markerRoot = Join-Path $fixture 'Маркеры с пробелами\OBS Тест'
New-ObsRoot $markerRoot $true
$null = Install $markerRoot
$markerConfig = "$markerRoot/config/obs-studio/plugin_config/obs-streambridge"
New-Item -ItemType Directory -Force $markerConfig | Out-Null
Set-Content "$markerConfig/vault.dpapi" 'synthetic marker test canary'
$installMarker = "$markerRoot/data/obs-plugins/obs-streambridge/install.ini"
$packageMarker = "$markerRoot/data/obs-plugins/obs-streambridge/package.ini"
$savedInstall = [IO.File]::ReadAllBytes($installMarker)
$savedPackage = [IO.File]::ReadAllBytes($packageMarker)
function Get-PreservedFiles {
    @(Get-ChildItem -LiteralPath $markerRoot -File -Recurse |
        Where-Object { $_.FullName -notin @([IO.Path]::GetFullPath($installMarker),[IO.Path]::GetFullPath($packageMarker)) } |
        Sort-Object FullName | ForEach-Object { $_.FullName + ':' + (Get-FileHash -LiteralPath $_.FullName).Hash }) -join "`n"
}
$preserved = Get-PreservedFiles
foreach ($case in @('missing-install','corrupt-install','empty-root','relative-root','wrong-product','missing-package','corrupt-package','different-root','lossy-ansi-root')) {
    try {
        switch ($case) {
            'missing-install' { Remove-Item -LiteralPath $installMarker }
            'corrupt-install' { Set-Content -LiteralPath $installMarker 'not an INI marker' -Encoding unicode }
            'empty-root' { Set-Content -LiteralPath $installMarker "[Install]`r`nRoot=" -Encoding unicode }
            'relative-root' { Set-Content -LiteralPath $installMarker "[Install]`r`nRoot=..\..\.." -Encoding unicode }
            'wrong-product' { Set-Content -LiteralPath $packageMarker "[StreamBridge]`r`nProduct=foreign-plugin" -Encoding unicode }
            'missing-package' { Remove-Item -LiteralPath $packageMarker }
            'corrupt-package' { Set-Content -LiteralPath $packageMarker 'not an INI marker' -Encoding unicode }
            'different-root' { Set-Content -LiteralPath $installMarker "[Install]`r`nRoot=$valid" -Encoding unicode }
            'lossy-ansi-root' {
                [IO.File]::WriteAllBytes($installMarker, [Text.Encoding]::GetEncoding(1252).GetBytes("[Install]`r`nRoot=$markerRoot`r`n"))
            }
        }
        $watch = [Diagnostics.Stopwatch]::StartNew()
        Uninstall $markerRoot -Purge -ExpectedExitCode 2
        Assert ($watch.Elapsed.TotalSeconds -lt [Math]::Min(15, $OperationTimeoutSeconds)) "$case returns promptly with exit 2"
        Assert ((Get-PreservedFiles) -ceq $preserved) "$case preserves all OBS, plugin and synthetic settings files"
        Assert (Test-Path "$valid/data/obs-plugins/unrelated/preserve.txt") "$case leaves the other installation untouched"
    } finally {
        [IO.File]::WriteAllBytes($installMarker, $savedInstall)
        [IO.File]::WriteAllBytes($packageMarker, $savedPackage)
    }
}
# Equivalent roots should pass normalization without relaxing ownership checks.
Set-Content -LiteralPath $installMarker "[Install]`r`nRoot=$markerRoot\.\" -Encoding unicode
Uninstall $markerRoot
Assert (Test-Path "$markerConfig/vault.dpapi") 'normalized root uninstall preserves settings'
Assert (-not (Test-Path "$markerRoot/data/obs-plugins/obs-streambridge")) 'normalized root uninstall removes owned resources'

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
    Assert ((Install $denied 5) -eq 5) 'denied write permission reports access denied without silent UAC'
    Assert (-not (Test-Path "$denied/obs-plugins/64bit/obs-streambridge.dll")) 'permission failure leaves plugin uninstalled'
} finally {
    Set-Acl -LiteralPath $denied -AclObject $originalAcl
}
$invalid = Join-Path $fixture 'invalid'
New-Item -ItemType Directory -Force $invalid | Out-Null
Assert ((Install $invalid 2) -ne 0) 'non-OBS folder rejected'
$conflict = Join-Path $fixture 'conflict'
New-ObsRoot $conflict $true
New-Item -ItemType Directory -Force "$conflict/obs-plugins/64bit" | Out-Null
Set-Content "$conflict/obs-plugins/64bit/obs-streambridge.dll" 'foreign file'
Assert ((Install $conflict 2) -ne 0) 'unowned conflicting files rejected'
Assert ((Get-Content "$conflict/obs-plugins/64bit/obs-streambridge.dll" -Raw).Trim() -eq 'foreign file') 'conflict is not overwritten'
$wrongArch = Join-Path $fixture 'wrong-architecture'
New-ObsRoot $wrongArch $true
$exeBytes = [IO.File]::ReadAllBytes("$wrongArch/bin/64bit/obs64.exe")
$peOffset = [BitConverter]::ToInt32($exeBytes,60)
$exeBytes[$peOffset + 4] = 0x4c
$exeBytes[$peOffset + 5] = 0x01
[IO.File]::WriteAllBytes("$wrongArch/bin/64bit/obs64.exe",$exeBytes)
Assert ((Install $wrongArch 2) -ne 0) 'x86 PE rejected'
$exeBytes[$peOffset + 4] = 0x64
$exeBytes[$peOffset + 5] = 0xaa
[IO.File]::WriteAllBytes("$wrongArch/bin/64bit/obs64.exe",$exeBytes)
Assert ((Install $wrongArch 2) -ne 0) 'ARM64 PE rejected'
if ($RunningPortableRoot) {
    $running = (Resolve-Path -LiteralPath $RunningPortableRoot).Path
    if (-not $running.StartsWith($workspace + [IO.Path]::DirectorySeparatorChar) -or -not (Test-Path "$running/portable_mode.txt")) { throw 'Running test root must be a workspace portable OBS' }
    $process = Get-Process obs64 -ErrorAction SilentlyContinue | Where-Object Path -EQ "$running\bin\64bit\obs64.exe"
    Assert ([bool]$process) 'test portable OBS is running'
    Assert ((Install $running 32) -ne 0) 'running OBS installation rejected before replacement'
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
    Uninstall $normal
    Assert (-not (Test-Path "$normal/obs-plugins/64bit/obs-streambridge.dll")) 'normal uninstall removes plugin'
    Assert (-not (Test-Path $uninstallKey)) 'normal uninstall removes its registry entry'
    Assert (Test-Path "$normal/data/obs-plugins/unrelated/preserve.txt") 'normal uninstall preserves unrelated plugin'
} finally {
    if ((Test-Path $uninstallKey) -and (Get-ItemProperty $uninstallKey).InstallLocation -eq $normal) {
        Remove-Item -LiteralPath $uninstallKey -Force
    }
}
Write-Output "Installer test fixture retained at $fixture"
