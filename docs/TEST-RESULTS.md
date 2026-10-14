# Executed validation

## Restart updater and Desktop OAuth 0.2.2 / 2026-10-14

All nine CTest suites passed. After additional UI and restart-guard assertions, OAuth, updates and UI passed again. New OAuth fixtures cover Desktop JSON parsing, Web-client rejection, import size limits, fixed token endpoint despite a supplied alternate URL, secret-free browser URL, encrypted credential persistence, refresh after process-session reconstruction, rotation, disconnect and safe actionable error mapping. These use simulated provider responses and a real TCP loopback callback, not a real Google account.

The production native launcher, standalone restart helper and uncompressed NSIS payload passed isolated OBS-compatible process tests: no file replacement before exit, successful installation followed by automatic relaunch from bin/64bit, byte-identical credential preservation, cancelled shutdown keeping the original process and plugin intact, and no relaunch on a mismatched installed version. Tests run in Unicode workspace paths. Protected-folder UAC accept/cancel and Windows 11 remain unverified manually.

Production Russian/English widgets were rendered and inspected; the Updates tab has one primary action. A real dedicated portable OBS 32.1.2 loaded all three docks and the browser source, passed frontend stream coexistence, and closed normally with active fixture outputs. This frontend run is separate from the restart-helper process test; it does not prove full automatic relaunch of a real OBS UI. Personal OBS was not modified or closed.

## Platform docks 0.2.0 / 2026-10-14

Nine local CTest suites passed: core, bootstrap, security, OAuth, platforms, proxy, updates, browser settings and UI. Platform fixtures cover selected-broadcast authentication, unavailable/zero/positive viewer counts, YouTube metadata preservation, exact moderation targets and failed operations, duplicate chat IDs, provider polling delay, chat disable during a request and stale responses after disconnect. Twitch metadata and moderation requests use synthetic HTTP replies; the IRC parser is tested independently.

Production Qt widgets were rendered in Russian and English with synthetic chat content, using the Windows Qt driver without showing test windows. Reviewed screenshots cover narrow docks, both eye badge colors, plain-text chat messages, server/account setup, Help, Updates and About. The UI suite also checks the 500-message bound and targeted local removal. Evidence: build/ui-review and build/tests-0.2.0.log. These images are fixture previews, not evidence of real platform messages.

A dedicated portable OBS 32.1.2 loaded all three production docks and the browser source. The existing frontend coexistence test passed start/stop independence and normal OBS shutdown with outputs active. Its primary stream uses a real loopback RTMP endpoint; secondary outputs are instrumented local sinks. Evidence: build/frontend-0.2.0.log. This run did not transmit to YouTube or Twitch.

Real Google/Twitch application consent, live platform viewer/chat reception, successful platform moderation, and go-live notification editing in platform dashboards remain untested. No Client IDs were supplied. Registered Google Desktop / Twitch Public applications and account-owner acceptance are required. No certificates or system trust settings were changed for this milestone. Windows 11, SmartScreen/UAC interaction and hardware encoders retain their separate acceptance requirements.

## Updater 0.1.2 / 2026-10-14

The standalone bootstrap has separate tests for the fixed release, draft/duplicate/wrong-origin/oversized metadata, unsafe redirects, and missing/truncated/modified payloads using a known SHA-256 vector. The release validation artifact records the live download/install results after publication. Visual UI acceptance and protected-folder UAC remain separate manual checks.

Real GitHub HTTPS with the installed OBS Qt runtime and only the packaged Schannel plugin returned HTTP 200 with TLS verification enabled. Without that plugin, the same normal OBS runtime had no TLS backend; the package now includes it. No SDK plugin path or added certificate trust is needed.

Automated updater tests passed metadata/asset validation, explicit preview opt-in, allowlisted redirects, exact size/SHA-256 checks, cancellation, truncated/oversized/corrupt downloads and persisted-download tampering. Tests inject network replies; they do not by themselves establish live GitHub delivery.

The production Windows launcher and NSIS update mode passed an isolated native host test: wrong process path/creation time rejected, plugin unchanged while the host runs, normal host exit followed by in-place replacement, and byte-identical credential preservation. This uses a test executable with an OBS-compatible version resource, not a live stream or manual OBS UI. Evidence: `build/updater-integration.log`. Live GitHub delivery and final compressed-package validation are recorded in the release validation artifact after execution. UAC accept/cancel under a protected installation has not been manually validated.

## Installer 0.1.1 regression / 2026-10-14

