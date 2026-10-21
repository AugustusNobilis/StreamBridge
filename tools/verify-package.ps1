param([string]$Dist = "$PSScriptRoot/../dist")
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path "$PSScriptRoot/..").Path
$Dist = (Resolve-Path -LiteralPath $Dist).Path
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach ($line in Get-Content "$Dist/SHA256SUMS.txt") {
    if ($line -notmatch '^([a-f0-9]{64})  ([a-zA-Z0-9._-]+)$') { throw 'Invalid checksum manifest' }
    if ((Get-FileHash -LiteralPath (Join-Path $Dist $Matches[2]) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Matches[1]) { throw "Hash mismatch: $($Matches[2])" }
    Write-Output "PASS: SHA-256 $($Matches[2])"
}
$version = [regex]::Match((Get-Content "$workspace/CMakeLists.txt" -Raw),'project\(obs-streambridge VERSION ([0-9.]+)').Groups[1].Value
if ((Get-FileHash "$Dist/updater.exe").Hash -ne (Get-FileHash "$workspace/build/Release/updater.exe").Hash) { throw 'Standalone updater mismatch' }
foreach ($source in (Get-Content "$workspace/dependencies.lock.json" -Raw | ConvertFrom-Json).dependencies | Where-Object sourceOnly) {
    if ((Get-FileHash (Join-Path $Dist $source.archive)).Hash -ne $source.sha256) { throw "Corresponding source mismatch: $($source.id)" }
}
Write-Output 'PASS: standalone updater and pinned corresponding-source hashes'
$zip = [IO.Compression.ZipFile]::OpenRead("$Dist/obs-streambridge-$version-windows-x64.zip")
try {
    $names = @($zip.Entries.FullName)
    if ('data/obs-plugins/obs-streambridge/streambridge-update-helper.exe' -notin $names) { throw 'Missing native restart helper' }
    foreach ($required in @('data/obs-plugins/obs-streambridge/qt-plugins/imageformats/qgif.dll','data/obs-plugins/obs-streambridge/qt-plugins/tls/qschannelbackend.dll','data/obs-plugins/obs-streambridge/qt-plugins/LICENSE.GPL2')) {
        if ($required -notin $names) { throw "Missing TLS runtime/license: $required" }
    }
    $tls = $zip.GetEntry('data/obs-plugins/obs-streambridge/qt-plugins/tls/qschannelbackend.dll').Open()
    $tlsSha = [Security.Cryptography.SHA256]::Create()
    try { $tlsHash = [BitConverter]::ToString($tlsSha.ComputeHash($tls)).Replace('-','') } finally { $tlsSha.Dispose(); $tls.Dispose() }
    if ($tlsHash -ne (Get-FileHash "$workspace/.deps/obs-deps-qt6-2025-08-23-x64/plugins/tls/qschannelbackend.dll").Hash) { throw 'TLS plugin differs from pinned SDK' }
    if ($names | Where-Object { $_ -match '(^|/)(\.deps|build|tests|config|plugin_config)/|fixture|frontend-test|\.pdb$|\.key$|\.cer$|vault\.dpapi|(^|/)Qt6.*\.dll$|(^|/)obs\.dll$|\.\./|^[/\\]|^[A-Za-z]:' }) { throw 'Forbidden content in binary ZIP' }
    foreach ($required in @('obs-plugins/64bit/obs-streambridge.dll','data/obs-plugins/obs-streambridge/browser/streambridge-browser.exe','data/obs-plugins/obs-streambridge/browser/streambridge-browser.dll','data/obs-plugins/obs-streambridge/browser/libcef.dll','data/obs-plugins/obs-streambridge/browser/CEF-LICENSE.txt','data/obs-plugins/obs-streambridge/browser/CHROMIUM-CREDITS.html','data/obs-plugins/obs-streambridge/docs/TEST-RESULTS.md')) {
        if ($required -notin $names) { throw "Missing binary ZIP entry: $required" }
    }
    foreach ($pair in @(@('data/obs-plugins/obs-streambridge/qt-plugins/imageformats/qgif.dll','.deps/obs-deps-qt6-2025-08-23-x64/plugins/imageformats/qgif.dll'),@('obs-plugins/64bit/obs-streambridge.dll','build/Release/obs-streambridge.dll'),@('data/obs-plugins/obs-streambridge/browser/streambridge-browser.dll','build-helper/Release/streambridge-browser.dll'))) {
        $stream = $zip.GetEntry($pair[0]).Open()
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','') } finally { $sha.Dispose(); $stream.Dispose() }
        if ($hash -ne (Get-FileHash (Join-Path $workspace $pair[1])).Hash) { throw 'Packaged DLL differs from built DLL' }
    }
    $manifestEntry = $zip.GetEntry('data/obs-plugins/obs-streambridge/uninstall-manifest.txt')
    if (-not $manifestEntry) { throw 'Missing uninstall manifest' }
    $reader = [IO.StreamReader]::new($manifestEntry.Open())
    try { $manifest = @($reader.ReadToEnd() -split '\r?\n' | Where-Object { $_ } | ForEach-Object { $_.Replace('\','/') }) }
    finally { $reader.Dispose() }
    $payloadFiles = @($names | Where-Object { -not $_.EndsWith('/') })
    $generated = @('data/obs-plugins/obs-streambridge/install.ini','data/obs-plugins/obs-streambridge/uninstall.exe')
    if (Compare-Object ($payloadFiles + $generated | Sort-Object) ($manifest | Sort-Object)) { throw 'ZIP payload/uninstall manifest mismatch' }
    if (($manifest | Sort-Object -Unique).Count -ne $manifest.Count) { throw 'Duplicate manifest entries' }
    Write-Output 'PASS: every packaged file is in the uninstall manifest; generated metadata is explicit'
    Write-Output 'PASS: binary allowlist, runtime notices and exact built DLL hashes'
} finally { $zip.Dispose() }
$source = [IO.Compression.ZipFile]::OpenRead("$Dist/obs-streambridge-$version-source.zip")
try {
    foreach ($entry in $source.Entries) {
        if ($entry.FullName -match '(^|/)(\.deps|build|dist|stage|\.git)/|\.pdb$|\.key$|\.cer$|vault\.dpapi|\.\./|^[/\\]|^[A-Za-z]:') { throw 'Forbidden source archive path' }
        if ($entry.FullName.EndsWith('/')) { continue }
        $stream = $entry.Open(); $sha = [Security.Cryptography.SHA256]::Create()
        try { $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','') } finally { $sha.Dispose(); $stream.Dispose() }
        if ($hash -ne (Get-FileHash (Join-Path $workspace $entry.FullName)).Hash) { throw "Source archive mismatch: $($entry.FullName)" }
    }
    Write-Output 'PASS: source archive matches workspace and excludes local dependencies/credentials'
} finally { $source.Dispose() }
