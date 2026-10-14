# OBS StreamBridge

Native Windows x64 plugin for OBS Studio 32+: two direct RTMPS outputs and a separate proxy browser source. C++20, Qt6, libobs and a sandboxed CEF helper. Russian and English UI. GPL-2.0-or-later source.

**0.1.2 is a development preview.** Real local RTMPS, CEF HTTPS/WSS/video/audio and installer tests have run. Platform account consent, DonationAlerts acceptance and full network packet audit remain pending. See [executed results](docs/TEST-RESULTS.md) and release validation; compilation is not acceptance.

## Возможности

- Две прямые RTMPS-трансляции: общий H.264/AAC encoder или независимые профили. Отдельные запуск, остановка, состояние и переподключение каждой платформы.
- Реальные bitrate, dropped frames, время работы и число переподключений. Ограниченный exponential backoff с jitter; максимум 12 повторов.
- Ручные ключи, Google Desktop OAuth с PKCE и Twitch Public Device Code Flow. Client ID регистрирует владелец приложения; секрет сервера в бинарник не встраивается.
- Proxied Browser Source: прозрачное видео, JavaScript, WSS, звук, HTTP CONNECT / HTTPS CONNECT / SOCKS5 TCP с авторизацией, отдельный helper на источник.
- Секреты и URL виджетов шифруются DPAPI. В сцену OBS они не записываются. Системный прокси и маршруты не меняются.
- EXE-установщик, portable ZIP и проверка GitHub Releases по согласию пользователя.

## Install and start

**Уже установлена 0.1.0 или 0.1.1?** Скачайте [updater.exe](https://github.com/AugustusNobilis/StreamBridge/releases/download/v0.1.2/updater.exe), запустите и нажмите **Обновить до 0.1.2**. Он найдёт OBS или предложит выбрать его папку, загрузит проверенный пакет и сохранит настройки. Закройте OBS обычным способом, если обновлятор попросит; дождитесь сообщения об успехе и снова запустите OBS. Для защищённых папок Windows запросит права администратора. Git и дополнительные DLL не нужны. Обновлятор предназначен для Windows x64 и требует интернет.

1. Close OBS and StreamBridge browser helpers. Use the EXE installer and select the OBS root containing `bin/64bit/obs64.exe`. It checks OBS 32+, x64, files in use and package ownership. For protected folders it requests Windows administrator permission when needed and reopens with the selected path. Writable portable installations do not require elevation. Windows localized folder names and Cyrillic paths are supported.
2. Start OBS. Open **Docks → StreamBridge** or **Tools → StreamBridge Settings**. Set RTMPS server and encrypted stream key for each destination, then Save. Saving/deleting a key takes effect immediately.
3. Choose Common or Advanced while both StreamBridge outputs are stopped. Start each destination or Start All. OBS's ordinary streaming settings are separate.
4. For a widget, add **Proxied Browser Source**, open its configuration and enter the HTTPS widget URL and proxy. Read [Proxy Browser](docs/PROXY-BROWSER.md) for controls and limits.

The ZIP contains the same `obs-plugins` and `data` layout. Extract it into a closed OBS installation. Chromium's LPAC sandbox needs read/execute access for SID `S-1-15-2-2` on `data/obs-plugins/obs-streambridge/browser`; the EXE applies this narrowly. For manual ZIP installation, see [Windows build/install instructions](docs/BUILD-WINDOWS.md). Neither package includes OBS itself.

The binaries are unsigned. Windows SmartScreen may warn about an unknown publisher. Verify the source and SHA-256 checksum and follow your Windows security policy; no protection bypass is supplied.

Upgrade with OBS closed. Settings are preserved. The uninstaller under `data/obs-plugins/obs-streambridge/uninstall.exe` preserves settings by default; its explicit removal option also deletes encrypted credentials. Portable mode uses `portable_mode.txt` in the OBS root.

## Supported limits

- Built against OBS 32.0.4 / Qt 6.8.3; exercised on OBS 32.1.2, Windows 10 x64. Other OBS/Qt combinations and Windows 11 need runtime validation. ABI compatibility with every future OBS release is not promised.
- SDR H.264/NV12 and AAC. Encoder choices depend on modules and hardware actually available in OBS; NVENC/AMF/QSV require separate hardware acceptance. Common mode shares one audio track. Advanced mode supports OBS tracks 1–6.
- Different FPS uses an integer divisor 1–6 of OBS FPS. Arbitrary combinations such as 60/24 are unavailable. Stop outputs before changing OBS video settings.
- Browser: HTTPS/WSS only, up to 1920×1080 and 60 FPS, CPU frame transfer. Each source costs a separate Chromium process tree. No performance claim for large numbers of sources.
- SOCKS5 supports TCP, remote destination DNS and username/password; no UDP ASSOCIATE. Proxy mode blocks QUIC and non-proxied WebRTC. Explicit proxy-off uses direct access.
- DPAPI credentials remain tied to the current Windows account. Scene export/import does not transfer them. New browser instances must be configured; Add Existing shares an instance.

## Network and updates

User-started outputs connect directly to the configured RTMPS servers. Configured browser sources connect through their own proxies (or directly when explicitly disabled). Connected OAuth accounts contact provider endpoints for authorization, refresh and Twitch validation. Check External IP explicitly contacts `api.ipify.org`; Test Connection loads the entered widget in a temporary browser. No hidden telemetry or remote control endpoint is implemented.

Updates use [AugustusNobilis/StreamBridge Releases](https://github.com/AugustusNobilis/StreamBridge/releases). Git is not required on the user's PC. Open **Settings → Updates**, enable automatic checks and (optionally) automatic downloads, then save. Enable **Include published preview releases** to receive the current development previews. Checks happen at most once daily. The repository can be changed for a trusted fork.

Click **Install after OBS closes** when a downloaded update is ready. The package is checked against GitHub's SHA-256 digest, then the updater waits for that exact OBS process to finish and updates the existing folder silently. It never closes OBS or stops streams itself. Credentials and settings stay in place; Program Files may require UAC when scheduling. Start OBS again after installation finishes. A failed download can be retried; damaged or incomplete packages are never executed. Versions 0.1.0/0.1.1 can make the initial upgrade using the standalone `updater.exe` above.

## Development

See [Windows build](docs/BUILD-WINDOWS.md), [architecture](docs/ARCHITECTURE.md), [OAuth](docs/OAUTH.md), [security](SECURITY.md), [contributing](CONTRIBUTING.md), [changelog](CHANGELOG.md) and [license inventory](THIRD_PARTY_NOTICES.md). The official OBS plugin template and obs-multi-rtmp were studied; the output implementation is original libobs code. Rationale and primary references are in [FEASIBILITY.md](docs/FEASIBILITY.md).

Maintainers publish a new version tag and its GitHub Release to deliver updates. Ordinary commits and unpublished drafts do not reach users. Complete the release checklist before announcing a stable release.
