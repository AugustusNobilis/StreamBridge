; SPDX-License-Identifier: GPL-2.0-or-later
!include StrFunc.nsh
${Using:StrFunc} StrRep
${Using:StrFunc} StrCase
Var UpdateMode
Var UpdateStatus
Var UpdatePid
Var UpdateCreated
Var UpdateHandle
Var UpdateRetries

Function WriteUpdateResult
  ${If} $UpdateMode == 1
    WriteINIStr "$EXEDIR\update-result.tmp" "Update" "Status" "$UpdateStatus"
    WriteINIStr "$EXEDIR\update-result.tmp" "Update" "Error" "$ErrorCode"
    WriteINIStr "$EXEDIR\update-result.tmp" "Update" "Version" "${VERSION}"
    System::Call 'kernel32::GetCurrentProcessId() i.r0'
    WriteINIStr "$EXEDIR\update-result.tmp" "Update" "Pid" "$0"
    System::Call 'kernel32::GetCurrentProcess() p.r0'
    System::Call 'kernel32::GetProcessTimes(p r0, *l .r1, *l .r2, *l .r3, *l .r4)'
    WriteINIStr "$EXEDIR\update-result.tmp" "Update" "Created" "$1"
    FlushINI "$EXEDIR\update-result.tmp"
    StrCpy $5 0
    result_replace:
    System::Call 'kernel32::MoveFileExW(w "$EXEDIR\update-result.tmp", w "$EXEDIR\update-result.ini", i 9) i.r0'
    ${If} $0 == 0
    ${AndIf} $5 < 20
      IntOp $5 $5 + 1
      Sleep 50
      Goto result_replace
    ${EndIf}
  ${EndIf}
FunctionEnd

Function WaitForUpdate
  StrCpy $UpdateMode 0
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/UPDATEPID=" $UpdatePid
  IfErrors update_regular
  StrCpy $UpdateMode 1
  StrCpy $ErrorCode 2
  StrCpy $UpdateStatus "failed"
  ${GetOptions} $0 "/UPDATECREATED=" $UpdateCreated
  IfErrors update_invalid
  ${If} $UpdatePid == ""
  ${OrIf} $UpdatePid == 0
  ${OrIf} $UpdateCreated == ""
  ${OrIf} $UpdateCreated == 0
    Goto update_invalid
  ${EndIf}
  StrLen $1 $UpdatePid
  System::Call 'shlwapi::StrSpnW(w "$UpdatePid", w "0123456789") i.r0'
  ${If} $0 != $1
  ${OrIf} $1 > 10
    Goto update_invalid
  ${EndIf}
  StrLen $1 $UpdateCreated
  System::Call 'shlwapi::StrSpnW(w "$UpdateCreated", w "0123456789") i.r0'
  ${If} $0 != $1
  ${OrIf} $1 > 20
    Goto update_invalid
  ${EndIf}
  GetFullPathName $INSTDIR $INSTDIR
  ; A solid-compressed installer may initialize after OBS has already exited.
  ; Only ERROR_INVALID_PARAMETER means the PID no longer exists. Access denied
  ; and other failures must not be treated as a closed OBS.
  System::Call 'kernel32::OpenProcess(i 0x101000, i 0, i $UpdatePid) p.r4 ?e'
  Pop $0
  StrCpy $UpdateHandle $4
  ${If} $UpdateHandle == 0
    ${If} $0 == 87
      Goto update_mutex
    ${EndIf}
    Goto update_invalid
  ${EndIf}
  ; A live PID must match both creation time and the selected installation.
  ; Check the creation time first, even if a retained process object has exited.
  System::Call 'kernel32::GetProcessTimes(p r4, *l .r5, *l .r6, *l .r7, *l .r8) i.r0'
  ${If} $0 == 0
    Goto update_close_invalid
  ${EndIf}
  StrCmp $5 $UpdateCreated 0 update_close_invalid
  System::Call 'kernel32::WaitForSingleObject(p r4, i 0) i.r0'
  ${If} $0 == 0
    Goto update_mutex
  ${EndIf}
  ${If} $0 != 258
    Goto update_close_invalid
  ${EndIf}
  System::Call 'kernel32::QueryFullProcessImageNameW(p r4, i 0, w .r5, *i ${NSIS_MAX_STRLEN}) i.r0'
  ${If} $0 == 0
    ; OBS can exit between the wait check and image-name query.
    System::Call 'kernel32::WaitForSingleObject(p r4, i 0) i.r0'
    ${If} $0 == 0
      Goto update_mutex
    ${EndIf}
    Goto update_close_invalid
  ${EndIf}
  StrCmp $5 "$INSTDIR\bin\64bit\obs64.exe" 0 update_close_invalid
  update_mutex:
  ; CheckDirectory still validates OBS, ownership, permissions and all processes
  ; using the destination before writing, including an OBS restarted meanwhile.
  ${StrCase} $8 "$INSTDIR" "U"
  ${StrRep} $8 "$8" "\" "/"
  System::Call 'kernel32::CreateMutexW(p 0, i 0, w "Local\StreamBridgeUpdate-$8") p.r9 ?e'
  Pop $0
  ${If} $0 == 183
    ; Leave the first updater's status intact.
    System::Call 'kernel32::CloseHandle(p $UpdateHandle)'
    SetErrorLevel 32
    Quit
  ${EndIf}
  ${If} $9 == 0
    Goto update_close_invalid
  ${EndIf}
  StrCpy $ErrorCode 0
  ${If} $UpdateHandle == 0
    Goto update_install
  ${EndIf}
  StrCpy $UpdateStatus "waiting"
  Call WriteUpdateResult
  update_wait:
  System::Call 'kernel32::WaitForSingleObject(p $UpdateHandle, i 500) i.r0'
  ${If} $0 == 258
    Goto update_wait
  ${EndIf}
  ${If} $0 != 0
    Goto update_close_invalid
  ${EndIf}
  System::Call 'kernel32::CloseHandle(p $UpdateHandle)'
  update_install:
  ; Keep the mutex until process exit. Never terminate OBS or its helpers.
  StrCpy $UpdateStatus "installing"
  Call WriteUpdateResult
  Return
  update_close_invalid:
  System::Call 'kernel32::CloseHandle(p $UpdateHandle)'
  update_invalid:
  StrCpy $ErrorCode 2
  StrCpy $UpdateStatus "failed"
  Call WriteUpdateResult
  SetErrorLevel 2
  Quit
  update_regular:
FunctionEnd
