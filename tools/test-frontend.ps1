param([string]$ObsRoot = "$PSScriptRoot/../.deps/obs-portable")
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path "$PSScriptRoot/..").Path
$ObsRoot = (Resolve-Path -LiteralPath $ObsRoot).Path
if (-not $ObsRoot.StartsWith($workspace + [IO.Path]::DirectorySeparatorChar) -or -not (Test-Path "$ObsRoot/portable_mode.txt")) { throw 'Use only a dedicated workspace portable OBS' }
$exe = Join-Path $ObsRoot 'bin/64bit/obs64.exe'
if (Get-Process obs64 -ErrorAction SilentlyContinue | Where-Object Path -EQ $exe) { throw 'Close the test OBS first' }
$fixture = Join-Path $workspace '.deps/frontend-fixture'
$moduleData = Join-Path $ObsRoot 'data/obs-plugins/streambridge-frontend-test'
New-Item -ItemType Directory -Force $fixture,$moduleData | Out-Null
Set-Content "$moduleData/run.txt" 'STREAMBRIDGE_LOCAL_FRONTEND_TEST_V1' -Encoding ascii
Copy-Item "$workspace/build/Release/streambridge-frontend-test.dll" "$ObsRoot/obs-plugins/64bit/streambridge-frontend-test.dll"
Copy-Item "$workspace/build/Release/obs-streambridge.dll" "$ObsRoot/obs-plugins/64bit/obs-streambridge.dll"
Copy-Item "$workspace/stage/data/obs-plugins/obs-streambridge" "$ObsRoot/data/obs-plugins" -Recurse -Force
& icacls.exe "$ObsRoot/data/obs-plugins/obs-streambridge/browser" /grant '*S-1-15-2-2:(OI)(CI)(RX)' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Helper sandbox ACL failed' }
$config = Join-Path $ObsRoot 'config/obs-studio'
$userConfig = Join-Path $config 'user.ini'
$previous = if (Test-Path $userConfig) { [IO.File]::ReadAllBytes($userConfig) } else { $null }
$pluginPreferences = Join-Path $config 'plugin_config/obs-streambridge/ui.ini'
$previousPreferences = if (Test-Path $pluginPreferences) { [IO.File]::ReadAllBytes($pluginPreferences) } else { $null }
New-Item -ItemType Directory -Force "$config/basic/profiles/StreamBridge-Test","$config/basic/scenes" | Out-Null
Set-Content "$config/basic/profiles/StreamBridge-Test/basic.ini" @'
[General]
Name=StreamBridge-Test
[Output]
Mode=Simple
[SimpleOutput]
StreamEncoder=x264
VBitrate=500
ABitrate=128
[Video]
BaseCX=640
BaseCY=360
OutputCX=640
OutputCY=360
FPSType=0
FPSCommon=30
[Audio]
SampleRate=48000
ChannelSetup=Stereo
'@ -Encoding utf8
Set-Content "$config/basic/scenes/StreamBridge-Test.json" '{"name":"StreamBridge-Test","current_scene":"Test","current_program_scene":"Test","sources":[{"id":"scene","versioned_id":"scene","name":"Test","settings":{"items":[]}}],"scene_order":[{"name":"Test"}]}' -Encoding utf8
Set-Content "$fixture/server.yml" @'
logLevel: info
rtsp: false
hls: false
webrtc: false
srt: false
moq: false
rtmp: true
rtmpAddress: 127.0.0.1:19365
api: false
authInternalUsers:
- user: any
  ips: ['127.0.0.1']
  permissions:
  - action: publish
  - action: read
paths:
  all_others:
'@ -Encoding ascii
$server = $null
$obs = $null
try {
    New-Item -ItemType Directory -Force (Split-Path $pluginPreferences -Parent) | Out-Null
    Set-Content -LiteralPath $pluginPreferences -Value @('[General]','language=en-US') -Encoding utf8
    Set-Content $userConfig @'
[General]
FirstRun=true
ConfirmOnExit=false
[Basic]
Profile=StreamBridge-Test
ProfileDir=StreamBridge-Test
SceneCollection=StreamBridge-Test
SceneCollectionFile=StreamBridge-Test.json
[BasicWindow]
PreviewEnabled=false
WarnBeforeStartingStream=false
WarnBeforeStoppingStream=false
'@ -Encoding utf8
    $server = Start-Process -FilePath "$workspace/.deps/mediamtx/mediamtx.exe" -ArgumentList ('"' + "$fixture/server.yml" + '"') -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput "$fixture/server.log" -RedirectStandardError "$fixture/server-error.log"
    $obs = Start-Process -FilePath $exe -WorkingDirectory "$ObsRoot/bin/64bit" -ArgumentList '--portable','--multi','--disable-updater','--profile','StreamBridge-Test','--collection','StreamBridge-Test' -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(90)
    while (-not $obs.WaitForExit(1000) -and [DateTime]::UtcNow -lt $deadline) { }
    if (-not $obs.HasExited) { throw 'Frontend fixture did not close within 90 seconds' }
    Copy-Item "$moduleData/run.txt.log" "$workspace/build/frontend-tests.log"
    $report = Get-Content "$workspace/build/frontend-tests.log" -Raw
    Write-Output $report
    if ($obs.ExitCode -ne 0 -or $report.Contains('FAIL:') -or -not $report.Contains('PASS: test module unloaded cleanly')) { throw "Frontend validation failed (OBS exit $($obs.ExitCode))" }
    if (-not (Select-String -LiteralPath "$fixture/server.log" -Pattern 'is publishing' -Quiet)) { throw 'Local RTMP server did not confirm media publication' }
    Write-Output 'PASS: frontend coexistence and OBS shutdown; primary uses loopback RTMP, plugin sinks are instrumented libobs outputs'
} finally {
    if ($obs -and -not $obs.HasExited) { Stop-Process -Id $obs.Id -Force }
    if ($server -and -not $server.HasExited) { Stop-Process -Id $server.Id -Force }
    if ($null -ne $previous) { [IO.File]::WriteAllBytes($userConfig,$previous) }
    if ($null -ne $previousPreferences) { [IO.File]::WriteAllBytes($pluginPreferences,$previousPreferences) }
    else { Remove-Item -LiteralPath $pluginPreferences -ErrorAction SilentlyContinue }
    Remove-Item -LiteralPath "$ObsRoot/obs-plugins/64bit/streambridge-frontend-test.dll" -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath "$moduleData/run.txt" -ErrorAction SilentlyContinue
}
