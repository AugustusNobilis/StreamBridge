# Installer filesystem and lifecycle checks

The installer owns `obs-plugins/64bit/obs-streambridge.dll` and
`data/obs-plugins/obs-streambridge` beneath the selected OBS root. It does not
own the OBS root, shared plugin directories, or other plugins. Settings are
preserved unless `/PURGESETTINGS` is explicit. Purge owns only
`config/obs-studio/plugin_config/obs-streambridge` for portable OBS, or
`$APPDATA/obs-studio/plugin_config/obs-streambridge` for the current user.
AppData is intentionally a separate allowed area, not a child of OBS.

`path-safety.nsh` rejects every reparse tag in all existing path components
starting at the drive root and throughout each recursively modified tree.
It checks file/directory types and enumeration errors, permits absent leaves,
and rejects unsupported UNC/device paths, alternate streams and inaccessible
components. Unicode Win32 APIs are used. All planned purge paths are checked
before any plugin deletion; installation checks run before writable probes,
extraction, browser cleanup, ACL assignment, or marker writes. Ownership
markers remain mandatory. Error 87 indicates an unsafe/unsupported path;
other inspection failures retain the Windows error code.

**Limit: this is a preflight, not an atomic containment boundary.** NSIS uses
path-based extraction, deletion and `icacls`. Another process with write access
can replace a checked component after the scan, or introduce a new child
before recursive deletion. Rechecking immediately before the section reduces
the window but does not eliminate it. Do not run installation concurrently
with directory relocation or mutation. Full protection against adversarial
concurrent replacement needs handle-relative filesystem operations for the
whole mutation, including extraction and recursion, or a transactional native
installer backend. No ACL changes or hidden elevation are used to simulate
that guarantee. Static reparse regressions are fixed; the broader concurrent
replacement aspect of review R1 remains open.
