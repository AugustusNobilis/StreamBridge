; SPDX-License-Identifier: GPL-2.0-or-later
; Shared installer/uninstaller preflight. OBS itself is inspected read-only.
Var ErrorCode
Var ErrorPath
Var ElevatedRetry

!macro PreflightFunctions PREFIX
Function ${PREFIX}CheckRunning
  StrCpy $ErrorCode 0
  !insertmacro Trace "${PREFIX}" "START RmStartSession"
  System::Call 'rstrtmgr::RmStartSession(*i .r4, i 0, w .r5) i.r0'
  !insertmacro Trace "${PREFIX}" "END RmStartSession code=$0"
  ${If} $0 != 0
    StrCpy $ErrorCode $0
    Goto running_error
  ${EndIf}
  ; Explicit buffers keep all five Unicode paths alive for the API call.
  System::Call '*(&w${NSIS_MAX_STRLEN} "$INSTDIR\bin\64bit\obs64.exe") p.r5'
  System::Call '*(&w${NSIS_MAX_STRLEN} "$INSTDIR\obs-plugins\64bit\obs-streambridge.dll") p.r6'
  System::Call '*(&w${NSIS_MAX_STRLEN} "$INSTDIR\data\obs-plugins\obs-streambridge\browser\streambridge-browser.exe") p.r7'
  System::Call '*(&w${NSIS_MAX_STRLEN} "$INSTDIR\data\obs-plugins\obs-streambridge\browser\streambridge-browser.dll") p.r8'
  System::Call '*(&w${NSIS_MAX_STRLEN} "$INSTDIR\data\obs-plugins\obs-streambridge\browser\libcef.dll") p.r9'
  System::Call '*(p r5, p r6, p r7, p r8, p r9) p.r3'
  !insertmacro Trace "${PREFIX}" "START RmRegisterResources"
  System::Call 'rstrtmgr::RmRegisterResources(i r4, i 5, p r3, i 0, p 0, i 0, p 0) i.r0'
  !insertmacro Trace "${PREFIX}" "END RmRegisterResources code=$0"
  System::Free $3
  System::Free $5
  System::Free $6
  System::Free $7
  System::Free $8
  System::Free $9
  ${If} $0 == 0
    !insertmacro Trace "${PREFIX}" "START RmGetList"
    System::Call 'rstrtmgr::RmGetList(i r4, *i .r1, *i 0, p 0, *i .r2) i.r0'
    !insertmacro Trace "${PREFIX}" "END RmGetList code=$0"
  ${EndIf}
  StrCpy $ErrorCode $0
  !insertmacro Trace "${PREFIX}" "START RmEndSession"
  System::Call 'rstrtmgr::RmEndSession(i r4)'
  !insertmacro Trace "${PREFIX}" "END RmEndSession"
  ${If} $ErrorCode == 234
    StrCpy $ErrorCode 32
    StrCpy $ErrorText "Files in this OBS installation are in use. Close OBS and StreamBridge browser processes, then retry.$\r$\n$INSTDIR"
    Return
  ${EndIf}
  ${If} $ErrorCode == 0
    Return
  ${EndIf}
  running_error:
  StrCpy $ErrorText "Windows could not check whether OBS files are in use (error $ErrorCode). No files were changed.$\r$\n$INSTDIR"
FunctionEnd

Function ${PREFIX}CheckWriteFile
  IfFileExists "$ErrorPath" 0 file_done
  ; Only files owned and replaced by StreamBridge need write permission.
  System::Call 'kernel32::CreateFileW(w "$ErrorPath", i 0x40000000, i 7, p 0, i 3, i 0, p 0) p.r0 ?e'
  Pop $1
  ${If} $0 == -1
    StrCpy $ErrorCode $1
    Return
  ${EndIf}
  System::Call 'kernel32::CloseHandle(p r0)'
  file_done:
FunctionEnd

Function ${PREFIX}CheckWriteDirectory
  ; Test the nearest existing parent when installing into a fresh OBS root.
  directory_parent:
  System::Call 'kernel32::GetFileAttributesW(w "$ErrorPath") i.r0'
  ${If} $0 == -1
    ${GetParent} "$ErrorPath" $1
    ${If} $1 == ""
    ${OrIf} $1 == $ErrorPath
      StrCpy $ErrorCode 3
      Return
    ${EndIf}
    StrCpy $ErrorPath $1
    Goto directory_parent
  ${EndIf}
  System::Call 'kernel32::GetTempFileNameW(w "$ErrorPath", w "sbr", i 0, w .r3) i.r0 ?e'
  Pop $1
  ${If} $0 == 0
    StrCpy $ErrorCode $1
    Return
  ${EndIf}
  System::Call 'kernel32::DeleteFileW(w r3) i.r0 ?e'
  Pop $1
  ${If} $0 == 0
    StrCpy $ErrorCode $1
  ${EndIf}
