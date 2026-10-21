# Installer filesystem safety

## Ownership inventory

The package owns `obs-plugins/64bit/obs-streambridge.dll` and **listed files**
under `data/obs-plugins/obs-streambridge`. It does not own OBS, shared plugin
directories, other plugins, or arbitrary files added to its resource directory.

The inspected output contained 262 static files: the plugin DLL; seven root
resource files (including package.ini and the restart helper); 17 browser
runtime/notices files; 220 CEF locales; 12 documentation files; two plugin
locales; and three Qt plugin/license files. Counts change with the build; this
is an inventory of that output, not a handwritten allowlist. The installer
additionally generates `install.ini` and `uninstall.exe`.

`tools/installer-payload.ps1` generates extraction instructions and
`uninstall-manifest.txt` from the **same staging inventory**, adding itself and
the two generated metadata files. `verify-package.ps1` checks exact set equality
against the binary ZIP. The uninstaller embeds its version's manifest; a
modified installed copy cannot authorize additional deletion. Helpers extracted
only into NSIS's private temporary directory are not installed payload files.

Dynamic configuration consists of `settings.json`, `vault.dpapi`, `ui.ini`,
`updates.json`, `pending-update.json`, temporary atomic-save files and
`updates/update-*` directories containing downloaded installers, update results
and `restart-*.exe`. A static filename list cannot implement a complete purge.
CEF profiles are separate `QTemporaryDir` instances created by BrowserHost;
exported user files and these profiles are outside uninstall ownership.

## Operations and race boundary

`installer/filesystem.cpp` is a native helper with no Qt/CEF dependency. NSIS
extracts it and the trusted manifest privately. Payload extraction also happens
privately; the helper copies bytes to destination **file handles**. Upgrade no
longer recursively removes the browser directory. Old/unknown names absent from
the current inventory are deliberately retained.

The helper opens the volume root once. Each subsequent filesystem component is
a single name opened relative to an already opened directory using documented
`NtCreateFile(RootDirectory, FILE_OPEN_REPARSE_POINT, OBJ_DONT_REPARSE)`.
Attributes and types are checked on that returned handle. Reparse objects are
rejected, never traversed. Unicode names and spaces are preserved. Unsupported
UNC/device/alternate-stream names, malformed components and inspection errors
fail closed. No backup/restore privilege bypass is enabled.

Win32 CreateFile lacks directory-relative opens. Its OPEN_REPARSE_POINT flag
only protects the final component; another pathname check cannot protect
ancestors during lookup. The narrow native API use supplies the missing
primitive; it is documented, not a private syscall. Enumeration and deletion
use Win32 GetFileInformationByHandleEx and
SetFileInformationByHandle(FileDispositionInfo).

Before uninstall mutates anything, the helper revalidates product/root markers
through the same resource directory handle used to prepare deletion. It opens
all manifest files, expected directories and, if requested, the entire settings
plan. Deletion consumes these handles without resolving their paths again.
Directories are removed only when empty. Unknown resources and their containing
directories remain; shared parent directories are never removed.

After an attacker renames a checked component and puts a junction at its former
name, an existing handle still denotes the original object. A subsequent
single-component open either addresses that original directory or rejects a
reparse leaf. Destructive operations never use the old absolute pathname. This
removes the check/use dependency instead of narrowing its timing window.

Windows may refuse directory disposition after an ancestor rename (error 5).
The helper reports failure and never falls back to pathname deletion. Partial
deletion of owned files on failure is possible: this is containment, not
transactional rollback. Concurrent new unknown files can keep directories
nonempty. Installation truncates/copies by handle, rejects existing hard-linked
destinations, and applies the existing browser LPAC read/execute grant to opened
objects. The temporary write probe and path-based icacls call are removed.
Directory ACL handles use MAXIMUM_ALLOWED, for which SetSecurityInfo explicitly
disables child ACE propagation. Individual owned files receive explicit grants;
an injected unknown child must not receive inherited changes. Race tests compare
outside directory/file SDDL as well as file bytes.
The NSIS preflight remains for early diagnostics, not as the security boundary.

## Settings and remaining recursion

Default uninstall does not open settings for deletion. `/PURGESETTINGS` allows
only `config/obs-studio/plugin_config/obs-streambridge` under portable OBS or
`obs-studio/plugin_config/obs-streambridge` under the current user's
FOLDERID_RoamingAppData. Production accepts no arbitrary settings root. AppData
is a separate legitimate area. Missing/invalid ownership markers forbid removal.

Dynamic settings enumeration is recursive **through directory handles**, with a
depth limit and no-follow single-component opens. Deletion uses the prepared
handles in postorder. There is no recursive path-based deletion or RMDir /r in
installer, upgrade or purge operations. NSIS still cleans its private extraction
directory. Build/test workspace cleanup is separate from installation/config
removal.

## Regression tests

`test-installer-races.ps1` builds on the identical helper source with test-only
named-event barriers and a synthetic AppData override. These features are absent
from the shipped helper. The controller waits for complete validation, actually
moves resources, nested resources, the DLL parent, portable settings or AppData
settings, creates a junction to an outside synthetic directory, then signals
deletion to resume. Every outside canary must retain its SHA-256. No random sleep
schedules the swap. Each process tree has a deadline; the barrier has its own
10-second deadline. Normal completion or a bounded Windows error-5 refusal is
acceptable; any outside change fails the test. Logs record actual exit codes.
Three further barriers cover upgrade resource/DLL replacement and a junction
inserted into the browser directory after validation, including ACL containment.

`test-installer-paths.ps1` covers preexisting/dangling junctions, symlinks,
invalid/inaccessible components, unknown resources and manifest tampering. Real
AppData and registration tests run only on isolated GitHub-hosted runners.
Local lifecycle uses -SkipNormal. Full lifecycle, updater and pinned published
0.2.5 migration remain mandatory, with no continue-on-error.

References: [NtCreateFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntcreatefile),
[CreateFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew),
[directory enumeration](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_id_both_dir_info),
[handle disposition](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle).
[ACL propagation contract](https://learn.microsoft.com/en-us/windows/win32/api/aclapi/nf-aclapi-setsecurityinfo).
