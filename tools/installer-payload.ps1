# SPDX-License-Identifier: GPL-2.0-or-later
param([Parameter(Mandatory=$true)][string]$Stage,
      [Parameter(Mandatory=$true)][string]$Output,
      [string]$Helper = "$PSScriptRoot/../build/Release/streambridge-files.exe")
$ErrorActionPreference = 'Stop'
$Stage = (Resolve-Path -LiteralPath $Stage).Path
$Helper = (Resolve-Path -LiteralPath $Helper).Path
$manifestRelative = 'data\obs-plugins\obs-streambridge\uninstall-manifest.txt'
$manifest = Join-Path $Stage $manifestRelative
# One inventory drives both extraction and removal. Generated installer metadata is explicit.
$files = @(Get-ChildItem -LiteralPath $Stage -File -Recurse | ForEach-Object {
    if ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse point in package stage' }
    [IO.Path]::GetRelativePath($Stage, $_.FullName).Replace('/','\')
} | Where-Object { $_ -ne $manifestRelative } | Sort-Object)
foreach ($path in $files) {
    if ($path -ne 'obs-plugins\64bit\obs-streambridge.dll' -and -not $path.StartsWith('data\obs-plugins\obs-streambridge\')) { throw "Unowned payload: $path" }
}
$inventory = @($files) + @($manifestRelative,'data\obs-plugins\obs-streambridge\install.ini','data\obs-plugins\obs-streambridge\uninstall.exe')
$inventory | Set-Content -LiteralPath $manifest -Encoding utf8
$lines = [Collections.Generic.List[string]]::new()
$lines.Add('!ifndef SB_MANIFEST')
$lines.Add('!define SB_MANIFEST "' + $manifest.Replace('$','$$') + '"')
$lines.Add('!define SB_FILES_HELPER "' + $Helper.Replace('$','$$') + '"')
$lines.Add('!endif')
$lines.Add('!ifdef SB_EXTRACT_PAYLOAD')
foreach ($relative in @($files) + @($manifestRelative)) {
    $dir = [IO.Path]::GetDirectoryName($relative)
    $lines.Add('SetOutPath "$PLUGINSDIR\payload\' + $dir.Replace('$','$$') + '"')
    $lines.Add('File "' + (Join-Path $Stage $relative).Replace('$','$$') + '"')
}
$lines.Add('!endif')
$lines | Set-Content -LiteralPath $Output -Encoding utf8
# Exact set equality includes the generated manifest; nothing is silently excluded.
$actual = @(Get-ChildItem -LiteralPath $Stage -File -Recurse | ForEach-Object { [IO.Path]::GetRelativePath($Stage,$_.FullName) })
$expected = @($inventory | Where-Object { $_ -notin @('data\obs-plugins\obs-streambridge\install.ini','data\obs-plugins\obs-streambridge\uninstall.exe') })
if (Compare-Object $actual $expected) { throw 'Installer payload/manifest mismatch' }
Write-Output "PASS payload manifest covers $($actual.Count) packaged files plus install.ini/uninstall.exe"
