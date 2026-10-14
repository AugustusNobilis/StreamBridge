# Changelog

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
