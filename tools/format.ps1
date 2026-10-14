param([switch]$Check)
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path "$PSScriptRoot/..").Path
$command = Get-Command clang-format -ErrorAction SilentlyContinue
if ($command) { $formatter = $command.Source } else {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -version '[17.0,18.0)' -latest -property installationPath
    $formatter = Join-Path $vs 'VC/Tools/Llvm/x64/bin/clang-format.exe'
}
foreach ($file in Get-ChildItem "$workspace/src","$workspace/tests","$workspace/helper","$workspace/updater" -File -Recurse | Where-Object Extension -In '.cpp','.hpp') {
    if ($Check) { & $formatter --dry-run --Werror $file.FullName } else { & $formatter -i $file.FullName }
    if ($LASTEXITCODE -ne 0) { throw "Formatting failed: $($file.FullName)" }
}
