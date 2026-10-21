# SPDX-License-Identifier: GPL-2.0-or-later
param([int]$OperationTimeoutSeconds = 30)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lifecycle-process.ps1"
$workspace = (Resolve-Path "$PSScriptRoot/..").Path
$fixture = Join-Path $workspace ('build/lifecycle-diagnostics/handle-races-' + [Guid]::NewGuid().ToString('N'))
Initialize-Lifecycle $fixture
$watch = [Diagnostics.Stopwatch]::StartNew()
$helper = "$workspace/build/Release/filesystem-test-helper.exe"
$production = "$workspace/build/Release/streambridge-files.exe"
function Assert([bool]$Value, [string]$Message) { if (-not $Value) { throw $Message }; Write-Lifecycle "PASS $Message" }
function Checked([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($fixture + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Not a fixture path: $full" }
    return $full
}
$stage = Checked "$fixture/stage"
New-Item -ItemType Directory -Force "$stage/obs-plugins/64bit","$stage/data/obs-plugins/obs-streambridge/browser/locales" | Out-Null
Set-Content "$stage/obs-plugins/64bit/obs-streambridge.dll" 'OWN DLL'
Set-Content "$stage/data/obs-plugins/obs-streambridge/browser/libcef.dll" 'OWN CEF'
Set-Content "$stage/data/obs-plugins/obs-streambridge/browser/locales/ru.pak" 'OWN LOCALE'
Set-Content "$stage/data/obs-plugins/obs-streambridge/package.ini" "[StreamBridge]`r`nProduct=obs-streambridge`r`nVersion=0.2.5"
& "$PSScriptRoot/installer-payload.ps1" -Stage $stage -Output "$fixture/payload.nsh"
$manifest = "$stage/data/obs-plugins/obs-streambridge/uninstall-manifest.txt"
$savedBarrier = $env:SB_FILESYSTEM_BARRIER
$savedAppData = $env:SB_FILESYSTEM_TEST_APPDATA
try {
    foreach ($case in @('resources','nested-resources','plugin-directory','portable-purge','appdata-purge',
                        'upgrade-resources','upgrade-plugin','upgrade-added-junction')) {
        $caseRoot = Checked "$fixture/$case"
        $root = Checked "$caseRoot/OBS Кириллица с пробелами"
        $resource = "$root/data/obs-plugins/obs-streambridge"
        $configBase = if ($case -eq 'appdata-purge') { "$caseRoot/AppData Тест" } else { "$root/config" }
        $config = "$configBase/obs-studio/plugin_config/obs-streambridge"
        New-Item -ItemType Directory -Force "$root/bin/64bit","$root/data/obs-plugins/foreign",$config | Out-Null
        Copy-Item "$workspace/build/Release/update-host-fixture.exe" "$root/bin/64bit/obs64.exe"
        Set-Content "$root/data/obs-plugins/foreign/keep.txt" 'FOREIGN'
        if ($case -ne 'appdata-purge') { Set-Content "$root/portable_mode.txt" '' }
        $env:SB_FILESYSTEM_TEST_APPDATA = [IO.Path]::GetFullPath($configBase)
        Set-Content "$stage/data/obs-plugins/obs-streambridge/install.ini" "[Install]`r`nRoot=$root" -Encoding unicode
        Set-Content "$stage/data/obs-plugins/obs-streambridge/uninstall.exe" 'SYNTHETIC GENERATED METADATA'
        $env:SB_FILESYSTEM_BARRIER = $null
        $null = Invoke-LifecycleProcess $production ('install "{0}" "{1}" "{2}"' -f $root,$manifest,$stage) "install-$case" $OperationTimeoutSeconds
        Set-Content "$resource/unknown.txt" 'UNKNOWN RESOURCE MUST SURVIVE'
        New-Item -ItemType Directory -Force "$config/updates/update-random" | Out-Null
        Set-Content "$config/vault.dpapi" 'SYNTHETIC SETTINGS'
        Set-Content "$config/updates/update-random/dynamic.tmp" 'SYNTHETIC DYNAMIC SETTINGS'
        $outside = Checked "$caseRoot/outside"
        New-Item -ItemType Directory -Force "$outside/browser/locales","$outside/updates/update-random" | Out-Null
        foreach ($name in @('obs-streambridge.dll','vault.dpapi','browser/libcef.dll','browser/locales/ru.pak','ru.pak','updates/update-random/dynamic.tmp','canary.txt')) {
            Set-Content "$outside/$name" "EXTERNAL CANARY $name"
        }
        $hashes = @{}; foreach ($file in Get-ChildItem $outside -File -Recurse) { $hashes[$file.FullName] = (Get-FileHash $file.FullName).Hash }
        $acls = @{}; foreach ($entry in @((Get-Item $outside)) + @(Get-ChildItem $outside -Recurse)) { $acls[$entry.FullName] = (Get-Acl -LiteralPath $entry.FullName).Sddl }
        $link = Checked $(switch ($case) {
            'resources' { $resource }
            'nested-resources' { "$resource/browser/locales" }
            'plugin-directory' { "$root/obs-plugins/64bit" }
            'upgrade-resources' { $resource }
            'upgrade-plugin' { "$root/obs-plugins/64bit" }
            'upgrade-added-junction' { "$resource/browser/inserted-after-validation" }
            default { $config }
        })
        $retained = Checked "$caseRoot/retained-original"
        $event = 'Local\StreamBridge-race-' + [Guid]::NewGuid().ToString('N')
        $ready = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::ManualReset, "$event-ready")
        $resume = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::ManualReset, "$event-resume")
        $env:SB_FILESYSTEM_BARRIER = $event
        $operation = $null; $linked = $false
        try {
            $purge = if ($case -like '*purge') { 'purge' } else { '' }
            $command = if ($case -like 'upgrade-*') { 'install "{0}" "{1}" "{2}"' -f $root,$manifest,$stage }
                       else { 'uninstall "{0}" "{1}" {2}' -f $root,$manifest,$purge }
            $operation = Start-LifecycleProcess $helper $command "race-$case" $OperationTimeoutSeconds
            Assert ($ready.WaitOne(8000)) "$case reached barrier AFTER complete validation and BEFORE deletion"
            if ($case -ne 'upgrade-added-junction') { Move-Item -LiteralPath $link -Destination $retained }
            New-Item -ItemType Junction -Path $link -Target $outside | Out-Null
            $linked = $true
            Write-Lifecycle "SWAP $case actual junction created path=$link target=$outside"
            $null = $resume.Set()
            $operation.Job.Wait()
            $actual = $operation.Job.ExitCode
            # Windows can reject disposition of a directory whose ancestor was renamed.
            # A bounded access-denied refusal is allowed; path-based fallback is forbidden.
            Assert ($actual -in @(0,5)) "$case terminal code=$actual (completed or safe Windows refusal)"
            $null = Complete-LifecycleProcess $operation -ExpectedExitCode $actual
            foreach ($file in $hashes.Keys) { Assert ((Test-Path -LiteralPath $file) -and (Get-FileHash -LiteralPath $file).Hash -eq $hashes[$file]) "$case external byte-identical: $([IO.Path]::GetRelativePath($outside,$file))" }
            foreach ($entry in $acls.Keys) { Assert ((Get-Acl -LiteralPath $entry).Sddl -eq $acls[$entry]) "$case external ACL identical: $([IO.Path]::GetRelativePath($outside,$entry))" }
            Assert (Test-Path "$root/bin/64bit/obs64.exe") "$case OBS preserved"
            Assert ((Get-Content "$root/data/obs-plugins/foreign/keep.txt" -Raw).Trim() -eq 'FOREIGN') "$case foreign plugin preserved"
            $unknown = if ($case -in @('resources','upgrade-resources')) { "$retained/unknown.txt" } else { "$resource/unknown.txt" }
            Assert ((Get-Content $unknown -Raw).Trim() -eq 'UNKNOWN RESOURCE MUST SURVIVE') "$case unknown resource preserved"
            if ($case -like '*purge') {
                Assert (-not (Test-Path "$retained/updates/update-random/dynamic.tmp")) "$case original dynamic file removed by handle"
                if ($actual -eq 0) { Assert (-not (Test-Path "$retained/vault.dpapi")) "$case completed purge removed original settings" }
            }
            else { Assert (Test-Path "$config/vault.dpapi") "$case default uninstall preserves settings" }
        } finally {
            $null = $resume.Set()
            if ($operation) { Stop-LifecycleProcess $operation }
            if ($linked) { [IO.Directory]::Delete((Checked $link)) } # unlink only, never traverse target
            $ready.Dispose(); $resume.Dispose()
            $env:SB_FILESYSTEM_BARRIER = $null
        }
    }
    Write-Lifecycle ('SUITE END synchronized-races status=PASS duration={0:F3}s' -f $watch.Elapsed.TotalSeconds)
} finally { $env:SB_FILESYSTEM_BARRIER = $savedBarrier; $env:SB_FILESYSTEM_TEST_APPDATA = $savedAppData }
