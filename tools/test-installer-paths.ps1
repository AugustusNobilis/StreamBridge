param([switch]$IsolatedNormal, [int]$OperationTimeoutSeconds = 30)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lifecycle-process.ps1"
$workspace = (Resolve-Path "$PSScriptRoot/..").Path
$fixture = Join-Path $workspace ('build/lifecycle-diagnostics/path-safety-' + [Guid]::NewGuid().ToString('N'))
Initialize-Lifecycle $fixture
$watch = [Diagnostics.Stopwatch]::StartNew()
function Assert([bool]$Value, [string]$Message) {
    if (-not $Value) { throw $Message }; Write-Lifecycle "PASS $Message"
}
function Fixture-Path([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($fixture + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Not a fixture path: $full" }
    return $full
}
# Minimal data, but the actual production NSIS code and normal solid compression.
$source = Join-Path $fixture 'synthetic.txt'
Set-Content $source 'SYNTHETIC PAYLOAD ONLY'
$package = Join-Path $fixture 'package.ini'
Set-Content $package "[StreamBridge]`r`nProduct=obs-streambridge`r`nVersion=0.2.5" -Encoding ascii
$payload = Join-Path $fixture 'payload.nsh'
$stage = Join-Path $fixture 'stage'
New-Item -ItemType Directory -Force "$stage/obs-plugins/64bit","$stage/data/obs-plugins/obs-streambridge/browser" | Out-Null
Copy-Item $source "$stage/obs-plugins/64bit/obs-streambridge.dll"
foreach ($name in @('streambridge-browser.exe','streambridge-browser.dll','libcef.dll')) { Copy-Item $source "$stage/data/obs-plugins/obs-streambridge/browser/$name" }
Copy-Item $package "$stage/data/obs-plugins/obs-streambridge/package.ini"
& "$PSScriptRoot/installer-payload.ps1" -Stage $stage -Output $payload
$installer = Join-Path $fixture 'setup.exe'
$null = Invoke-LifecycleProcess "$workspace/.deps/nsis-3.11/makensis.exe" ('/V2 /DVERSION=0.2.5 "/DPAYLOAD={0}" "/DOUTPUT={1}" "{2}"' -f $payload,$installer,"$workspace/installer/streambridge.nsi") compile 30
function Install([string]$Root, [int]$Expected = 0) {
    $null = Invoke-LifecycleProcess $installer "/S /D=$Root" install $OperationTimeoutSeconds $Expected
}
function Uninstall([string]$Root, [int]$Expected = 0, [switch]$Purge) {
    $arguments = '/S'; if ($Purge) { $arguments += ' /PURGESETTINGS' }
    $null = Invoke-LifecycleProcess "$Root/data/obs-plugins/obs-streambridge/uninstall.exe" $arguments uninstall $OperationTimeoutSeconds $Expected -Uninstaller
}
function New-Root([string]$Name, [switch]$Normal) {
    $root = Fixture-Path "$fixture/$Name/OBS Кириллица с пробелами"
    New-Item -ItemType Directory -Force "$root/bin/64bit","$root/data/obs-plugins/other","$root/config/obs-studio/plugin_config/obs-streambridge" | Out-Null
    Copy-Item "$workspace/build/Release/update-host-fixture.exe" "$root/bin/64bit/obs64.exe"
    Set-Content "$root/data/obs-plugins/other/keep.txt" 'FOREIGN PLUGIN'
    Set-Content "$root/config/obs-studio/plugin_config/obs-streambridge/vault.dpapi" 'SYNTHETIC SETTINGS'
    if (-not $Normal) { Set-Content "$root/portable_mode.txt" '' }
    Install $root
    return $root
}
function Snapshot([string]$Path) {
    # Explicitly do not follow reparse points, even during test verification.
    $rows = @()
    foreach ($entry in Get-ChildItem -LiteralPath $Path -Force) {
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
        if ($entry.PSIsContainer) { $rows += Snapshot $entry.FullName }
        else { $rows += $entry.FullName + ':' + (Get-FileHash -LiteralPath $entry.FullName).Hash }
    }
    return $rows | Sort-Object
}
$unknownRoot = New-Root 'unknown-resources'
$unknownResource = "$unknownRoot/data/obs-plugins/obs-streambridge/user-added.txt"
Set-Content $unknownResource 'UNKNOWN RESOURCE'
$unknownHash = (Get-FileHash $unknownResource).Hash
# Editing the installed manifest must not expand the embedded uninstall allowlist.
Add-Content "$unknownRoot/data/obs-plugins/obs-streambridge/uninstall-manifest.txt" "data\obs-plugins\other\keep.txt`r`ndata\obs-plugins\obs-streambridge\user-added.txt"
Uninstall $unknownRoot
Assert ((Get-FileHash $unknownResource).Hash -eq $unknownHash) 'unknown resource preserved despite tampered installed manifest'
Assert (Test-Path "$unknownRoot/data/obs-plugins/other/keep.txt") 'tampered installed manifest cannot expand ownership'
Assert (Test-Path "$unknownRoot/config/obs-studio/plugin_config/obs-streambridge/vault.dpapi") 'unknown resource case preserves settings by default'
foreach ($case in @('dll-directory','resources','ancestor','root','nested','portable-purge','dangling','file-symlink','directory-symlink')) {
    $root = New-Root $case
    $caseRoot = Split-Path $root
    $outside = Fixture-Path "$caseRoot/outside"
    New-Item -ItemType Directory $outside | Out-Null
    Set-Content "$outside/canary.txt" 'EXTERNAL CANARY'
    $link = switch ($case) {
        'dll-directory' { "$root/obs-plugins/64bit" }
        'resources' { "$root/data/obs-plugins/obs-streambridge" }
        'ancestor' { "$root/data" }
        'root' { $root }
        'nested' { "$root/data/obs-plugins/obs-streambridge/browser/nested" }
        'portable-purge' { "$root/config/obs-studio/plugin_config" }
        'dangling' { "$root/obs-plugins/64bit" }
        'file-symlink' { "$root/obs-plugins/64bit/obs-streambridge.dll" }
        'directory-symlink' { "$root/obs-plugins/64bit" }
    }
    $link = Fixture-Path $link
    $target = Fixture-Path "$outside/target"
    $moved = Test-Path -LiteralPath $link
    if ($moved) { Move-Item -LiteralPath $link -Destination $target }
    else { New-Item -ItemType Directory $target | Out-Null; Set-Content "$target/nested.txt" 'NESTED EXTERNAL CANARY' }
    $linkCreated = $false
    try {
        $kind = if ($case -like '*symlink') { 'SymbolicLink' } else { 'Junction' }
        try { New-Item -ItemType $kind -Path $link -Target $target | Out-Null; $linkCreated = $true }
        catch {
            if ($kind -ne 'SymbolicLink') { throw }
            Write-Lifecycle "SKIP $case creation unavailable: $($_.Exception.Message)"
            continue
        }
        if ($case -eq 'dangling') {
            Move-Item -LiteralPath $target -Destination (Fixture-Path "$outside/retained-target")
        }
        $before = (Snapshot $caseRoot) -join "`n"
        # Test the native boundary independently of NSIS's early preflight.
        $null = Invoke-LifecycleProcess "$workspace/build/Release/streambridge-files.exe" ('uninstall "{0}" "{1}" purge' -f $root,"$stage/data/obs-plugins/obs-streambridge/uninstall-manifest.txt") "native-reject-$case" $OperationTimeoutSeconds 87
        # No settings writes in install/upgrade, so only purge must inspect config.
        if ($case -ne 'portable-purge') { Install $root 87 }
        Uninstall $root 87 -Purge
        Assert (((Snapshot $caseRoot) -join "`n") -ceq $before) "$case rejects before changing OBS, plugin, settings or outside hashes"
    } finally {
        if ($linkCreated) {
            # Remove only the link itself; never recursive deletion of its target.
            if ($case -eq 'file-symlink') { [IO.File]::Delete($link) }
            else { [IO.Directory]::Delete($link) }
        }
        if ($case -eq 'dangling') { Move-Item -LiteralPath (Fixture-Path "$outside/retained-target") -Destination $target }
        $outsideBeforeCleanup = (Snapshot $outside) -join "`n"
        if ($moved) {
            # Copy back for normal uninstall. Preserve the external target so its
            # hashes can also be verified after link removal and owned cleanup.
            Copy-Item -LiteralPath $target -Destination $link -Recurse
        }
        Uninstall $root -Purge
        Assert (((Snapshot $outside) -join "`n") -ceq $outsideBeforeCleanup) "$case outside files survive link cleanup and normal uninstall"
        Assert (Test-Path "$root/bin/64bit/obs64.exe") "$case OBS survives cleanup"
        Assert (Test-Path "$root/data/obs-plugins/other/keep.txt") "$case other plugin survives cleanup"
    }
}
# Malformed component: a file where a directory is required.
$root = New-Root damaged-component
$dir = Fixture-Path "$root/obs-plugins/64bit"
$saved = Fixture-Path "$(Split-Path $root)/saved-directory"
Move-Item -LiteralPath $dir -Destination $saved
Set-Content $dir 'NOT A DIRECTORY'
$before = (Snapshot (Split-Path $root)) -join "`n"
try {
    Install $root 87
    Uninstall $root 87 -Purge
    Assert (((Snapshot (Split-Path $root)) -join "`n") -ceq $before) 'damaged component fails without changes'
} finally {
    Remove-Item -LiteralPath $dir
    Move-Item -LiteralPath $saved -Destination $dir
    Uninstall $root -Purge
}
# Deny enumeration only on a new synthetic subtree, restore the ACL in finally.
$root = New-Root inaccessible-tree
$denied = Fixture-Path "$root/data/obs-plugins/obs-streambridge/denied"
New-Item -ItemType Directory $denied | Out-Null
Set-Content "$denied/canary.txt" 'DENIED SYNTHETIC FILE'
$before = (Snapshot $root) -join "`n"
$acl = Get-Acl $denied
$deny = Get-Acl $denied
$deny.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.WindowsIdentity]::GetCurrent().User,'ListDirectory','Deny'))
try {
    Set-Acl $denied $deny
    Install $root 5
    Uninstall $root 5 -Purge
} finally { Set-Acl $denied $acl }
Assert (((Snapshot $root) -join "`n") -ceq $before) 'inaccessible subtree fails before any mutation'
Uninstall $root -Purge

