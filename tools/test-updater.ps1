param([Parameter(Mandatory=$true)][string]$Installer,
      [string]$HostExecutable = "$PSScriptRoot/../build/Release/update-host-fixture.exe",
      [switch]$ImmediateExit, [switch]$RestartBeforeInstall,
      [switch]$AutoRestart, [switch]$CancelRestart, [switch]$RejectRestart,
      [ValidateRange(1,1800)][int]$OperationTimeoutSeconds = 120)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lifecycle-process.ps1"
$workspace = (Resolve-Path "$PSScriptRoot/..").Path
$fixture = [IO.Path]::GetFullPath((Join-Path $workspace ('.deps/updater-test-' + [Guid]::NewGuid().ToString('N'))))
if (-not $fixture.StartsWith($workspace + [IO.Path]::DirectorySeparatorChar)) { throw 'Fixture escapes workspace' }
Initialize-Lifecycle $fixture
$operations = [Collections.Generic.List[object]]::new()
$testSucceeded = $false
$originalPath = $env:PATH
$root = Join-Path $fixture 'OBS Обновление'
$download = Join-Path $fixture 'download'
New-Item -ItemType Directory -Force "$root/bin/64bit","$root/obs-plugins/64bit","$root/data/obs-plugins/obs-streambridge",$download | Out-Null
Copy-Item -LiteralPath (Resolve-Path $Installer).Path -Destination "$download/setup.exe"
Copy-Item -LiteralPath (Resolve-Path $HostExecutable).Path -Destination "$root/bin/64bit/obs64.exe"
Copy-Item -LiteralPath "$workspace/build/Release/streambridge-update-helper.exe" -Destination "$root/data/obs-plugins/obs-streambridge"
New-Item -ItemType File "$root/portable_mode.txt" | Out-Null
Set-Content "$root/data/obs-plugins/obs-streambridge/package.ini" @('[StreamBridge]','Product=obs-streambridge','Version=0.1.0')
Set-Content "$root/obs-plugins/64bit/obs-streambridge.dll" 'old plugin canary'
$config = "$root/config/obs-studio/plugin_config/obs-streambridge"
New-Item -ItemType Directory -Force $config | Out-Null
Set-Content "$config/vault.dpapi" 'synthetic credential canary'
$before = (Get-FileHash "$config/vault.dpapi").Hash
$oldPlugin = (Get-FileHash "$root/obs-plugins/64bit/obs-streambridge.dll").Hash
function Assert([bool]$value, [string]$message) { if (-not $value) { throw $message }; Write-Lifecycle "PASS: $message" }
function Wait-Until([scriptblock]$condition, [string]$message, [int]$seconds = 30) {
    $deadline = [DateTime]::UtcNow.AddSeconds([Math]::Min($seconds, $OperationTimeoutSeconds))
    while ([DateTime]::UtcNow -lt $deadline) {
        try { if (& $condition) { return } } catch [IO.IOException] { }
        Start-Sleep -Milliseconds 100
    }
    Write-Lifecycle "TIMEOUT condition: $message"
    throw $message
}
$env:PATH = (Resolve-Path "$workspace/.deps/obs-deps-qt6-2025-08-23-x64/bin").Path + ';' + $env:PATH
$control = "$fixture/control.txt"
$result = "$fixture/launch.txt"
try {
    $hostOperation = Start-LifecycleProcess -FilePath "$root/bin/64bit/obs64.exe" -Arguments ('"{0}" "{1}" "{2}"' -f "$download/setup.exe",$control,$result) -Name host-and-update -TimeoutSeconds $OperationTimeoutSeconds
    $operations.Add($hostOperation)
    # Exact paths in this unique fixture + job membership + retained process
    # handles identify roles. Protocol status alone cannot establish success.
    $hostOperation.Roles = @(
        @{ Name='host'; Path="$root/bin/64bit/obs64.exe"; Count=1; ExitCode=0 },
        @{ Name='installer'; Path="$download/setup.exe"; Count=1; ExitCode=0 }
    )
    if ($RestartBeforeInstall) { $hostOperation.Roles[1].Count = 0 }
    if ($CancelRestart) { $hostOperation.Roles[1].ExitCode = 1223 }
    if ($AutoRestart) { $hostOperation.Roles[0].Count = 2 }
    $hostProcess = $hostOperation.Process
    Start-Sleep -Milliseconds 300
    Assert (-not $hostProcess.HasExited) 'isolated host fixture starts'
    if ($AutoRestart -or $CancelRestart -or $RejectRestart) {
        $targetVersion = [regex]::Match((Get-Content "$workspace/CMakeLists.txt" -Raw), 'project\(obs-streambridge VERSION ([0-9.]+)').Groups[1].Value
        if ($RejectRestart) { $targetVersion = '99.0.0' }
        Set-Content -LiteralPath $control "restart:$targetVersion"
        Wait-Until { Test-Path -LiteralPath $result } 'Restart launcher did not report'
        Assert ((Get-Content -LiteralPath $result -Raw).Trim() -eq 'launched') 'restart helper and installer launched'
        $helpers = @(Get-ChildItem -LiteralPath $download -Filter 'restart-*.exe' -File)
        Assert ($helpers.Count -eq 1) 'one restart helper artifact in this unique download directory'
        Assert ((Get-FileHash $helpers[0].FullName).Hash -eq (Get-FileHash "$workspace/build/Release/streambridge-update-helper.exe").Hash) 'restart helper identity matches the built helper'
        $hostOperation.Roles += @{ Name='restart-helper'; Path=$helpers[0].FullName; Count=1; ExitCode=$(if ($RejectRestart) { 3 } else { 0 }) }
        Wait-Until { (Test-Path "$download/update-result.ini") -and (Get-Content "$download/update-result.ini" -Raw) -match 'Status=waiting' } 'Installer did not wait'
        Assert (((Get-FileHash "$root/obs-plugins/64bit/obs-streambridge.dll").Hash) -eq $oldPlugin) 'no replacement before OBS exit'
        if ($CancelRestart) {
            Set-Content -LiteralPath $control 'cancel'
            Wait-Until { (Test-Path "$download/restart-result.ini") -and (Get-Content "$download/restart-result.ini" -Raw) -match 'Status=cancelled' } 'Restart did not cancel'
            Wait-Until { (Get-Content "$download/update-result.ini" -Raw) -match 'Status=cancelled' } 'Installer did not cancel'
            Assert (-not $hostProcess.HasExited) 'cancelled shutdown leaves original OBS running'
            Assert (((Get-FileHash "$root/obs-plugins/64bit/obs-streambridge.dll").Hash) -eq $oldPlugin) 'cancelled update leaves plugin intact'
            Assert (-not (Test-Path "$root/bin/64bit/restarted.txt")) 'cancellation does not launch second OBS'
        } else {
            Set-Content -LiteralPath $control 'exit'
            Assert ($hostProcess.WaitForExit(10000)) 'original OBS exits normally'
            if ($RejectRestart) {
                Wait-Until { (Test-Path "$download/restart-result.ini") -and (Get-Content "$download/restart-result.ini" -Raw) -match 'Status=failed' } 'Mismatched installation was not rejected' 90
                Assert (-not (Test-Path "$root/bin/64bit/restarted.txt")) 'mismatched installed version prevents automatic OBS launch'
                $testSucceeded = $true
                Write-Output "Updater test fixture retained at $fixture"
                return
            }
            Wait-Until { Test-Path "$root/bin/64bit/restarted.txt" } 'OBS did not automatically restart' 90
            Assert ((Get-Content "$download/update-result.ini" -Raw) -match 'Status=success') 'restart follows successful installation'
            Assert ((Get-Content "$root/bin/64bit/restarted.txt" -Raw).Replace('\','/').StartsWith($root.Replace('\','/') + '/bin/64bit')) 'restarted OBS uses its binary working directory'
            Assert (((Get-FileHash "$config/vault.dpapi").Hash) -eq $before) 'automatic restart preserves credentials'
        }
        $testSucceeded = $true
        Write-Output "Updater test fixture retained at $fixture"
        return
    }
    if ($RestartBeforeInstall) {
        $oldPid = $hostProcess.Id
        $oldCreated = $hostProcess.StartTime.ToUniversalTime().ToFileTimeUtc()
        Set-Content -LiteralPath $control 'exit'
        Assert ($hostProcess.WaitForExit(10000)) 'original host exits before installer launch'
        Set-Content -LiteralPath $control 'idle'
        $hostOperation = Start-LifecycleProcess -FilePath "$root/bin/64bit/obs64.exe" -Arguments ('"{0}" "{1}" "{2}"' -f "$download/setup.exe",$control,$result) -Name host-and-update -TimeoutSeconds $OperationTimeoutSeconds
        $operations.Add($hostOperation)
        $hostOperation.Roles = @(@{ Name='restarted-host'; Path="$root/bin/64bit/obs64.exe"; Count=1; ExitCode=0 })
        $hostProcess = $hostOperation.Process
        $installerOperation = Start-LifecycleProcess -FilePath "$download/setup.exe" -Arguments "/S /UPDATEPID=$oldPid /UPDATECREATED=$oldCreated /D=$root" -Name restarted-host-installer -TimeoutSeconds $OperationTimeoutSeconds
        $operations.Add($installerOperation)
        $installerOperation.Roles = @(@{ Name='installer'; Path="$download/setup.exe"; Count=1; ExitCode=0 })
        $installerProcess = $installerOperation.Process
        Start-Sleep -Seconds 6
        Assert (-not $hostProcess.HasExited) 'restarted OBS remains running'
        Assert (((Get-FileHash "$root/obs-plugins/64bit/obs-streambridge.dll").Hash) -eq $oldPlugin) 'restarted OBS prevents writes despite missing original PID'
        Assert (-not $installerProcess.HasExited) 'installer waits for destination files to be released'
        Set-Content -LiteralPath $control 'exit'
        Assert ($hostProcess.WaitForExit(10000)) 'restarted host exits normally'
        Assert ((Complete-LifecycleProcess $installerOperation) -eq 0) 'installer completes after restarted host closes'
        Assert ((Get-Content "$download/update-result.ini" -Raw) -match 'Status=success') 'restarted-host update succeeds'
        Assert (((Get-FileHash "$config/vault.dpapi").Hash) -eq $before) 'restarted-host update preserves credentials'
        $testSucceeded = $true
        Write-Output "Updater test fixture retained at $fixture"
        return
    }
    if ($ImmediateExit) {
        Set-Content -LiteralPath $control 'launch'
        Wait-Until { Test-Path -LiteralPath $result } 'Native launcher did not report'
        Assert ((Get-Content -LiteralPath $result -Raw).Trim() -eq 'launched') 'production launcher accepts installer'
        Set-Content -LiteralPath $control 'exit'
        Assert ($hostProcess.WaitForExit(10000)) 'host closes immediately without waiting for installer acknowledgement'
        $hostProcess = $null
        Wait-Until { (Test-Path "$download/update-result.ini") -and (Get-Content "$download/update-result.ini" -Raw) -match 'Status=(success|failed)' } 'Immediate-exit update did not finish' 90
        $null = Complete-LifecycleProcess $hostOperation
        Assert ((Get-Content "$download/update-result.ini" -Raw) -match 'Status=success') 'update succeeds even when OBS exits before installer initialization'
        Assert (((Get-FileHash "$root/obs-plugins/64bit/obs-streambridge.dll").Hash) -ne $oldPlugin) 'immediate-exit update replaces plugin'
        Assert (((Get-FileHash "$config/vault.dpapi").Hash) -eq $before) 'immediate-exit update preserves credentials'
        $testSucceeded = $true
        Write-Output "Updater test fixture retained at $fixture"
        return
    }
    $created = $hostProcess.StartTime.ToUniversalTime().ToFileTimeUtc()
    $wrong = Invoke-LifecycleProcess -FilePath "$download/setup.exe" -Arguments "/S /UPDATEPID=$($hostProcess.Id) /UPDATECREATED=1 /D=$root" -Name wrong-identity -TimeoutSeconds $OperationTimeoutSeconds -ExpectedExitCode 2
    Assert ($wrong -eq 2) 'reused or mismatched process identity rejected'
    $other = Invoke-LifecycleProcess -FilePath "$download/setup.exe" -Arguments "/S /UPDATEPID=$PID /UPDATECREATED=$created /D=$root" -Name wrong-host -TimeoutSeconds $OperationTimeoutSeconds -ExpectedExitCode 2
    Assert ($other -eq 2) 'process outside selected OBS root rejected'
    Set-Content -LiteralPath $control 'launch'
    Wait-Until { Test-Path -LiteralPath $result } 'Native launcher did not report'
    Assert ((Get-Content -LiteralPath $result -Raw).Trim() -eq 'launched') 'production native launcher starts verified installer'
    Wait-Until { (Test-Path "$download/update-result.ini") -and (Get-Content "$download/update-result.ini" -Raw) -match 'Status=waiting' } 'Installer did not wait'
    Wait-Until { Test-Path "$result.running" } 'Updater identity was not recognized'
    Assert (Test-Path "$result.running") 'pending updater identity can be recovered safely'
    $duplicate = Invoke-LifecycleProcess -FilePath "$download/setup.exe" -Arguments "/S /UPDATEPID=$($hostProcess.Id) /UPDATECREATED=$created /D=$root" -Name duplicate -TimeoutSeconds $OperationTimeoutSeconds -ExpectedExitCode 32
    Assert ($duplicate -eq 32) 'concurrent update to the same root rejected'
    Assert ((Get-Content "$download/update-result.ini" -Raw) -match 'Status=waiting') 'duplicate does not overwrite active update status'
    Assert (-not $hostProcess.HasExited) 'updater does not close the running host'
    Assert (((Get-FileHash "$root/obs-plugins/64bit/obs-streambridge.dll").Hash) -eq $oldPlugin) 'plugin remains unchanged while host is running'
    Set-Content -LiteralPath $control 'exit'
    Assert ($hostProcess.WaitForExit(10000)) 'host exits normally on test command'
    Assert ((Complete-LifecycleProcess $hostOperation) -eq 0) 'updater completes after host exit'
    Assert ((Get-Content "$download/update-result.ini" -Raw) -match 'Status=success') 'in-place update succeeds'
    Assert (((Get-FileHash "$root/obs-plugins/64bit/obs-streambridge.dll").Hash) -ne $oldPlugin) 'plugin replaced after host exit'
    Assert (((Get-FileHash "$config/vault.dpapi").Hash) -eq $before) 'update preserves credentials byte for byte'
    Assert (Test-Path "$root/data/obs-plugins/obs-streambridge/browser/streambridge-browser.exe") 'browser runtime updated'
    $null = Invoke-LifecycleProcess -FilePath "$root/data/obs-plugins/obs-streambridge/uninstall.exe" -Arguments '/S' -Name uninstall -TimeoutSeconds $OperationTimeoutSeconds -Uninstaller
    Assert (Test-Path "$config/vault.dpapi") 'cleanup uninstall preserves configuration'
    $testSucceeded = $true
} finally {
    try { Set-Content -LiteralPath $control 'exit' } catch { Write-Warning 'Could not signal fixture exit' }
    try {
        if ($testSucceeded) {
            foreach ($operation in $operations) {
                if (-not $operation.Completed) { $null = Complete-LifecycleProcess $operation }
            }
        }
    } finally {
        $cleanupErrors = @()
        foreach ($operation in $operations) {
            try { Stop-LifecycleProcess $operation }
            catch { $cleanupErrors += $_.Exception.Message }
        }
        $env:PATH = $originalPath
        if ($cleanupErrors.Count) { throw ("Lifecycle cleanup failed: " + ($cleanupErrors -join '; ')) }
    }
}
Write-Output "Updater test fixture retained at $fixture"
