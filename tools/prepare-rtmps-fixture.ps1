param([string]$Directory = "$PSScriptRoot/../.deps/rtmps-fixture")
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Directory | Out-Null
$Directory = (Resolve-Path $Directory).Path
foreach ($name in @('trusted', 'untrusted')) {
    $key = [Security.Cryptography.RSA]::Create(2048)
    $request = [Security.Cryptography.X509Certificates.CertificateRequest]::new(
        "CN=StreamBridge localhost test $name", $key,
        [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $san = [Security.Cryptography.X509Certificates.SubjectAlternativeNameBuilder]::new()
    $san.AddDnsName('localhost')
    $san.AddIpAddress([Net.IPAddress]::Parse('127.0.0.1'))
    $request.CertificateExtensions.Add($san.Build())
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($false, $false, 0, $true))
    $usage = [Security.Cryptography.OidCollection]::new()
    [void]$usage.Add([Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.1'))
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($usage, $true))
    $cert = $request.CreateSelfSigned([DateTimeOffset]::UtcNow.AddMinutes(-5), [DateTimeOffset]::UtcNow.AddHours(4))
    [IO.File]::WriteAllText((Join-Path $Directory "$name.key"), $key.ExportPkcs8PrivateKeyPem())
    [IO.File]::WriteAllText((Join-Path $Directory "$name.crt"), $cert.ExportCertificatePem())
    [IO.File]::WriteAllBytes((Join-Path $Directory "$name.cer"), $cert.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))
    if ($name -eq 'trusted') { $cert.Thumbprint | Set-Content (Join-Path $Directory 'thumbprint.txt') }
    $cert.Dispose(); $key.Dispose()
}
for ($i = 0; $i -lt 3; $i++) {
    $tlsPort = 19443 + $i
    $apiPort = 19997 + $i
    $certificate = if ($i -eq 2) { 'untrusted' } else { 'trusted' }
    @"
logLevel: info
rtsp: false
hls: false
webrtc: false
srt: false
moq: false
rtmp: true
rtmpEncryption: strict
rtmpsAddress: 127.0.0.1:$tlsPort
rtmpServerKey: $certificate.key
rtmpServerCert: $certificate.crt
api: true
apiAddress: 127.0.0.1:$apiPort
authInternalUsers:
- user: any
  ips: ['127.0.0.1']
  permissions:
  - action: publish
  - action: read
  - action: api
paths:
  all_others:
"@ | Set-Content (Join-Path $Directory "server-$i.yml") -Encoding utf8
}
Write-Output "Prepared three loopback-only RTMPS endpoints. No certificate trust has been changed."
Get-Content (Join-Path $Directory 'thumbprint.txt')
