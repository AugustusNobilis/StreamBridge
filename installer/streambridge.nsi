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
!include "preflight.nsh"
!include "update.nsh"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "StreamBridge requires Windows x64."
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
FunctionEnd


Function CheckDirectory
  GetFullPathName $INSTDIR $INSTDIR
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
    MessageBox MB_ICONSTOP "$ErrorText"
  SetErrorLevel $ErrorCode
  Abort
FunctionEnd

Section "StreamBridge" Main
  Call CheckDirectory
  ; This dedicated directory contains only files from our package. User settings
  ; live under OBS plugin_config and are never touched by installation/upgrade.
  RMDir /r "$INSTDIR\data\obs-plugins\obs-streambridge\browser"
  IfFileExists "$INSTDIR\data\obs-plugins\obs-streambridge\browser\*.*" install_failed
  SetOverwrite on
  ClearErrors
  !include "${PAYLOAD}"
  IfErrors install_failed
  ; Chromium's LPAC sandbox needs read/execute access to its own runtime files.
  ; No OBS configuration, user profile or unrelated folder ACL is changed.
  nsExec::ExecToStack '"$SYSDIR\icacls.exe" "$INSTDIR\data\obs-plugins\obs-streambridge\browser" /grant "*S-1-15-2-2:(OI)(CI)(RX)"'
  Pop $0
  Pop $1
  StrCmp $0 "0" 0 install_failed
  WriteINIStr "$INSTDIR\data\obs-plugins\obs-streambridge\install.ini" "Install" "Root" "$INSTDIR"
  WriteUninstaller "$INSTDIR\data\obs-plugins\obs-streambridge\uninstall.exe"
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
  Goto done
  install_failed:
  StrCpy $ErrorCode 3
  StrCpy $UpdateStatus "failed"
  Call WriteUpdateResult
  IfSilent +2
    MessageBox MB_ICONSTOP "Installation failed. Keep OBS closed and rerun the installer. User settings were preserved."
  SetErrorLevel 3
  Abort
  done:
SectionEnd

Function un.onInit
  SetRegView 64
  ; NSIS sets INSTDIR to the uninstaller's original directory.
  ReadINIStr $0 "$INSTDIR\install.ini" "Install" "Root"
  GetFullPathName $1 "$0\data\obs-plugins\obs-streambridge"
  StrCmp $INSTDIR $1 0 invalid
  ReadINIStr $1 "$INSTDIR\package.ini" "StreamBridge" "Product"
  StrCmp $1 "obs-streambridge" 0 invalid
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
  Return
  invalid:
  MessageBox MB_ICONSTOP "Invalid installation marker. No files were removed."
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
  Call un.CheckRunning
  ${If} $ErrorCode != 0
    Goto un_failed
  ${EndIf}
  !insertmacro CheckWritable "un." un_write_failed
  StrCpy $ErrorCode 3
  StrCpy $ErrorText "Could not remove StreamBridge files. Check folder permissions and retry."
  ClearErrors
  Delete "$INSTDIR\obs-plugins\64bit\obs-streambridge.dll"
  RMDir /r "$INSTDIR\data\obs-plugins\obs-streambridge"
  IfErrors un_failed
  ${If} $Purge == 1
    IfFileExists "$INSTDIR\portable_mode.txt" portable_config
    RMDir /r "$APPDATA\obs-studio\plugin_config\obs-streambridge"
    Goto removed_config
    portable_config:
    RMDir /r "$INSTDIR\config\obs-studio\plugin_config\obs-streambridge"
    removed_config:
  ${EndIf}
  ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\OBSStreamBridge" "InstallLocation"
  ${If} $0 == $INSTDIR
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\OBSStreamBridge"
  ${EndIf}
  SetErrorLevel 0
  Goto un_done
  un_write_failed:
  !insertmacro WriteFailure "un."
  un_failed:
  IfSilent +2
    MessageBox MB_ICONSTOP "$ErrorText"
  SetErrorLevel $ErrorCode
  Abort
  un_done:
SectionEnd
