# Changelog

## 0.2.5 — 2026-10-15

- Show stream uptime as HH:MM:SS, including hours beyond 24.
- Render chat backgrounds independently of OBS styles, with bold platform-colored names, role badges and clickable user cards.
- Support Twitch emotes, BetterTTV global/channel emotes and animated GIFs, with bounded anonymous media loading and text fallback.
- Fetch Twitch following dates on demand; request the additional follower scope on reconnect.
- Automatically connect linked chats, refresh on stream start and show compact Online/Offline/Error statuses with an icon reconnect action.
- Move chat controls to Settings and the log to an About dialog. Add Astra at twice its previous amplitude, custom PCM WAV, volume and preview.

## 0.2.4 — 2026-10-14

- Remove the stream information dock; retain YouTube lifecycle controls in the main dock.
- Persist server/key together, preserve manual YouTube routes and accept the official backup query.
- Restore broadcast selection and metadata across UI reopen, restart and token refresh.
- Recover missing broadcast/chat IDs and transient chat failures; refresh viewers for the recovered event.
- Add translucent platform chat rows and optional quiet, throttled notifications with silent history.


## 0.2.3 — YouTube broadcast controls and viewer counts, 2026-10-14

- Queue viewer refreshes behind chat traffic, support numeric and string counts, and distinguish measured zero from YouTube's omitted zero-or-hidden field with a 0* badge and explanation.
- Add explicit Go live and End broadcast controls, active-ingest checks, monitor/testing transitions, bounded status polling and confirmation before completion. Stop only YouTube output after confirmed completion.
- Remove description and notification editors while preserving existing YouTube descriptions during title/category updates.
- Localize YouTube category names and link Gaming to the selected broadcast in Studio; the public API exposes no writable game-title field.
- Update Russian and English setup guides and target the standalone updater at 0.2.3.

## 0.2.2 — restart updates and Google Desktop credentials, 2026-10-14

- Replace multiple update buttons with one state-dependent action and collapsible preferences.
- Add Install and restart OBS: normal frontend close, cancellation on veto, and a native helper that restarts OBS with the original user token only after verified installation success. Active streams, recording, replay buffer and virtual camera block installation.
- Import each user's Google Desktop client JSON locally; include its client_secret in code exchange and refresh, persist it with DPAPI, and explain token failures without displaying provider payloads.
- Update both offline YouTube guides. No shared Google project or distributor Client ID is bundled.
- Target the standalone updater at 0.2.2.

## 0.2.1 — account setup guides and update race fix, 2026-10-14

- Add offline Russian/English step-by-step guides beside the YouTube and Twitch account buttons, using the same documents as GitHub.
- Explain Google's tester-related 403, Desktop client setup and broadcast selection; specify Twitch registration fields and Public device authorization.
- Handle OBS exiting before the compressed update installer initializes. A missing process no longer aborts installation; live process identity, destination, ownership, permissions and in-use-file checks remain enforced.
- Add immediate-exit and restarted-host updater regression coverage.

## 0.2.0 — platform docks and interface, 2026-10-14

- Add viewer eye badges with unknown/zero/positive states.
- Add combined native chat, platform icons and permission-aware message deletion/permanent bans.
- Add title/category editing, YouTube descriptions and local notification drafts.
- Expand OAuth permissions, persist the selected YouTube broadcast and preserve unrelated video metadata during updates.
- Simplify platform setup; merge help/security/proxy guidance; display installed/latest versions and add a plugin language selector.
- Add author attribution, retain GPL, and rewrite the README in Russian and English.
- Update the standalone bootstrapper target to 0.2.0 without changing published older release files.
- Add protocol, API and native widget regression tests. Real-account acceptance remains pending.

## 0.1.2 — GitHub in-place updates, 2026-10-14

- Provide a standalone `updater.exe` for 0.1.0/0.1.1 → 0.1.2, using native Windows HTTPS and SHA-256 without external runtime DLLs.
- Include the Qt Schannel backend and matching source/recipe assets; HTTPS updates work without the developer SDK.

- Bind updates to AugustusNobilis/StreamBridge, with editable repository and optional preview channel.
- Add opt-in daily checks/background downloads, progress, cancellation, retry and recovery of verified downloads after OBS restarts.
- Verify exact GitHub asset name, size and SHA-256; reject foreign redirects and damaged packages.
- Schedule a silent update in the existing OBS directory after the identified process exits; preserve configuration and request Windows elevation for protected folders.
- Add automated network-boundary tests and native launcher/installer lifecycle tests to CI.

## 0.1.1 — Windows installer fix, 2026-10-14

- Check running OBS/CEF through Windows Restart Manager without requesting write access to `obs64.exe`.
- Distinguish files in use from denied folder permissions; include the affected path and Windows error code.
- Request UAC only when an interactive install/uninstall encounters denied write access; preserve the selected OBS root. Silent operations return error 5 without opening UAC.
- Cover Cyrillic paths with spaces, read-only OBS executables, busy files and denied folder permissions in installer regression tests.

## 0.1.0 — development preview, 2026-10-14

- Native OBS 32+ plugin, Russian/English dock and settings, direct independent RTMPS outputs.
- Owned shared H.264/AAC encoding and independent advanced profiles with supported FPS divisors.
- Bounded reconnect, stream metrics, encrypted manual credentials and safe diagnostic export.
- Google Desktop PKCE and Twitch Public Device Code account integration; registered-client acceptance pending.
- Separate sandboxed CEF source with transparent BGRA, stereo audio, authenticated proxy adapter, HTTPS/WSS and explicit IP test.
- Proxy shutdown, TLS EOF and Chromium WebRTC routing corrections verified in runtime tests.
- NSIS installer, portable ZIP packaging, pinned dependency hashes, opt-in GitHub update checks and draft-release workflow.
- Local test evidence and outstanding release gates documented. This version has not been published as a stable GitHub release.
