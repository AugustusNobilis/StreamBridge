param([string]$ObsRoot = "$PSScriptRoot/../.deps/obs-portable", [switch]$ShowWindow)
$ErrorActionPreference = 'Stop'
$obsBin = (Resolve-Path (Join-Path $ObsRoot 'bin/64bit')).Path
$obsExe = Join-Path $obsBin 'obs64.exe'
if (-not (Test-Path (Join-Path $ObsRoot 'portable_mode.txt'))) { throw 'Only an isolated portable OBS may be launched by this script.' }
# OBS Windows resolves locale, theme and libobs assets relative to bin/64bit.
# Launching from the project root produces "Failed to find locale/en-US.ini" before logging starts.
$style = if ($ShowWindow) { 'Normal' } else { 'Hidden' }
Start-Process -FilePath $obsExe -WorkingDirectory $obsBin -ArgumentList '--portable','--multi','--disable-updater' -WindowStyle $style -PassThru