The uncompressed NSIS regression build passed installation, upgrade and uninstall in `Файлы программ\OBS Тест`, with `obs64.exe` marked read-only and verified byte-identical afterward. Windows Restart Manager detects open OBS/plugin files and returns installer exit 32. A temporary write-deny ACL on a new workspace fixture returns exit 5 without UAC in silent mode or installing plugin files; the ACL is restored afterward. Credential preservation, explicit purge, unrelated-file preservation, conflict and wrong-architecture rejection also passed. Evidence: `build/installer-0.1.1-test.log`.

The normal-install registry lifecycle was skipped for this patch run because the real user already has StreamBridge installed; that existing registration and running OBS were left untouched. Earlier 0.1.0 normal lifecycle results remain below. Automatic UAC acceptance/cancellation has not been manually verified on the reported second PC; Windows 11 and that PC's specific ACLs remain unverified. Folder localization itself is covered by the Unicode fixture, not by a second Windows language installation.

## Windows / OBS 32.1.2 / 2026-10-14

The native plugin loaded in an isolated portable OBS installation. The startup error was a working-directory error: OBS must start from its `bin/64bit` directory to locate `../../data/obs-studio/locale/en-US.ini`. `tools/start-test-obs.ps1` sets that directory. Accessibility inspection found the StreamBridge dock menu entry. Visual screenshots were unavailable in this environment; that is not a visual QA pass.

`core-tests`, `security-tests` and `obs-integration` passed. The last test feeds real x264/AAC packets to an instrumented libobs output and checks shared ownership, independent profiles, stop/rejoin, retry and destruction. It is separate from the network test below.

`rtmps-integration` passed against three local MediaMTX **v1.21.2** processes with OBS's unmodified `rtmp_output`:

- Two simultaneous RTMPS connections with certificate verification enabled.
- Both servers parsed H.264 (640×360) and AAC (48 kHz stereo); both received media bytes.
- Common mode used the same video encoder pointer.
- Stopping/rejoining one output left its peer receiving media.
- Killing one endpoint triggered bounded backoff; the other continued receiving media. Restarting the server recovered automatically; only the failed output's retry counter changed.
- Advanced mode used separate encoders and delivered media over TLS (700/300 kbps, 640×360/320×180, 30/15 FPS as reported by x264).
- An untrusted TLS certificate was rejected and that endpoint received no media, while the trusted peer stayed live.
- Single-output start left the other idle; shutdown during active RTMPS completed.

Evidence: local `build/rtmps-integration.log` and `.deps/rtmps-fixture/server-*.log` (ignored build artifacts). The tests used only generated black/silent media and synthetic keys on loopback, not platform accounts.

The user explicitly approved temporary CurrentUser Root trust for a four-hour, localhost-only certificate. Windows required manual confirmation for addition and removal. After testing, absence of thumbprint `10EC423EEC472323E603286E3E919FF005761564` was independently verified under the real user's account. The wrapper's blocked .NET removal was replaced with `certutil` and fresh-store verification. No system proxy or routes were changed.

## OAuth and automated regression

All six current CTest suites passed: `core`, `security`, `oauth`, `proxy`, `updates`, `browser-settings`. OAuth exercises mock provider APIs, real loopback callback sockets, PKCE, token punctuation encoding, polling delay, cancellation, refresh rotation and encrypted persistence. Browser configuration tests verify DPAPI persistence and absence of URL/password plaintext. Update tests reject malicious release URLs, draft/prerelease metadata and oversized responses; opt-in defaults off and persists. A real-libobs integration regression also passed; see `build/integration.log`.

## Browser and proxy runtime

Pinned CEF 154.0.34+g14c5a08 / Chromium 154.0.8037.98, production Windows sandbox bootstrap.

- `proxy-tests`: real local sockets for HTTP CONNECT, authenticated SOCKS5 with remote unresolved destination hostname, bad credentials, unauthenticated loopback rejection and plaintext-request rejection. HTTPS proxy verifies a private test CA scoped only to that socket; missing trust rejects it. No Windows trust changes.
- `browser-probe`: two production helpers load a real public HTTPS page through separate local proxies; an explicitly direct helper loads independently. Proxy A failure produces a main-page error without successful direct reload; B stays working. All process trees stop.
- `browser-media-tests`: a separate test-only helper serves a synthetic widget to exercise JS animation, transparent/partial alpha, actual nonzero 48 kHz stereo audio, dimensions and remote hostname. A loopback STUN UDP canary receives zero packets. Real D3D11/libobs source receives CEF frames/audio; active worker shutdown passes. This fixture is excluded from packaging and does not prove origin TLS.
- `tls-widget-tests`: production helper, actual localhost HTTPS origin and WSS echo, three simultaneous authenticated HTTP/HTTPS/SOCKS5 proxies. Passed 302 navigation, images, worker requests, JS animation, WSS-induced pixel change, nonzero audio and proxy authentication. Proxy A refusal blocks the still-reachable origin, the SOCKS peer continues, recovery works, and active WSS/audio process trees stop. STUN canary receives no packets.