if ($IsolatedNormal) {
    if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') { throw 'AppData test requires GitHub-hosted isolated user' }
    $config = Join-Path $env:APPDATA 'obs-studio/plugin_config/obs-streambridge'
    $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\OBSStreamBridge'
    if ((Test-Path $config) -or (Test-Path $key)) { throw 'Refusing to touch existing user settings/registration' }
    $root = New-Root normal-appdata -Normal
    $external = Fixture-Path "$(Split-Path $root)/outside-settings"
    New-Item -ItemType Directory -Force $external,(Split-Path $config) | Out-Null
    Set-Content "$external/vault.dpapi" 'SYNTHETIC APPDATA SETTINGS'
    $before = (Snapshot $external) -join "`n"
    $linked = $false
    try {
        New-Item -ItemType Junction -Path $config -Target $external | Out-Null
        $linked = $true
        $obsBefore = (Snapshot $root) -join "`n"
        $null = Invoke-LifecycleProcess "$workspace/build/Release/streambridge-files.exe" ('uninstall "{0}" "{1}" purge' -f $root,"$stage/data/obs-plugins/obs-streambridge/uninstall-manifest.txt") native-reject-appdata $OperationTimeoutSeconds 87
        Uninstall $root 87 -Purge
        Assert (((Snapshot $root) -join "`n") -ceq $obsBefore) 'AppData reparse rejection happens before plugin deletion'
        [IO.Directory]::Delete($config); $linked = $false
        Copy-Item -LiteralPath $external -Destination $config -Recurse
        Uninstall $root -Purge
        Assert (-not (Test-Path $config)) 'ordinary isolated AppData purge removes only own settings'
        Assert (((Snapshot $external) -join "`n") -ceq $before) 'AppData external target survives rejection and cleanup'
        Assert (-not (Test-Path $key)) 'normal cleanup removes own registration'
    } finally {
        if ($linked) { [IO.Directory]::Delete($config) }
        # Only this explicitly created settings directory on the isolated runner.
        if (Test-Path $config) {
            if ((Get-Item $config).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unexpected redirected cleanup path' }
            Remove-Item -LiteralPath $config -Recurse
        }
        if ((Test-Path $key) -and (Get-ItemProperty $key).InstallLocation -eq $root) { Remove-Item -LiteralPath $key }
    }
} else { Write-Lifecycle 'SKIP isolated AppData/registry test: local profile is not an isolated runner' }
# A failed helper launch must be a failure, not nsExec's string "error" coerced to exit 0.
$badPayload = Join-Path $fixture 'bad-helper-payload.nsh'
& "$PSScriptRoot/installer-payload.ps1" -Stage $stage -Output $badPayload -Helper $source
$badInstaller = Join-Path $fixture 'bad-helper-setup.exe'
$null = Invoke-LifecycleProcess "$workspace/.deps/nsis-3.11/makensis.exe" ('/V2 /DVERSION=0.2.5 "/DPAYLOAD={0}" "/DOUTPUT={1}" "{2}"' -f $badPayload,$badInstaller,"$workspace/installer/streambridge.nsi") compile-bad-helper 30
$root = New-Root helper-launch-failure
$before = (Snapshot $root) -join "`n"
$null = Invoke-LifecycleProcess $badInstaller "/S /D=$root" reject-helper-launch $OperationTimeoutSeconds 3
Assert (((Snapshot $root) -join "`n") -ceq $before) 'failed helper launch returns 3 without changing installed files'
Uninstall $root -Purge
Write-Lifecycle ('SUITE END path-safety status=PASS duration={0:F3}s' -f $watch.Elapsed.TotalSeconds)
