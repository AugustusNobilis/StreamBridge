# Windows x64 build and packaging

Prerequisites: Windows 10/11 x64, Visual Studio 2022 Desktop C++ workload and Windows SDK, PowerShell 7, CMake 3.28+ (tested 3.30.5), clang-format 19 (tested 19.1.1), Git and enough space for OBS/CEF SDKs and runtime copies. OBS 32+ x64 is required to run the plugin. No Qt/OBS DLLs are copied into the plugin package.

From the repository root:

```powershell
./tools/bootstrap.ps1
./tools/build.ps1
./tools/format.ps1 -Check
$env:PATH = (Resolve-Path '.deps/obs-deps-qt6-2025-08-23-x64/bin').Path + ';' + $env:PATH
$env:QT_PLUGIN_PATH = (Resolve-Path '.deps/obs-deps-qt6-2025-08-23-x64/plugins').Path
ctest --test-dir build -C Release --output-on-failure
./tools/prepare-proxy-fixture.ps1
./build/Release/proxy-tests.exe .deps/proxy-fixture
./tools/package.ps1
```

`dependencies.lock.json` pins archives and SHA-256 hashes. Downloads/extraction fail on mismatch. `bootstrap.ps1 -VerifyOnly` checks the cached archives. Official OBS CMake also obtains its own buildspec-pinned support dependencies, including x86 build tools; this does not change the plugin's x64 target. Bootstrap builds the real OBS libobs/frontend SDK into `.deps/sdk`. CEF is compiled separately to avoid loading its ABI into OBS. The Windows CEF distribution requires its matching bootstrap EXE and DLL; replacing the bootstrap with an ordinary executable or disabling sandbox is unsupported.

`tools/cmake.ps1` finds Visual Studio's CMake if needed and normalizes the Windows PATH environment for MSBuild. `build.ps1` defaults to `AugustusNobilis/StreamBridge`; use `-Repository owner/repository` for a fork. The GitHub workflow supplies `github.repository`. Users can select a trusted public repository in the Updates tab.

## Runtime tests

Build optional probes using `./tools/build.ps1 -IntegrationTests -WidgetFixture`. Add the tested OBS `bin/64bit` folder to PATH alongside Qt. Tests requiring libobs receive the OBS root, not its executable directory:

```powershell
./build/Release/obs-integration.exe 'D:/Programs/obs-studio'
./build/Release/browser-probe.exe (Resolve-Path build-helper/Release/streambridge-browser.exe).Path
./build/Release/browser-media-tests.exe (Resolve-Path build-helper/Release/streambridge-browser-fixture.exe).Path 'D:/Programs/obs-studio'
```

The fixture helper is a separate test-only target and is excluded from all packages. It supplies an in-memory widget and does not prove origin TLS. `tls-widget-tests` instead uses the production helper and real localhost HTTPS/WSS. Its origin certificate must be temporarily trusted by Windows for positive CEF verification; see its argument validation/source and recorded results. Do not disable TLS verification to run it. `prepare-proxy-fixture.ps1` adds a CA only to a test socket, never Windows.

`tools/test-frontend.ps1` requires a closed dedicated workspace portable OBS. It temporarily loads a test-only module, uses a synthetic empty scene/profile, and checks standard frontend streaming against loopback RTMP alongside instrumented plugin outputs, then active OBS exit. It restores the previous user.ini, stops its server and removes the test module. No platform credentials are used; never point it at a personal OBS installation.

`test-rtmps.ps1` runs real local MediaMTX RTMPS with synthetic media. Read its parameters first. The explicit `-TrustLoopbackCertificate` test option may cause Windows dialogs; the script removes and verifies the certificate afterward. Never install these short-lived test certificates for production.

`start-test-obs.ps1` requires a portable marker and sets OBS's working directory to `bin/64bit`; starting elsewhere causes the missing-locale startup error. Use a dedicated test profile with no account credentials.

## Packages and installation

The standalone `build/Release/updater.exe` is statically linked against the MSVC runtime and uses native WinHTTP/CNG/Windows JSON. It targets the fixed `v0.1.2` release. The ordinary GUI detects OBS and offers a folder picker. For diagnostics, `updater.exe --download-only <absolute-cache-folder>` downloads and validates without installation and writes `updater-result.txt`. Explicit automation mode `--install-to <absolute-OBS-root>` downloads the same fixed release and installs it; use only an isolated portable fixture for testing. No arbitrary URL or local executable override exists.

