param(
    [Parameter(Mandatory=$true)][string]$ObsRoot,
    [switch]$TrustLoopbackCertificate
)
$ErrorActionPreference = 'Stop'
Write-Output 'RTMPS test wrapper started.'
$repo = (Resolve-Path "$PSScriptRoot/..").Path
$fixture = Join-Path $repo '.deps/rtmps-fixture'
$runner = Join-Path $repo 'build/Release/rtmps-integration.exe'
$server = Join-Path $repo '.deps/mediamtx/mediamtx.exe'
if (-not $TrustLoopbackCertificate) { throw 'Explicit approval and -TrustLoopbackCertificate are required. This temporarily changes CurrentUser Root trust.' }
$cert = [Security.Cryptography.X509Certificates.X509Certificate2]::new((Join-Path $fixture 'trusted.cer'))
if ($cert.Thumbprint -ne (Get-Content (Join-Path $fixture 'thumbprint.txt')).Trim() -or $cert.NotAfter -lt (Get-Date)) { throw 'Fixture certificate mismatch or expired; regenerate the fixture.' }
$store = [Security.Cryptography.X509Certificates.X509Store]::new('Root', 'CurrentUser')
$store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
Write-Output 'CurrentUser trust store opened.'
$added = $false
try {
    if ($store.Certificates.Find('FindByThumbprint', $cert.Thumbprint, $false).Count -ne 0) { throw 'Certificate already trusted; refusing to assume ownership.' }
    Write-Output 'Adding the approved temporary localhost certificate.'
    & certutil.exe -user -addstore Root (Join-Path $fixture 'trusted.cer') | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Could not add the temporary certificate.' }
    $added = $true
    Write-Output 'Temporary certificate installed; starting RTMPS runner.'
    $env:PATH = (Join-Path (Resolve-Path $ObsRoot).Path 'bin/64bit') + ';' + $env:PATH
    & $runner (Resolve-Path $ObsRoot).Path $server $fixture *> (Join-Path $repo 'build/rtmps-integration.log')
    $testExit = $LASTEXITCODE
    Get-Content (Join-Path $repo 'build/rtmps-integration.log') | Select-String '^PASS:|^FAIL:|^SERVER '
    if ($testExit -ne 0) { throw "RTMPS test failed with exit code $testExit; see build/rtmps-integration.log" }
} finally {
    if ($added) {
        Write-Output 'Confirm the Windows removal dialog for the temporary test certificate, if shown.'
        & certutil.exe -user -delstore Root $cert.Thumbprint | Out-Null
        $store.Close()
        $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadOnly)
        if ($store.Certificates.Find('FindByThumbprint', $cert.Thumbprint, $false).Count -ne 0) { throw "Cleanup failed: remove certificate $($cert.Thumbprint) from CurrentUser Root." }
        Write-Output 'Temporary certificate removed and absence verified.'
    }
    $store.Close(); $cert.Dispose()
}
