param([Parameter(Mandatory=$true)][string]$Installer,
      [string]$HostExecutable = "$PSScriptRoot/../build/Release/update-host-fixture.exe",
      [switch]$ImmediateExit, [switch]$RestartBeforeInstall)
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path "$PSScriptRoot/..").Path
$fixture = [IO.Path]::GetFullPath((Join-Path $workspace ('.deps/updater-test-' + [Guid]::NewGuid().ToString('N'))))
if (-not $fixture.StartsWith($workspace + [IO.Path]::DirectorySeparatorChar)) { throw 'Fixture escapes workspace' }
$root = Join-Path $fixture 'OBS Обновление'
$download = Join-Path $fixture 'download'
New-Item -ItemType Directory -Force "$root/bin/64bit","$root/obs-plugins/64bit","$root/data/obs-plugins/obs-streambridge",$download | Out-Null
Copy-Item -LiteralPath (Resolve-Path $Installer).Path -Destination "$download/setup.exe"
Copy-Item -LiteralPath (Resolve-Path $HostExecutable).Path -Destination "$root/bin/64bit/obs64.exe"
New-Item -ItemType File "$root/portable_mode.txt" | Out-Null
Set-Content "$root/data/obs-plugins/obs-streambridge/package.ini" @('[StreamBridge]','Product=obs-streambridge','Version=0.1.0')
Set-Content "$root/obs-plugins/64bit/obs-streambridge.dll" 'old plugin canary'
$config = "$root/config/obs-studio/plugin_config/obs-streambridge"
New-Item -ItemType Directory -Force $config | Out-Null
Set-Content "$config/vault.dpapi" 'synthetic credential canary'
$before = (Get-FileHash "$config/vault.dpapi").Hash
$oldPlugin = (Get-FileHash "$root/obs-plugins/64bit/obs-streambridge.dll").Hash
function Assert([bool]$value, [string]$message) { if (-not $value) { throw $message }; Write-Output "PASS: $message" }
function Wait-Until([scriptblock]$condition, [string]$message, [int]$seconds = 30) {
    $deadline = [DateTime]::UtcNow.AddSeconds($seconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        try { if (& $condition) { return } } catch [IO.IOException] { }
        Start-Sleep -Milliseconds 100
    }
    throw $message
}
$env:PATH = (Resolve-Path "$workspace/.deps/obs-deps-qt6-2025-08-23-x64/bin").Path + ';' + $env:PATH
$control = "$fixture/control.txt"
$result = "$fixture/launch.txt"
$hostProcess = Start-Process -FilePath "$root/bin/64bit/obs64.exe" -ArgumentList ('"{0}" "{1}" "{2}"' -f "$download/setup.exe",$control,$result) -WindowStyle Hidden -PassThru
try {
    Start-Sleep -Milliseconds 300
    Assert (-not $hostProcess.HasExited) 'isolated host fixture starts'
    if ($RestartBeforeInstall) {
        $oldPid = $hostProcess.Id
        $oldCreated = $hostProcess.StartTime.ToUniversalTime().ToFileTimeUtc()
        Set-Content -LiteralPath $control 'exit'
        Assert ($hostProcess.WaitForExit(10000)) 'original host exits before installer launch'
        $hostProcess.Dispose()
        Set-Content -LiteralPath $control 'idle'
        $hostProcess = Start-Process -FilePath "$root/bin/64bit/obs64.exe" -ArgumentList ('"{0}" "{1}" "{2}"' -f "$download/setup.exe",$control,$result) -WindowStyle Hidden -PassThru
        $installerProcess = Start-Process -FilePath "$download/setup.exe" -ArgumentList "/S /UPDATEPID=$oldPid /UPDATECREATED=$oldCreated /D=$root" -WindowStyle Hidden -PassThru
        Start-Sleep -Seconds 6
        Assert (-not $hostProcess.HasExited) 'restarted OBS remains running'
        Assert (((Get-FileHash "$root/obs-plugins/64bit/obs-streambridge.dll").Hash) -eq $oldPlugin) 'restarted OBS prevents writes despite missing original PID'
        Assert (-not $installerProcess.HasExited) 'installer waits for destination files to be released'
        Set-Content -LiteralPath $control 'exit'
        Assert ($hostProcess.WaitForExit(10000)) 'restarted host exits normally'
        Assert ($installerProcess.WaitForExit(90000)) 'installer completes after restarted host closes'
        Assert ((Get-Content "$download/update-result.ini" -Raw) -match 'Status=success') 'restarted-host update succeeds'
        Assert (((Get-FileHash "$config/vault.dpapi").Hash) -eq $before) 'restarted-host update preserves credentials'
        Write-Output "Updater test fixture retained at $fixture"
        return
    }
    if ($ImmediateExit) {
        Set-Content -LiteralPath $control 'launch'
        Wait-Until { Test-Path -LiteralPath $result } 'Native launcher did not report'
        Assert ((Get-Content -LiteralPath $result -Raw).Trim() -eq 'launched') 'production launcher accepts installer'
        Set-Content -LiteralPath $control 'exit'
        Assert ($hostProcess.WaitForExit(10000)) 'host closes immediately without waiting for installer acknowledgement'
        $hostProcess.Dispose()
        $hostProcess = $null
        Wait-Until { (Test-Path "$download/update-result.ini") -and (Get-Content "$download/update-result.ini" -Raw) -match 'Status=(success|failed)' } 'Immediate-exit update did not finish' 90
        Assert ((Get-Content "$download/update-result.ini" -Raw) -match 'Status=success') 'update succeeds even when OBS exits before installer initialization'
        Assert (((Get-FileHash "$root/obs-plugins/64bit/obs-streambridge.dll").Hash) -ne $oldPlugin) 'immediate-exit update replaces plugin'
        Assert (((Get-FileHash "$config/vault.dpapi").Hash) -eq $before) 'immediate-exit update preserves credentials'
        Write-Output "Updater test fixture retained at $fixture"
        return
    }
    $created = $hostProcess.StartTime.ToUniversalTime().ToFileTimeUtc()
    $wrong = Start-Process -FilePath "$download/setup.exe" -ArgumentList "/S /UPDATEPID=$($hostProcess.Id) /UPDATECREATED=1 /D=$root" -WindowStyle Hidden -PassThru -Wait
    Assert ($wrong.ExitCode -eq 2) 'reused or mismatched process identity rejected'
    $other = Start-Process -FilePath "$download/setup.exe" -ArgumentList "/S /UPDATEPID=$PID /UPDATECREATED=$created /D=$root" -WindowStyle Hidden -PassThru -Wait
    Assert ($other.ExitCode -eq 2) 'process outside selected OBS root rejected'
    Set-Content -LiteralPath $control 'launch'
    Wait-Until { Test-Path -LiteralPath $result } 'Native launcher did not report'
    Assert ((Get-Content -LiteralPath $result -Raw).Trim() -eq 'launched') 'production native launcher starts verified installer'
    Wait-Until { (Test-Path "$download/update-result.ini") -and (Get-Content "$download/update-result.ini" -Raw) -match 'Status=waiting' } 'Installer did not wait'
    Wait-Until { Test-Path "$result.running" } 'Updater identity was not recognized'
    Assert (Test-Path "$result.running") 'pending updater identity can be recovered safely'
    $duplicate = Start-Process -FilePath "$download/setup.exe" -ArgumentList "/S /UPDATEPID=$($hostProcess.Id) /UPDATECREATED=$created /D=$root" -WindowStyle Hidden -PassThru -Wait
    Assert ($duplicate.ExitCode -eq 32) 'concurrent update to the same root rejected'
    Assert ((Get-Content "$download/update-result.ini" -Raw) -match 'Status=waiting') 'duplicate does not overwrite active update status'
    Assert (-not $hostProcess.HasExited) 'updater does not close the running host'
    Assert (((Get-FileHash "$root/obs-plugins/64bit/obs-streambridge.dll").Hash) -eq $oldPlugin) 'plugin remains unchanged while host is running'
    $state = Get-Content "$download/update-result.ini" -Raw
    $match = [regex]::Match($state, '(?m)^Pid=(\d+)')
    $updaterProcess = Get-Process -Id ([int]$match.Groups[1].Value)
    Set-Content -LiteralPath $control 'exit'
    Assert ($hostProcess.WaitForExit(10000)) 'host exits normally on test command'
    Assert ($updaterProcess.WaitForExit(90000)) 'updater completes after host exit'
    Assert ((Get-Content "$download/update-result.ini" -Raw) -match 'Status=success') 'in-place update succeeds'
    Assert (((Get-FileHash "$root/obs-plugins/64bit/obs-streambridge.dll").Hash) -ne $oldPlugin) 'plugin replaced after host exit'
    Assert (((Get-FileHash "$config/vault.dpapi").Hash) -eq $before) 'update preserves credentials byte for byte'
    Assert (Test-Path "$root/data/obs-plugins/obs-streambridge/browser/streambridge-browser.exe") 'browser runtime updated'
    Start-Process -FilePath "$root/data/obs-plugins/obs-streambridge/uninstall.exe" -ArgumentList '/S' -WindowStyle Hidden -Wait
    Assert (Test-Path "$config/vault.dpapi") 'cleanup uninstall preserves configuration'
} finally {
    try { Set-Content -LiteralPath $control 'exit' } catch { Write-Warning 'Could not signal fixture exit' }
    if ($hostProcess -and -not $hostProcess.HasExited -and -not $hostProcess.WaitForExit(5000)) {
        if ($hostProcess.Path -eq "$root\bin\64bit\obs64.exe") { Stop-Process -Id $hostProcess.Id }
    }
}
Write-Output "Updater test fixture retained at $fixture"
