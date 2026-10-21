# SPDX-License-Identifier: GPL-2.0-or-later
param([Parameter(Mandatory=$true)][string]$Installer,
      [Parameter(Mandatory=$true)][string]$ObsExecutable,
      [string]$PublishedInstaller = '',
      [ValidateRange(1,1800)][int]$OperationTimeoutSeconds = 120)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lifecycle-process.ps1"
$workspace = (Resolve-Path "$PSScriptRoot/..").Path
$fixture = Join-Path $workspace ('build/lifecycle-diagnostics/published-upgrade-' + [Guid]::NewGuid().ToString('N'))
Initialize-Lifecycle $fixture
$Installer = (Resolve-Path -LiteralPath $Installer).Path
# Immutable expected bytes, verified against the release asset's GitHub SHA-256 digest.
# A newer installer with the same version string is deliberately NOT a substitute.
$source = 'https://github.com/AugustusNobilis/StreamBridge/releases/download/v0.2.5/obs-streambridge-0.2.5-windows-x64-setup.exe'
$expected = '84c82143980f464e004ff5208649a03295ab68a9cdc0348a131006660aa3ea80'
Write-Lifecycle "SOURCE published=v0.2.5 url=$source expected-sha256=$expected"
if (-not $PublishedInstaller) {
    $PublishedInstaller = Join-Path $fixture 'published-0.2.5-setup.exe'
    Write-Lifecycle 'START download published installer max-time=120s'
    $downloadWatch = [Diagnostics.Stopwatch]::StartNew()
    & curl.exe --fail --location --proto '=https' --proto-redir '=https' --tlsv1.2 --max-time 120 --output $PublishedInstaller $source
    $downloadCode = $LASTEXITCODE
    Write-Lifecycle ("END download code={0} duration={1:F3}s" -f $downloadCode,$downloadWatch.Elapsed.TotalSeconds)
    if ($downloadCode -ne 0) { throw 'Published installer download failed; migration test was not run' }
}
$PublishedInstaller = (Resolve-Path -LiteralPath $PublishedInstaller).Path
$actual = (Get-FileHash -LiteralPath $PublishedInstaller -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Lifecycle "SOURCE actual-sha256=$actual local-file=$PublishedInstaller"
if ($actual -cne $expected) { throw 'Published v0.2.5 SHA-256 mismatch; refusing to execute' }
if ((Get-FileHash -LiteralPath $Installer).Hash -eq $actual) { throw 'Upgrade target is the old published installer, not the fixed build' }
function Assert([bool]$Value, [string]$Message) {
    if (-not $Value) { throw $Message }
    Write-Lifecycle "PASS: $Message"
}
$root = Join-Path $fixture 'Переход версии с пробелами\OBS Тест'
New-Item -ItemType Directory -Force "$root/bin/64bit","$root/data/obs-plugins/unrelated" | Out-Null
Copy-Item -LiteralPath $ObsExecutable -Destination "$root/bin/64bit/obs64.exe"
New-Item -ItemType File -Path "$root/portable_mode.txt" | Out-Null
Set-Content -LiteralPath "$root/data/obs-plugins/unrelated/preserve.txt" 'unrelated synthetic plugin'
$config = "$root/config/obs-studio/plugin_config/obs-streambridge"
New-Item -ItemType Directory -Force $config | Out-Null
Set-Content -LiteralPath "$config/settings.json" '{"publishedUpgradeTestCanary":true}'
Set-Content -LiteralPath "$config/vault.dpapi" 'synthetic vault canary; no user credentials'
$preserved = @{}
foreach ($path in @("$root/bin/64bit/obs64.exe","$root/data/obs-plugins/unrelated/preserve.txt","$config/settings.json","$config/vault.dpapi")) {
    $preserved[$path] = (Get-FileHash -LiteralPath $path).Hash
}
$null = Invoke-LifecycleProcess $PublishedInstaller "/S /D=$root" published-install $OperationTimeoutSeconds
$resources = "$root/data/obs-plugins/obs-streambridge"
Assert ((Get-Content -LiteralPath "$resources/package.ini" -Raw) -match '(?m)^Version=0\.2\.5\s*$') 'real published 0.2.5 is installed'
$oldUninstaller = (Get-FileHash -LiteralPath "$resources/uninstall.exe").Hash
$oldMarker = [IO.File]::ReadAllBytes("$resources/install.ini")
Write-Lifecycle ("Published install.ini prefix={0}" -f [BitConverter]::ToString($oldMarker[0..([Math]::Min(7,$oldMarker.Length - 1))]))
$null = Invoke-LifecycleProcess $Installer "/S /D=$root" upgrade-to-fixed $OperationTimeoutSeconds
Assert ((Get-FileHash -LiteralPath "$resources/uninstall.exe").Hash -ne $oldUninstaller) 'upgrade replaces the published uninstaller with the fixed uninstaller'
$marker = [IO.File]::ReadAllBytes("$resources/install.ini")
Assert ($marker[0] -eq 255 -and $marker[1] -eq 254 -and [Text.Encoding]::Unicode.GetString($marker).Contains("Root=$root")) 'upgrade repairs the Unicode ownership marker with the exact installation root'
foreach ($path in $preserved.Keys) {
    Assert ((Get-FileHash -LiteralPath $path).Hash -eq $preserved[$path]) "published upgrade preserves $([IO.Path]::GetFileName($path)) byte for byte"
}
$null = Invoke-LifecycleProcess "$resources/uninstall.exe" '/S' fixed-uninstall $OperationTimeoutSeconds -Uninstaller
Assert (-not (Test-Path -LiteralPath "$root/obs-plugins/64bit/obs-streambridge.dll")) 'new uninstaller removes the upgraded plugin DLL'
Assert (-not (Test-Path -LiteralPath $resources)) 'new uninstaller removes the upgraded resources'
foreach ($path in $preserved.Keys) {
    Assert ((Get-FileHash -LiteralPath $path).Hash -eq $preserved[$path]) "new uninstaller preserves $([IO.Path]::GetFileName($path)) byte for byte"
}
Write-Lifecycle 'PASS: published 0.2.5 -> fixed build -> new silent uninstaller'
