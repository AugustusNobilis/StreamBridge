$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lifecycle-process.ps1"
$workspace = (Resolve-Path "$PSScriptRoot/..").Path
$fixture = Join-Path $workspace ('build/lifecycle-diagnostics/updater-failure-' + [Guid]::NewGuid().ToString('N'))
Initialize-Lifecycle $fixture
$watch = [Diagnostics.Stopwatch]::StartNew()
$source = Join-Path $fixture 'fault-installer.nsi'
$installer = Join-Path $fixture 'fault-installer.exe'
@'
Unicode true
Name "Synthetic failure after status write"
OutFile "${OUTPUT}"
RequestExecutionLevel user
SilentInstall silent
Section
  ; This fixture never accepts a real OBS path.
  GetFullPathName $INSTDIR $INSTDIR
  StrLen $0 "${ALLOWED}"
  StrCpy $1 "$INSTDIR" $0
  StrCmp $1 "${ALLOWED}" allowed
  SetErrorLevel 2
  Quit
  allowed:
  Sleep 1500
  FileOpen $0 "$INSTDIR\obs-plugins\64bit\obs-streambridge.dll" w
  FileWrite $0 "SYNTHETIC CHANGED PAYLOAD"
  FileClose $0
  WriteINIStr "$EXEDIR\update-result.ini" "Update" "Status" "success"
  FlushINI "$EXEDIR\update-result.ini"
  Sleep 500
  SetErrorLevel 7
SectionEnd
'@ | Set-Content $source -Encoding utf8
$null = Invoke-LifecycleProcess "$workspace/.deps/nsis-3.11/makensis.exe" ('/V2 "/DOUTPUT={0}" "/DALLOWED={1}\.deps\updater-test-" "{2}"' -f $installer,$workspace,$source) compile 30
$caught = $false
try { & "$PSScriptRoot/test-updater.ps1" -Installer $installer -ImmediateExit -OperationTimeoutSeconds 30 }
catch {
    if ($_.Exception.Message -notmatch 'Role installer expected exit 0, got 7 \(parent=0\)') { throw }
    $caught = $true
    # test-updater uses its own script scope for diagnostics.
    Write-Lifecycle "PASS updater regression rejects synthetic success + installer=7 + parent=0: $($_.Exception.Message)"
}
if (-not $caught) { throw 'Updater suite accepted installer exit 7 despite parent exit 0' }
Write-Lifecycle ('SUITE END updater-failure status=PASS duration={0:F3}s' -f $watch.Elapsed.TotalSeconds)