FunctionEnd

Function ${PREFIX}MaybeElevate
  ; Unattended installs never unexpectedly open a consent dialog.
  IfSilent elevation_done
  ${If} $ElevatedRetry == 1
    Return
  ${EndIf}
  System::Call 'kernel32::GetCurrentProcess() p.r0'
  System::Call 'advapi32::OpenProcessToken(p r0, i 8, *p .r1) i.r0'
  ${If} $0 == 0
    Return
  ${EndIf}
  System::Call 'advapi32::GetTokenInformation(p r1, i 20, *i .r2, i 4, *i .r3) i.r0'
  System::Call 'kernel32::CloseHandle(p r1)'
  ${If} $0 == 0
  ${OrIf} $2 != 0
    Return
  ${EndIf}
  ClearErrors
  !if "${PREFIX}" == "un."
    StrCpy $0 "/SB_ELEVATED"
    ${If} $Purge == 1
      StrCpy $0 "$0 /PURGESETTINGS"
    ${EndIf}
    ExecShell "runas" "$INSTDIR\data\obs-plugins\obs-streambridge\uninstall.exe" "$0" SW_SHOWNORMAL
  !else
    ; /D consumes the remaining command line; do not quote it or append flags.
    ExecShell "runas" "$EXEPATH" "/SB_ELEVATED /D=$INSTDIR" SW_SHOWNORMAL
  !endif
  IfErrors elevation_cancelled
  Quit
  elevation_cancelled:
  StrCpy $ErrorText "Administrator permission was not granted. No files were changed. Run this installer as administrator or select a writable portable OBS folder.$\r$\n$ErrorPath"
  elevation_done:
FunctionEnd
!macroend

!macro RequireWritableFile PATH PREFIX FAIL
  StrCpy $ErrorPath "${PATH}"
  !insertmacro Trace "${PREFIX}" "START CheckWriteFile path=$ErrorPath"
  Call ${PREFIX}CheckWriteFile
  !insertmacro Trace "${PREFIX}" "END CheckWriteFile code=$ErrorCode"
  ${If} $ErrorCode != 0
    Goto ${FAIL}
  ${EndIf}
!macroend
!macro RequireWritableDirectory PATH PREFIX FAIL
  StrCpy $ErrorPath "${PATH}"
  !insertmacro Trace "${PREFIX}" "START CheckWriteDirectory path=$ErrorPath"
  Call ${PREFIX}CheckWriteDirectory
  !insertmacro Trace "${PREFIX}" "END CheckWriteDirectory code=$ErrorCode"
  ${If} $ErrorCode != 0
    Goto ${FAIL}
  ${EndIf}
!macroend

!macro CheckWritable PREFIX FAIL
  StrCpy $ErrorCode 0
  !insertmacro RequireWritableFile "$INSTDIR\obs-plugins\64bit\obs-streambridge.dll" "${PREFIX}" ${FAIL}
  !insertmacro RequireWritableFile "$INSTDIR\data\obs-plugins\obs-streambridge\browser\streambridge-browser.exe" "${PREFIX}" ${FAIL}
  !insertmacro RequireWritableFile "$INSTDIR\data\obs-plugins\obs-streambridge\browser\streambridge-browser.dll" "${PREFIX}" ${FAIL}
  !insertmacro RequireWritableFile "$INSTDIR\data\obs-plugins\obs-streambridge\browser\libcef.dll" "${PREFIX}" ${FAIL}
  !insertmacro RequireWritableDirectory "$INSTDIR" "${PREFIX}" ${FAIL}
  !insertmacro RequireWritableDirectory "$INSTDIR\obs-plugins\64bit" "${PREFIX}" ${FAIL}
  !insertmacro RequireWritableDirectory "$INSTDIR\data\obs-plugins\obs-streambridge" "${PREFIX}" ${FAIL}
!macroend

!macro WriteFailure PREFIX
  ${If} $ErrorCode == 5
    StrCpy $ErrorText "Access denied to this OBS folder (Windows error 5). Run the installer as administrator, or use a writable portable OBS folder. Also check read-only attributes and folder permissions.$\r$\n$ErrorPath"
    Call ${PREFIX}MaybeElevate
  ${ElseIf} $ErrorCode == 32
    StrCpy $ErrorText "A StreamBridge file is in use (Windows error 32). Close OBS and browser processes, then retry.$\r$\n$ErrorPath"
  ${Else}
    StrCpy $ErrorText "Cannot write to this OBS folder (Windows error $ErrorCode). Check the folder permissions and available disk space.$\r$\n$ErrorPath"
  ${EndIf}
!macroend

!insertmacro PreflightFunctions ""
!insertmacro PreflightFunctions "un."