The browser tests exposed and fixed three actual defects: iterator invalidation while deleting adapter child tunnels, aborting queued TLS response bytes at normal upstream EOF, and a Chromium WebRTC flag ineffective in the Chrome runtime. The effective command-line policy plus required context preference is now applied before navigation. A resolver rule was also corrected to exclude the loopback adapter address, and the Chrome login prompt disabled so proxy authentication reaches the CEF callback.

Evidence: `build/proxy-tests.log`, `build/browser-probe.log`, `build/browser-media-tests.log`, `build/tls-widget-tests.log`. After the latest adapter/host changes, the unit, proxy TLS, libobs lifecycle, synthetic media and production-helper routing regressions all passed again. The full TLS widget test passed before that final regression; it was not unnecessarily repeated after deleting its test certificate.

The user approved temporary trust for the real origin TLS test. The localhost-only four-hour certificate `983AF357175FB553E105EC19D676787BCFEB85FB` was removed afterward; a fresh CurrentUser Root store query under the real user returned **0 matches**. No system proxy or route changed.

## Real OBS frontend coexistence

`tools/test-frontend.ps1` passed inside the dedicated portable OBS 32.1.2 with the current production plugin loaded. The production Proxied Browser Source registered, created and destroyed successfully. A separate opt-in test module exercised the same official frontend Start/Stop Streaming API used by OBS controls:

- Standard OBS streaming published real H.264/AAC to loopback MediaMTX over RTMP.
- Two StreamBridge outputs simultaneously encoded real packets into instrumented libobs sinks, with encoder ownership separate from the standard stream.
- Stopping plugin Twitch left plugin YouTube and standard streaming sending packets.
- Standard Stop/Start Streaming left plugin YouTube live; the standard stream restarted successfully.
- Closing OBS while all three outputs were active delivered frontend EXIT, shut down the plugin outputs and unloaded cleanly; process exit was 0 and OBS reported `Number of memory leaks: 0`.

This complements, rather than replaces, the earlier two-output RTMPS test. This frontend test uses loopback RTMP for the primary output and instrumented plugin sinks, not three network RTMPS outputs or a manually clicked UI button. The test-only module requires an explicit fixture marker and is excluded from packages. It initially held an observation reference too long and delayed shutdown; releasing test references before closing OBS fixed the harness. Production unload now avoids calling frontend callback removal after frontend EXIT. Evidence: `build/frontend-tests.log`, `.deps/frontend-fixture/server.log` and portable OBS log `2026-10-14 11-27-12.txt`.

## Installer and build

Release plugin and both production/test helper targets built with MSVC 2022. `bootstrap.ps1` verified all five production archive SHA-256 hashes and completed OBS SDK configure/build/install against existing cached inputs. This is a rerunnable local bootstrap check, not a clean-cloud CI result.

NSIS lifecycle tests passed on synthetic OBS roots inside the workspace: portable install, upgrade, byte-for-byte credential preservation, default uninstall, reinstall, explicit purge, unrelated-file preservation, non-OBS rejection, unowned conflict preservation, x86/ARM64 rejection, locked-file refusal and refusal while the real dedicated portable OBS is running. Normal install/upgrade/uninstall also passed, including creation/removal of its HKCU uninstall entry. Original OBS executables were copied, never patched in the user's installation; architecture negatives modified only disposable fixture copies.

Evidence: `build/installer-tests.log`. The lifecycle run used an uncompressed test build of the same NSIS script and staged runtime; final compressed package validation is recorded separately when executed. Old-version rejection uses the file version check; an OBS 31 binary negative fixture was not executed.

## Not yet accepted

Real Google/Twitch consent/refresh with registered clients; real DonationAlerts alert with audio; different public egress IPs alongside normal OBS Browser Source; exhaustive process-attributed DNS/DoH/IPv6/QUIC/UDP capture; complete visual UI review/manual-button acceptance, hardware encoder matrix, long-duration A/V sync, Windows 11, clean GitHub CI and public release. Client IDs are unavailable and the user requested continuing implementation without them. No pending item is accepted merely because it compiles or a mock passes.
