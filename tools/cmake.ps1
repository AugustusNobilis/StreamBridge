# Run CMake with a normalized child environment (some launchers pass both Path and PATH).
param([Parameter(ValueFromRemainingArguments=$true)][string[]]$CMakeArgs)
$ErrorActionPreference = 'Stop'
$command = Get-Command cmake -ErrorAction SilentlyContinue
if ($command) { $cmakeExe = $command.Source } else {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -version '[17.0,18.0)' -latest -property installationPath
    $cmakeExe = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
$start = [System.Diagnostics.ProcessStartInfo]::new()
$start.FileName = $cmakeExe
$start.UseShellExecute = $false
$start.WorkingDirectory = (Get-Location).Path
foreach ($argument in $CMakeArgs) { $start.ArgumentList.Add($argument) }
$start.Environment.Clear()
foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
    $start.Environment[$entry.Key.ToUpperInvariant()] = $entry.Value
}
$process = [System.Diagnostics.Process]::Start($start)
$process.WaitForExit()
exit $process.ExitCode
