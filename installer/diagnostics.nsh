; SPDX-License-Identifier: GPL-2.0-or-later
; Only operation stages, marker values, paths and error codes. Never credentials.
Var LifecycleLog
!macro Trace PREFIX TEXT
  Push "${TEXT}"
  Call ${PREFIX}LifecycleTrace
!macroend
!macro TraceFunctions PREFIX
Function ${PREFIX}InitLifecycleLog
  Push $0
  ReadEnvStr $LifecycleLog STREAMBRIDGE_LIFECYCLE_LOG
  ${If} $LifecycleLog == ""
    System::Call 'kernel32::GetCurrentProcessId() i.r0'
    StrCpy $LifecycleLog "$TEMP\StreamBridge-lifecycle-$0.log"
  ${EndIf}
  Pop $0
FunctionEnd
Function ${PREFIX}LifecycleTrace
  Exch $R9
  Push $R0
  Push $R1
  Push $R2
  Push $R8
  ; Logging must not change the caller's error flag or scratch registers.
  StrCpy $R8 0
  IfErrors 0 +2
    StrCpy $R8 1
  System::Call 'kernel32::GetSystemTimeAsFileTime(*l .R1)'
  System::Call 'kernel32::GetCurrentProcessId() i.R2'
  FileOpen $R0 "$LifecycleLog" a
  IfErrors trace_done
  FileSeek $R0 0 END
  ; A full helper diagnostic can fill an NSIS string; its terminal record still
  ; needs a newline so the next END record remains independently parseable.
  FileWriteUTF16LE $R0 "filetime=$R1 pid=$R2 "
  FileWriteUTF16LE $R0 "$R9"
  FileWriteUTF16LE $R0 "$\r$\n"
  FileClose $R0
  trace_done:
  ClearErrors
  StrCmp $R8 1 0 +2
    SetErrors
  Pop $R8
  Pop $R2
  Pop $R1
  Pop $R0
  Pop $R9
FunctionEnd
!macroend
!insertmacro TraceFunctions ""
!insertmacro TraceFunctions "un."
