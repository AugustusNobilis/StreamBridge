param([switch]$TestDependencies, [switch]$VerifyOnly)
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path "$PSScriptRoot/..").Path
$deps = Join-Path $workspace '.deps'
New-Item -ItemType Directory -Force $deps | Out-Null
$lock = Get-Content "$workspace/dependencies.lock.json" -Raw | ConvertFrom-Json
foreach ($dependency in $lock.dependencies) {
    if ($dependency.testOnly -and -not $TestDependencies) { continue }
    $archive = Join-Path $deps $dependency.archive
    if (-not (Test-Path -LiteralPath $archive)) {
        if ($VerifyOnly) { throw "Missing archive: $($dependency.archive)" }
        Write-Output "Downloading pinned $($dependency.id)"
        & curl.exe --fail --location --retry 3 --proto '=https' --tlsv1.2 --output $archive $dependency.url
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $($dependency.id)" }
    }
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if ($hash -ne $dependency.sha256) { throw "SHA-256 mismatch: $($dependency.id). Archive was not extracted." }
    Write-Output "SHA-256 verified: $($dependency.id)"
    if ($VerifyOnly -or $dependency.sourceOnly -or (Test-Path -LiteralPath (Join-Path $deps $dependency.directory))) { continue }
    $destination = Join-Path $deps $dependency.extract
    New-Item -ItemType Directory -Force $destination | Out-Null
    Push-Location $destination
    try { & "$PSScriptRoot/cmake.ps1" -CMakeArgs @('-E','tar','xf',$archive) } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw "Extraction failed: $($dependency.id)" }
}
if ($VerifyOnly) { return }
$prefix = ($deps + '/obs-deps-2025-08-23-x64;' + $deps + '/obs-deps-qt6-2025-08-23-x64').Replace('\','/')
& "$PSScriptRoot/cmake.ps1" -CMakeArgs @('-S',"$deps/obs-studio-32.0.4",'-B',"$deps/obs-build",'-G','Visual Studio 17 2022','-A','x64',
    '-DOBS_VERSION_OVERRIDE=32.0.4','-DENABLE_FRONTEND=OFF','-DENABLE_PLUGINS=OFF','-DENABLE_SCRIPTING=OFF','-DENABLE_BROWSER=OFF',"-DCMAKE_PREFIX_PATH=$prefix")
if ($LASTEXITCODE -ne 0) { throw 'OBS SDK configuration failed' }
& "$PSScriptRoot/cmake.ps1" -CMakeArgs @('--build',"$deps/obs-build",'--config','Release','--target','obs-frontend-api','--parallel','4')
if ($LASTEXITCODE -ne 0) { throw 'OBS SDK build failed' }
& "$PSScriptRoot/cmake.ps1" -CMakeArgs @('--install',"$deps/obs-build",'--config','Release','--component','Development','--prefix',"$deps/sdk")
if ($LASTEXITCODE -ne 0) { throw 'OBS SDK installation failed' }
Write-Output 'Pinned SDK and helper dependencies are ready. No certificate trust was changed.'
