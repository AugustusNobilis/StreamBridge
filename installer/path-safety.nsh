; SPDX-License-Identifier: GPL-2.0-or-later
; Fail closed on redirected components/trees BEFORE any destination write.
; This is a preflight, not an atomic filesystem transaction: see
; docs/INSTALLER-SAFETY.md for the concurrent path replacement limitation.
Var GuardPath
Var GuardKind
Var GuardAttributes

!macro PathSafetyFunctions PREFIX
Function ${PREFIX}CheckPath
  Push $R0
  Push $R1
  Push $R2
  Push $R3
  Push $R4
  StrCpy $ErrorPath $GuardPath
  StrCpy $GuardAttributes -1
  ; Only ordinary absolute drive paths are supported. Reject device/UNC/ADS
  ; spellings rather than guessing the semantics of a redirected namespace.
  StrCpy $R0 $GuardPath 2 1
  StrCmp $R0 ":\" 0 path_invalid
  StrCpy $R0 $GuardPath 1
  System::Call 'shlwapi::StrSpnW(w R0, w "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") i.R1'
  StrCmp $R1 1 0 path_invalid
  StrCpy $R0 $GuardPath 3
  System::Call 'kernel32::GetFileAttributesW(w R0) i.R3 ?e'
  Pop $R0
  ${If} $R3 == -1
    StrCpy $ErrorCode $R0
    Goto path_done
  ${EndIf}
  IntOp $R0 $R3 & 0x400
  StrCmp $R0 0 0 path_invalid
  StrLen $R4 $GuardPath
  StrCpy $R1 3
  path_component:
  StrCpy $R0 $GuardPath 1 $R1
  StrCmp $R0 ":" path_invalid
  StrCmp $R0 "*" path_invalid
  StrCmp $R0 "?" path_invalid
  StrCmp $R0 "\" path_inspect
  IntCmp $R1 $R4 path_inspect path_next path_invalid
  path_inspect:
  StrCpy $R2 $GuardPath $R1
  System::Call 'kernel32::GetFileAttributesW(w R2) i.R3 ?e'
  Pop $R0
  ${If} $R3 == -1
    ; Missing leaves are valid for a fresh install, but all existing ancestors
    ; have already been checked. Access denied and malformed paths are errors.
    ${If} $R0 != 2
    ${AndIf} $R0 != 3
      StrCpy $ErrorCode $R0
      Goto path_done
    ${EndIf}
  ${Else}
    IntOp $R0 $R3 & 0x400 ; FILE_ATTRIBUTE_REPARSE_POINT (all tags)
    StrCmp $R0 0 0 path_invalid
    ${If} $R1 < $R4
    ${OrIf} $GuardKind != "file"
      IntOp $R0 $R3 & 0x10
      StrCmp $R0 0 path_invalid
    ${Else}
      IntOp $R0 $R3 & 0x10
      StrCmp $R0 0 0 path_invalid
    ${EndIf}
  ${EndIf}
  ${If} $R1 == $R4
    StrCpy $GuardAttributes $R3
    Goto path_done
  ${EndIf}
  path_next:
  IntOp $R1 $R1 + 1
  Goto path_component
  path_invalid:
  StrCpy $ErrorCode 87
  path_done:
  Pop $R4
  Pop $R3
  Pop $R2
  Pop $R1
  Pop $R0
FunctionEnd

Function ${PREFIX}CheckTree
  Push $R0
  Push $R1
  Push $R2
  Push $R3
  Push $R4
  Push $GuardPath
  StrCpy $GuardKind "directory"
  Call ${PREFIX}CheckPath
  ${If} $ErrorCode != 0
  ${OrIf} $GuardAttributes == -1
    Goto tree_done
  ${EndIf}
  StrCpy $R4 $GuardPath
  System::Alloc 592 ; WIN32_FIND_DATAW
  Pop $R1
  StrCmp $R1 0 tree_memory_error
  System::Call 'kernel32::FindFirstFileW(w "$R4\*", p R1) p.R0 ?e'
  Pop $R2
  ${If} $R0 == -1
    ${If} $R2 != 2
      StrCpy $ErrorCode $R2
    ${EndIf}
    Goto tree_free
  ${EndIf}
  tree_entry:
  IntOp $R2 $R1 + 44 ; cFileName
  System::Call '*$R2(&w260 .R3)'
  StrCmp $R3 "." tree_next
  StrCmp $R3 ".." tree_next
  StrCpy $GuardPath "$R4\$R3"
  StrLen $R2 $GuardPath
  ; Leave room for a separator and an entire child name; never scan a
  ; silently truncated path. Win32 long-path errors also fail closed.
  IntCmp $R2 700 tree_invalid 0 tree_invalid
  System::Call '*$R1(i .R2)'
  IntOp $R3 $R2 & 0x10
  ${If} $R3 != 0
    Call ${PREFIX}CheckTree
  ${Else}
    StrCpy $GuardKind "file"
    Call ${PREFIX}CheckPath
  ${EndIf}
  StrCmp $ErrorCode 0 0 tree_close
  tree_next:
  System::Call 'kernel32::FindNextFileW(p R0, p R1) i.R2 ?e'
  Pop $R3
  StrCmp $R2 0 0 tree_entry
  ${If} $R3 != 18 ; ERROR_NO_MORE_FILES is the only successful end
    StrCpy $ErrorCode $R3
  ${EndIf}
  Goto tree_close
  tree_invalid:
  StrCpy $ErrorPath $GuardPath
  StrCpy $ErrorCode 87
  tree_close:
  System::Call 'kernel32::FindClose(p R0)'
  tree_free:
  System::Free $R1
  Goto tree_done
  tree_memory_error:
  StrCpy $ErrorCode 8
  tree_done:
  Pop $GuardPath
  Pop $R4
  Pop $R3
  Pop $R2
  Pop $R1
  Pop $R0
FunctionEnd
!macroend

!macro GuardDestination PATH KIND PREFIX FAIL
  StrCpy $GuardPath "${PATH}"
  StrCpy $GuardKind "${KIND}"
  StrCpy $ErrorCode 0
  !insertmacro Trace "${PREFIX}" "START path safety kind=${KIND} path=$GuardPath"
  !if "${KIND}" == "tree"
    Call ${PREFIX}CheckTree
  !else
    Call ${PREFIX}CheckPath
  !endif
  !insertmacro Trace "${PREFIX}" "END path safety code=$ErrorCode path=$ErrorPath"
  ${If} $ErrorCode != 0
    StrCpy $ErrorText "Unsafe or inaccessible installation path (Windows error $ErrorCode). Redirected paths, including junctions and symbolic links, are not supported. No destination files were changed.$\r$\n$ErrorPath"
    Goto ${FAIL}
  ${EndIf}
!macroend

!macro GuardPluginPaths PREFIX FAIL
  !insertmacro GuardDestination "$INSTDIR" "directory" "${PREFIX}" ${FAIL}
  !insertmacro GuardDestination "$INSTDIR\portable_mode.txt" "file" "${PREFIX}" ${FAIL}
  !insertmacro GuardDestination "$INSTDIR\bin\64bit\obs64.exe" "file" "${PREFIX}" ${FAIL}
  !insertmacro GuardDestination "$INSTDIR\obs-plugins\64bit\obs-streambridge.dll" "file" "${PREFIX}" ${FAIL}
  !insertmacro GuardDestination "$INSTDIR\data\obs-plugins\obs-streambridge" "tree" "${PREFIX}" ${FAIL}
!macroend

!insertmacro PathSafetyFunctions ""
!insertmacro PathSafetyFunctions "un."
