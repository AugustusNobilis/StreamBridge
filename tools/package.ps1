param([string]$Build = "$PSScriptRoot/../build", [string]$HelperBuild = "$PSScriptRoot/../build-helper",
      [string]$Nsis = "$PSScriptRoot/../.deps/nsis-3.11/makensis.exe")
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path "$PSScriptRoot/..").Path
$version = [regex]::Match((Get-Content "$workspace/CMakeLists.txt" -Raw), 'project\(obs-streambridge VERSION ([0-9.]+)').Groups[1].Value
if (-not $version) { throw 'Project version missing' }
& "$PSScriptRoot/cmake.ps1" -CMakeArgs @('--build',$Build,'--config','Release','--target','obs-streambridge','updater','streambridge-files','--parallel','4')
if ($LASTEXITCODE -ne 0) { throw 'Plugin rebuild before packaging failed' }
$stage = [IO.Path]::GetFullPath((Join-Path $workspace 'stage'))
if (-not $stage.StartsWith($workspace + [IO.Path]::DirectorySeparatorChar)) { throw 'Stage escapes workspace' }
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage, "$workspace/dist", "$workspace/build/package" | Out-Null
& "$PSScriptRoot/cmake.ps1" --install $Build --config Release --prefix $stage
if ($LASTEXITCODE -ne 0) { throw 'Plugin installation into stage failed' }
$browser = Join-Path $stage 'data/obs-plugins/obs-streambridge/browser'
New-Item -ItemType Directory -Force $browser | Out-Null
# Explicit allowlist excludes test helpers, import libraries, debug files and build metadata.
$files = @('streambridge-browser.exe','streambridge-browser.dll','libcef.dll','chrome_elf.dll','d3dcompiler_47.dll',
  'dxcompiler.dll','dxil.dll','icudtl.dat','chrome_100_percent.pak','chrome_200_percent.pak','resources.pak',
  'v8_context_snapshot.bin','vk_swiftshader.dll','vk_swiftshader_icd.json','vulkan-1.dll')
foreach ($file in $files) { Copy-Item -LiteralPath "$HelperBuild/Release/$file" -Destination $browser }
Copy-Item -LiteralPath "$HelperBuild/Release/locales" -Destination $browser -Recurse
$cef = (Get-ChildItem "$workspace/.deps/cef" -Directory | Where-Object Name -Like 'cef_binary_154.0.34*' | Select-Object -First 1).FullName
if (-not $cef) { throw 'Pinned CEF distribution missing' }
Copy-Item -LiteralPath "$cef/LICENSE.txt" -Destination "$browser/CEF-LICENSE.txt"
Copy-Item -LiteralPath "$cef/CREDITS.html" -Destination "$browser/CHROMIUM-CREDITS.html"
@"
[StreamBridge]
Product=obs-streambridge
Version=$version
"@ | Set-Content "$stage/data/obs-plugins/obs-streambridge/package.ini" -Encoding utf8
$payload = Join-Path $workspace 'build/package/payload.nsh'
& "$PSScriptRoot/installer-payload.ps1" -Stage $stage -Output $payload -Helper "$Build/Release/streambridge-files.exe"
$exe = Join-Path $workspace "dist/obs-streambridge-$version-windows-x64-setup.exe"
& $Nsis /V2 "/DVERSION=$version" "/DPAYLOAD=$payload" "/DOUTPUT=$exe" "$workspace/installer/streambridge.nsi"
if ($LASTEXITCODE -ne 0) { throw 'NSIS compilation failed' }
$zip = Join-Path $workspace "dist/obs-streambridge-$version-windows-x64.zip"
Push-Location $stage
try { & "$PSScriptRoot/cmake.ps1" -CMakeArgs @('-E','tar','cf',$zip,'--format=zip','obs-plugins','data') } finally { Pop-Location }
if ($LASTEXITCODE -ne 0) { throw 'ZIP creation failed' }
$sourceZip = Join-Path $workspace "dist/obs-streambridge-$version-source.zip"
$sourceFiles = @('CMakeLists.txt','CMakePresets.json','dependencies.lock.json','.clang-format','.gitattributes','.gitignore','.github','src','helper','updater','tests','tools','installer','data','docs','README.md','LICENSE','THIRD_PARTY_NOTICES.md','CONTRIBUTING.md','SECURITY.md','CHANGELOG.md')
Push-Location $workspace
try { & "$PSScriptRoot/cmake.ps1" -CMakeArgs (@('-E','tar','cf',$sourceZip,'--format=zip') + $sourceFiles) } finally { Pop-Location }
if ($LASTEXITCODE -ne 0) { throw 'Source ZIP creation failed' }
$updater = "$workspace/dist/updater.exe"
Copy-Item -LiteralPath "$Build/Release/updater.exe" -Destination $updater
$releaseFiles = @($exe,$zip,$sourceZip,$updater)
# Matching source and build recipe for the redistributed Qt Schannel backend.
$lock = Get-Content "$workspace/dependencies.lock.json" -Raw | ConvertFrom-Json
foreach ($source in $lock.dependencies | Where-Object sourceOnly) {
    $inputFile = Join-Path "$workspace/.deps" $source.archive
    if ((Get-FileHash -LiteralPath $inputFile).Hash -ne $source.sha256) { throw "Source hash mismatch: $($source.id)" }
    $outputFile = Join-Path "$workspace/dist" $source.archive
    Copy-Item -LiteralPath $inputFile -Destination $outputFile
    $releaseFiles += $outputFile
}
$checksums = foreach ($file in $releaseFiles) { $h = Get-FileHash -LiteralPath $file -Algorithm SHA256; $h.Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($file) }
$checksums | Set-Content "$workspace/dist/SHA256SUMS.txt" -Encoding ascii
Get-Item $releaseFiles | Select-Object Name,Length
