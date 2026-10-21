; SPDX-License-Identifier: GPL-2.0-or-later
Unicode true
!include MUI2.nsh
!include LogicLib.nsh
!include FileFunc.nsh
!include x64.nsh
!include nsDialogs.nsh
!ifndef VERSION
  !error "VERSION is required"
!endif
!ifndef PAYLOAD
  !error "PAYLOAD is required"
!endif
!include "${PAYLOAD}"
Name "OBS StreamBridge ${VERSION}"
OutFile "${OUTPUT}"
InstallDir "$PROGRAMFILES64\obs-studio"
RequestExecutionLevel user
!ifdef INSTALLER_TEST
  SetCompress off
!else
  SetCompressor /SOLID lzma
  SetCompressorDictSize 32
!endif
ShowInstDetails show
ShowUninstDetails show
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "OBS StreamBridge"
VIAddVersionKey "FileDescription" "StreamBridge installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "StreamBridge contributors"
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!define MUI_DIRECTORYPAGE_TEXT_TOP "Select the OBS Studio root containing bin\64bit\obs64.exe. Portable OBS is supported. Close OBS and StreamBridge browser processes first. Windows will request administrator permission if this folder is protected."
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE CheckDirectory
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
UninstPage custom un.SettingsPage un.SettingsLeave
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Russian"
Var ErrorText
Var Purge
Var PurgeBox
!include "diagnostics.nsh"
!include "preflight.nsh"
!include "path-safety.nsh"
!include "update.nsh"

Function .onInit
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  File /oname=streambridge-files.exe "${SB_FILES_HELPER}"
  File /oname=uninstall-manifest.txt "${SB_MANIFEST}"
  Call InitLifecycleLog
  !insertmacro Trace "" "START installer init INSTDIR=$INSTDIR"
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "StreamBridge requires Windows x64." /SD IDOK
    SetErrorLevel 2
    Quit
  ${EndIf}
  SetRegView 64
  ${GetParameters} $0
  StrCpy $ElevatedRetry 0
  ClearErrors
  ${GetOptions} $0 "/SB_ELEVATED" $1
  IfErrors +2
    StrCpy $ElevatedRetry 1
  ClearErrors
  ${GetOptions} $0 "/D=" $1
  ${IfNot} ${Errors}
    StrCpy $INSTDIR $1
    Goto directory_ready
  ${EndIf}
  ReadRegStr $0 HKLM "Software\OBS Studio" ""
  ${If} $0 != ""
    StrCpy $INSTDIR $0
  ${EndIf}
  directory_ready:
  Call WaitForUpdate
  !insertmacro Trace "" "END installer init INSTDIR=$INSTDIR"
FunctionEnd