Packaging also emits `dist/updater.exe` and the pinned Qt source/recipe archives. The binary ZIP includes `qt-plugins/tls/qschannelbackend.dll` and its GPLv2 license. `tools/verify-package.ps1` verifies the exact pinned TLS DLL, bootstrap executable, and corresponding-source hashes. The bootstrap must be available in the published release before linking users to it; the setup asset's GitHub digest must be populated.

`package.ps1` rebuilds the plugin, then creates `dist/obs-streambridge-0.1.2-windows-x64-setup.exe`, the matching ZIP, `obs-streambridge-0.1.2-source.zip` and `SHA256SUMS.txt`. Its binary allowlist excludes test helpers/modules, symbols, SDKs, OBS DLLs and credentials. The source ZIP includes build scripts and tests, without dependency caches or secrets. CEF's exact BSD license and Chromium notices are included. Verify assets with `Get-FileHash -Algorithm SHA256`.

For a manual ZIP install, close OBS, extract into the selected OBS root, then grant Chromium LPAC read/execute access only to the helper runtime (replace the path):

```powershell
icacls.exe 'D:\OBS\data\obs-plugins\obs-streambridge\browser' /grant '*S-1-15-2-2:(OI)(CI)(RX)'
```

The EXE does this automatically. Do not grant write access or change ACLs on OBS configuration or user-profile directories. Protected installations may require administrator privileges. Portable mode requires `portable_mode.txt` at the root. ZIP removal deletes only `obs-plugins/64bit/obs-streambridge.dll` and `data/obs-plugins/obs-streambridge`; leave `plugin_config/obs-streambridge` to retain credentials.

The installer checks users of OBS/CEF files through Windows Restart Manager without opening OBS itself for write. Error 32 means a file is in use; error 5 means write access is denied. Interactive install/uninstall requests UAC once on denied write access and keeps the selected root. Silent `/S` operations never prompt for elevation: launch them from an already elevated shell for protected folders. They return 5 for access denied, 32 for busy files, 2 for invalid/conflicting roots and 3 for extraction/removal failure. A denied or cancelled UAC request leaves files unchanged. Elevation under a different Windows account uses that account's HKCU; keep settings by default when uninstalling this way.

Run installer tests with `tools/test-installer.ps1 -Installer <EXE> -ObsExecutable <obs64.exe>`; it creates synthetic disposable OBS roots inside `.deps`, without launching them or modifying the named executable. Tests include Cyrillic/space paths, read-only OBS, busy files and a temporary write-deny ACL scoped to one new fixture and restored in `finally`. An optional running workspace portable root tests running-OBS refusal. Use `-SkipNormal` when the current user already has StreamBridge installed; this skips the HKCU normal-install lifecycle without changing the existing registry entry.

## Publishing

`tools/test-updater.ps1 -Installer <EXE>` tests the production launcher and NSIS wait/apply mode using an isolated executable fixture, not a personal OBS installation. It checks the process path and creation time, unchanged files before exit, result persistence and preserved credentials. `update-tests` covers metadata, channel filtering, digest/size verification, redirects, cancellation, interrupted downloads and cached-package tampering. `update-release-probe <cache-directory>` uses the real updater as an older client to download a published preview without installing it.

For each update, increase the CMake version, locale About strings and release notes, commit, then push the matching `v<version>` tag. CI produces EXE, ZIP, source ZIP and checksums and creates a draft preview. Review and publish that draft. Uploaded assets must keep their generated names and have a GitHub `sha256:` digest; a draft or tag alone is not an update. Stable clients ignore previews unless opted in. Existing releases are not overwritten by CI. The package is a full download; delta updates, Authenticode signing and transactional rollback after disk/extraction failure are not implemented.

The workflow checks formatting, fetches dependencies, builds, runs available automated tests and packages artifacts. A matching `v0.1.2` tag prepares a draft prerelease with EXE, ZIP and checksums. No workflow has been accepted until it actually runs on the intended GitHub repository. Complete [release gates](RELEASE-NOTES.md), publish matching source, inspect license notices and verify installer checksums before publishing the draft. Signing is not configured; do not imply a verified publisher.
