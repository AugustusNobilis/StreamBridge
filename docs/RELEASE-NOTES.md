# StreamBridge 0.1.2 development preview

This preview adds updates from GitHub Releases: optional daily checks/background download, SHA-256 verification and installation after OBS closes, with configuration preserved. In Settings → Updates, enable preview releases to receive development builds. The installer permissions/Unicode fixes from 0.1.1 are included.

**Для обновления 0.1.0/0.1.1 скачайте `updater.exe` из файлов релиза.** Запустите, проверьте папку OBS и нажмите «Обновить до 0.1.2». Закройте OBS, когда обновлятор попросит; после сообщения об успехе запустите OBS снова. Настройки и ключи сохраняются. Git и дополнительные DLL не нужны, интернет нужен. Следующие версии можно устанавливать из настроек StreamBridge.

The standalone updater uses Windows WinHTTP and a static runtime. The plugin includes Qt's Schannel backend so HTTPS works with ordinary OBS installations without the developer SDK. The two Qt source/recipe archives are for source compliance; end users need only `updater.exe` or the full setup EXE.

Native OBS 32+ multistream and isolated proxy browser implementation. Two direct RTMPS outputs share owned encoders or use independent profiles. The browser renders transparent video and audio through HTTP CONNECT, HTTPS CONNECT or authenticated SOCKS5 TCP. Credentials use Windows DPAPI.

Local real RTMPS, HTTPS/WSS and installer tests are recorded in TEST-RESULTS.md. OAuth protocol tests use mock providers. The binaries are unsigned. Use checksums, review the source and retain CEF/Chromium notices.

## Before publishing a stable release

- Run clean checkout CI on the real repository and attach matching source, EXE, ZIP and SHA-256 checksums.
- Validate Google/Twitch consent and refresh with registered clients; no private secret may enter the binary.
- Exercise a real DonationAlerts test alert, transparent video and audio, plus different public egress IPs for normal and proxied OBS sources.
- Capture process-attributed DNS/DoH/IPv6/QUIC/UDP traffic during success, proxy refusal and recovery.
- Complete manual UI acceptance. Official frontend Start/Stop coexistence and active OBS exit passed the local test module; see TEST-RESULTS.md for its exact transport boundaries.
- Validate Windows 11 and available NVENC/AMF/QSV hardware, prolonged A/V sync and load.
- Review distribution/source obligations for exact dependencies. Private vulnerability reporting is enabled.

This preview does not claim those pending gates passed.