Function CheckDirectory
  GetFullPathName $INSTDIR $INSTDIR
  !insertmacro GuardPluginPaths "" failed
  StrCpy $ErrorCode 2
  StrCpy $ErrorText "Select an OBS Studio 32+ x64 root folder."
  IfFileExists "$INSTDIR\bin\64bit\obs64.exe" 0 failed
  ClearErrors
  GetDLLVersion "$INSTDIR\bin\64bit\obs64.exe" $0 $1
  IfErrors failed
  IntOp $0 $0 >> 16
  IntCmp $0 32 +2 failed +2
  Goto failed
  ; Read the PE machine field; GetBinaryType alone also accepts ARM64.
  FileOpen $2 "$INSTDIR\bin\64bit\obs64.exe" r
  IfErrors failed
  FileSeek $2 60 SET
  FileReadByte $2 $0
  FileReadByte $2 $1
  IntOp $1 $1 << 8
  IntOp $0 $0 | $1
  FileReadByte $2 $1
  IntOp $1 $1 << 16
  IntOp $0 $0 | $1
  FileReadByte $2 $1
  IntOp $1 $1 << 24
  IntOp $0 $0 | $1
  IntOp $0 $0 + 4
  FileSeek $2 $0 SET
  FileReadByte $2 $0
  FileReadByte $2 $1
  FileClose $2
  ${If} $0 != 100
  ${OrIf} $1 != 134
    Goto failed
  ${EndIf}
  StrCpy $UpdateRetries 0
  check_running:
  Call CheckRunning
  ${If} $UpdateMode == 1
  ${AndIf} $ErrorCode == 32
  ${AndIf} $UpdateRetries < 30
    IntOp $UpdateRetries $UpdateRetries + 1
    Sleep 1000
    Goto check_running
  ${EndIf}
  ${If} $ErrorCode != 0
    Goto failed
  ${EndIf}
  StrCpy $ErrorCode 2
  IfFileExists "$INSTDIR\obs-plugins\64bit\obs-streambridge.dll" check_owner
  IfFileExists "$INSTDIR\data\obs-plugins\obs-streambridge\*.*" check_owner writable
  check_owner:
  ReadINIStr $0 "$INSTDIR\data\obs-plugins\obs-streambridge\package.ini" "StreamBridge" "Product"
  StrCpy $ErrorText "Conflicting StreamBridge files have no package ownership marker. Back them up and remove them manually before installation."
  StrCmp $0 "obs-streambridge" writable failed
  writable:
  !insertmacro CheckWritable "" write_failed
  Return
  write_failed:
  !insertmacro WriteFailure ""
  failed:
  StrCpy $UpdateStatus "failed"
  Call WriteUpdateResult
  !ifdef INSTALLER_TEST
    FileOpen $9 "$EXEDIR\installer-test.log" w
    FileWrite $9 "$INSTDIR$\r$\n$ErrorText$\r$\nerror: $ErrorCode$\r$\n"
    FileClose $9
  !endif
  IfSilent +2
    MessageBox MB_ICONSTOP "$ErrorText" /SD IDOK
  SetErrorLevel $ErrorCode
  Abort
FunctionEnd

Section "StreamBridge" Main
  Call CheckDirectory
  Call CheckUpdateCancelled
  ; Extract only into our private NSIS directory. The helper copies to opened handles.
  SetOverwrite on
  ClearErrors
  !define SB_EXTRACT_PAYLOAD
  !include "${PAYLOAD}"
  !undef SB_EXTRACT_PAYLOAD
  IfErrors install_failed
  !insertmacro Trace "" "START write install.ini Root=$INSTDIR"
  ; Win32 profile APIs create ANSI INI files unless a Unicode BOM already exists.
  ; Write UTF-16 explicitly so Cyrillic paths survive non-Cyrillic Windows ACPs.
  ClearErrors
  FileOpen $2 "$PLUGINSDIR\payload\data\obs-plugins\obs-streambridge\install.ini" w
  IfErrors install_failed
  FileWriteUTF16LE /BOM $2 "[Install]$\r$\nRoot=$INSTDIR$\r$\n"
  FileClose $2
  IfErrors install_failed
  FlushINI "$PLUGINSDIR\payload\data\obs-plugins\obs-streambridge\install.ini"
  ClearErrors
  ReadINIStr $0 "$PLUGINSDIR\payload\data\obs-plugins\obs-streambridge\install.ini" "Install" "Root"
  !insertmacro Trace "" "END write install.ini readback=$0 expected=$INSTDIR"
  IfErrors install_failed
  StrCmp $0 $INSTDIR 0 install_failed
  WriteUninstaller "$PLUGINSDIR\payload\data\obs-plugins\obs-streambridge\uninstall.exe"
  !insertmacro Trace "" "START handle-based install root=$INSTDIR"
  nsExec::ExecToStack '"$PLUGINSDIR\streambridge-files.exe" install "$INSTDIR" "$PLUGINSDIR\uninstall-manifest.txt" "$PLUGINSDIR\payload"'
  !insertmacro FilesystemResult
  !insertmacro Trace "" "END handle-based install code=$ErrorCode detail=$1"
  StrCmp $ErrorCode "0" native_installed
  !insertmacro WriteFailure ""
  Goto native_failed
  native_installed:
  IfFileExists "$INSTDIR\portable_mode.txt" installed
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\OBSStreamBridge" "DisplayName" "OBS StreamBridge"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\OBSStreamBridge" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\OBSStreamBridge" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\OBSStreamBridge" "UninstallString" '$\"$INSTDIR\data\obs-plugins\obs-streambridge\uninstall.exe$\"'
  installed:
  StrCpy $ErrorCode 0
  StrCpy $UpdateStatus "success"
  Call WriteUpdateResult
  SetErrorLevel 0
  !insertmacro Trace "" "END installer code=0"
  Goto done
  install_failed:
  StrCpy $ErrorCode 3
  native_failed:
  !insertmacro Trace "" "END installer code=$ErrorCode"
  StrCpy $UpdateStatus "failed"
  Call WriteUpdateResult
  IfSilent +2
    MessageBox MB_ICONSTOP "Installation failed. Keep OBS closed and rerun the installer. User settings were preserved." /SD IDOK
  SetErrorLevel $ErrorCode
  Abort
  done:
