param([string]$Filter = '.*')
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path "$PSScriptRoot/..").Path
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -version '[17.0,18.0)' -latest -property installationPath
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
$start.WorkingDirectory = $workspace
$start.UseShellExecute = $false
foreach ($arg in @('--test-dir','build','-C','Release','--output-on-failure','-R',$Filter)) { $start.ArgumentList.Add($arg) }
$start.Environment.Clear()
foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) { $start.Environment[$entry.Key.ToUpperInvariant()] = $entry.Value }
$start.Environment['PATH'] = "$workspace/.deps/obs-deps-qt6-2025-08-23-x64/bin;$workspace/.deps/obs-portable/bin/64bit;" + $start.Environment['PATH']
$start.Environment['QT_PLUGIN_PATH'] = "$workspace/.deps/obs-deps-qt6-2025-08-23-x64/plugins"
$process = [Diagnostics.Process]::Start($start)
$process.WaitForExit()
exit $process.ExitCode
