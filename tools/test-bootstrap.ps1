param([string]$Updater = "$PSScriptRoot/../dist/updater.exe",
      [string]$HostExecutable = "$PSScriptRoot/../build/Release/update-host-fixture.exe")
# Requires the public v0.2.2 release. Never modifies the user's installed OBS.
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path "$PSScriptRoot/..").Path
$fixture = [IO.Path]::GetFullPath((Join-Path $workspace ('.deps/bootstrap-test-' + [Guid]::NewGuid().ToString('N'))))
if (-not $fixture.StartsWith($workspace + [IO.Path]::DirectorySeparatorChar)) { throw 'Fixture escapes workspace' }
New-Item -ItemType Directory -Force $fixture | Out-Null
$Updater = (Resolve-Path -LiteralPath $Updater).Path
function Assert([bool]$value, [string]$message) { if (-not $value) { throw $message }; Write-Output "PASS: $message" }
$originalPath = $env:PATH
try {
    # Deliberately exclude OBS, Qt and developer runtimes from DLL search.
    $env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
    $download = Start-Process -FilePath $Updater -ArgumentList ('--download-only "{0}"' -f $fixture) -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
    if (-not $download.WaitForExit(600000)) { throw 'Bootstrap download timed out' }
    Assert ($download.ExitCode -eq 0) 'standalone updater downloads real GitHub release without Qt/OBS runtime'
    $report = Get-Content "$fixture/updater-result.txt"
    Assert ($report[0].StartsWith('verified ')) 'download reports verification success'
    $file = $report[0].Substring(9)
    Assert ((Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant() -eq $report[1]) 'GitHub SHA-256 matches actual downloaded EXE'
    foreach ($oldVersion in @('0.1.0','0.1.1','0.1.2','0.2.0','0.2.1')) {
        $root = Join-Path $fixture "Файлы программ/OBS $oldVersion"
        New-Item -ItemType Directory -Force "$root/bin/64bit","$root/obs-plugins/64bit","$root/data/obs-plugins/obs-streambridge" | Out-Null
        Copy-Item -LiteralPath (Resolve-Path -LiteralPath $HostExecutable).Path -Destination "$root/bin/64bit/obs64.exe"
        New-Item -ItemType File "$root/portable_mode.txt" | Out-Null
        Set-Content "$root/data/obs-plugins/obs-streambridge/package.ini" @('[StreamBridge]','Product=obs-streambridge',"Version=$oldVersion")
        Set-Content "$root/obs-plugins/64bit/obs-streambridge.dll" 'old plugin canary'
        $oldPlugin = (Get-FileHash "$root/obs-plugins/64bit/obs-streambridge.dll").Hash
        $config = "$root/config/obs-studio/plugin_config/obs-streambridge"
        New-Item -ItemType Directory -Force $config | Out-Null
        Set-Content "$config/vault.dpapi" "synthetic credential $oldVersion"
        $before = (Get-FileHash "$config/vault.dpapi").Hash
        $control = "$fixture/control-$oldVersion.txt"
        $hostProcess = $null
        try {
            if ($oldVersion -eq '0.1.0') {
                # One case keeps the host alive to exercise the native bootstrap's PID/path/time handoff.
                $env:PATH = "$workspace/.deps/obs-deps-qt6-2025-08-23-x64/bin;$originalPath"
                $hostProcess = Start-Process -FilePath "$root/bin/64bit/obs64.exe" -ArgumentList ('"{0}" "{1}" "{2}"' -f $file,$control,"$fixture/host-result.txt") -WindowStyle Hidden -PassThru
                Start-Sleep -Milliseconds 300
                Assert (-not $hostProcess.HasExited) 'isolated host starts'
            }
            $env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
            $update = Start-Process -FilePath $Updater -ArgumentList ('--install-to "{0}"' -f $root) -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
            if ($hostProcess) {
                # Download duration is variable; wait until the installer explicitly reports waiting.
                $deadline = [DateTime]::UtcNow.AddMinutes(5)
                $waiting = $false
                while ([DateTime]::UtcNow -lt $deadline -and -not $update.HasExited) {
                    $children = Get-CimInstance Win32_Process -Filter "Name='obs-streambridge-0.2.2-windows-x64-setup.exe'" |
                        Where-Object { $_.CommandLine -like "*$root*" }
                    foreach ($child in $children) {
                        $status = Join-Path ([IO.Path]::GetDirectoryName($child.ExecutablePath)) 'update-result.ini'
                        try { if ((Test-Path $status) -and (Get-Content $status -Raw) -match 'Status=waiting') { $waiting = $true } } catch [IO.IOException] {}
                    }
                    if ($waiting) { break }
                    Start-Sleep -Milliseconds 300
                }
                Assert $waiting 'native bootstrap waits for selected running OBS'
                Assert (-not $hostProcess.HasExited) 'bootstrap does not terminate OBS'
                Assert ((Get-FileHash "$root/obs-plugins/64bit/obs-streambridge.dll").Hash -eq $oldPlugin) 'files unchanged while OBS runs'
                Set-Content -LiteralPath $control 'exit'
                Assert ($hostProcess.WaitForExit(10000)) 'host exits normally'
            }
            Assert ($update.WaitForExit(600000)) "upgrade from $oldVersion completes"
            Assert ($update.ExitCode -eq 0) "upgrade from $oldVersion returns success"
            Assert ((Get-Content "$root/data/obs-plugins/obs-streambridge/package.ini" -Raw) -match 'Version=0.2.2') 'target version installed'
            Assert ((Get-FileHash "$root/obs-plugins/64bit/obs-streambridge.dll").Hash -eq (Get-FileHash "$workspace/build/Release/obs-streambridge.dll").Hash) 'installed plugin equals released build'
            Assert ((Get-FileHash "$config/vault.dpapi").Hash -eq $before) 'credentials preserved byte for byte'
            Assert (Test-Path "$root/data/obs-plugins/obs-streambridge/qt-plugins/tls/qschannelbackend.dll") 'HTTPS backend installed'
        } finally {
            if ($hostProcess -and -not $hostProcess.HasExited) {
                Set-Content -LiteralPath $control 'exit'
                if (-not $hostProcess.WaitForExit(5000) -and $hostProcess.Path -eq "$root\bin\64bit\obs64.exe") { Stop-Process -Id $hostProcess.Id }
            }
        }
        Start-Process -FilePath "$root/data/obs-plugins/obs-streambridge/uninstall.exe" -ArgumentList '/S' -WindowStyle Hidden -Wait
        Assert (Test-Path "$config/vault.dpapi") 'fixture uninstall preserves settings'
    }
} finally { $env:PATH = $originalPath }
Write-Output "Bootstrap fixture retained at $fixture"