SectionEnd

Function un.onInit
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  File /oname=streambridge-files.exe "${SB_FILES_HELPER}"
  File /oname=uninstall-manifest.txt "${SB_MANIFEST}"
  Call un.InitLifecycleLog
  !insertmacro Trace "un." "START un.onInit INSTDIR=$INSTDIR EXEDIR=$EXEDIR"
  SetRegView 64
  ; NSIS sets INSTDIR to the uninstaller's original directory.
  GetFullPathName $INSTDIR $INSTDIR
  !insertmacro Trace "un." "START read install.ini path=$INSTDIR\install.ini"
  ClearErrors
  ReadINIStr $0 "$INSTDIR\install.ini" "Install" "Root"
  IfErrors invalid_root_read
  !insertmacro Trace "un." "END read install.ini result=ok Root=$0"
  StrCmp $0 "" invalid
  System::Call 'shlwapi::PathIsRelativeW(w r0) i.r2'
  StrCmp $2 0 0 invalid
  GetFullPathName $0 $0
  GetFullPathName $1 "$0\data\obs-plugins\obs-streambridge"
  !insertmacro Trace "un." "START compare paths actual=$INSTDIR expected=$1"
  StrCmp $INSTDIR $1 0 invalid_path_compare
  !insertmacro Trace "un." "END compare paths equal"
  !insertmacro Trace "un." "START read package.ini path=$INSTDIR\package.ini"
  ClearErrors
  ReadINIStr $1 "$INSTDIR\package.ini" "StreamBridge" "Product"
  IfErrors invalid_product_read
  !insertmacro Trace "un." "END read package.ini result=ok Product=$1"
  !insertmacro Trace "un." "START compare product actual=$1 expected=obs-streambridge"
  StrCmp $1 "obs-streambridge" 0 invalid_product_compare
  !insertmacro Trace "un." "END compare product equal"
  StrCpy $INSTDIR $0
  StrCpy $Purge 0
  ${GetParameters} $0
  StrCpy $ElevatedRetry 0
  ClearErrors
  ${GetOptions} $0 "/SB_ELEVATED" $1
  IfErrors +2
    StrCpy $ElevatedRetry 1
  ClearErrors
  ${GetOptions} $0 "/PURGESETTINGS" $1
  IfErrors +2
    StrCpy $Purge 1
  !insertmacro Trace "un." "END un.onInit root=$INSTDIR purge=$Purge"
  Return
  invalid_path_compare:
  !insertmacro Trace "un." "END compare paths different"
  Goto invalid
  invalid_product_compare:
  !insertmacro Trace "un." "END compare product different"
  Goto invalid
  invalid_root_read:
  !insertmacro Trace "un." "END read install.ini result=failed Root=$0"
  Goto invalid
  invalid_product_read:
  !insertmacro Trace "un." "END read package.ini result=failed Product=$1"
  invalid:
  !insertmacro Trace "un." "REJECT invalid installation marker; no files removed"
  MessageBox MB_ICONSTOP "Invalid installation marker. No files were removed." /SD IDOK
  !insertmacro Trace "un." "END uninstall code=2 invalid marker"
  SetErrorLevel 2
  Quit
FunctionEnd
Function un.SettingsPage
  nsDialogs::Create 1018
  Pop $0
  ${NSD_CreateLabel} 0 0 100% 40u "Settings are preserved by default. Removing settings also permanently removes encrypted stream keys, OAuth sessions and browser credentials for this OBS configuration."
  Pop $0
  ${NSD_CreateCheckbox} 0 50u 100% 22u "Remove StreamBridge settings and encrypted credentials"
  Pop $PurgeBox
  ${NSD_SetState} $PurgeBox $Purge
  nsDialogs::Show
FunctionEnd
Function un.SettingsLeave
  ${NSD_GetState} $PurgeBox $Purge
FunctionEnd
Section "Uninstall"
  ; Validate EVERY planned area before CheckWritable or the first deletion.
  !insertmacro GuardPluginPaths "un." un_failed
  ${If} $Purge == 1
    IfFileExists "$INSTDIR\portable_mode.txt" guard_portable_settings
    !insertmacro GuardDestination "$APPDATA\obs-studio\plugin_config\obs-streambridge" "tree" "un." un_failed
    Goto guard_settings_done
    guard_portable_settings:
    !insertmacro GuardDestination "$INSTDIR\config\obs-studio\plugin_config\obs-streambridge" "tree" "un." un_failed
    guard_settings_done:
  ${EndIf}
  !insertmacro Trace "un." "START un.CheckRunning root=$INSTDIR"
  Call un.CheckRunning
  !insertmacro Trace "un." "END un.CheckRunning code=$ErrorCode"
  ${If} $ErrorCode != 0
    Goto un_failed
  ${EndIf}
  !insertmacro Trace "un." "START CheckWritable root=$INSTDIR"
  !insertmacro CheckWritable "un." un_write_failed
  !insertmacro Trace "un." "END CheckWritable code=$ErrorCode"
  StrCpy $ErrorCode 3
  StrCpy $ErrorText "Could not remove StreamBridge files. Check folder permissions and retry."
  StrCpy $2 ""
  ${If} $Purge == 1
    StrCpy $2 "purge"
  ${EndIf}
  !insertmacro Trace "un." "START handle-based uninstall root=$INSTDIR purge=$Purge"
  nsExec::ExecToStack '"$PLUGINSDIR\streambridge-files.exe" uninstall "$INSTDIR" "$PLUGINSDIR\uninstall-manifest.txt" $2'
  !insertmacro FilesystemResult
  !insertmacro Trace "un." "END handle-based uninstall code=$ErrorCode detail=$1"
  StrCmp $ErrorCode "0" 0 un_failed
  ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\OBSStreamBridge" "InstallLocation"
  ${If} $0 == $INSTDIR
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\OBSStreamBridge"
  ${EndIf}
  SetErrorLevel 0
  !insertmacro Trace "un." "END uninstall code=0"
  Goto un_done
  un_write_failed:
  !insertmacro Trace "un." "END CheckWritable code=$ErrorCode path=$ErrorPath"
  !insertmacro WriteFailure "un."
  un_failed:
  !insertmacro Trace "un." "END uninstall code=$ErrorCode error=$ErrorText"
  IfSilent +2
    MessageBox MB_ICONSTOP "$ErrorText" /SD IDOK
  SetErrorLevel $ErrorCode
  Abort
  un_done:
SectionEnd

Function un.onUninstSuccess
  !insertmacro Trace "un." "EXIT un.onUninstSuccess code=0"
FunctionEnd
Function un.onUninstFailed
  !insertmacro Trace "un." "EXIT un.onUninstFailed code=$ErrorCode"
FunctionEnd
