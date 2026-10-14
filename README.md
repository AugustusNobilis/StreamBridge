# OBS StreamBridge

**[Скачать / Download](https://github.com/AugustusNobilis/StreamBridge/releases)** · [Автор / Author](https://github.com/AugustusNobilis) · [GPL-2.0-or-later](LICENSE)

## Русский

Бесплатный плагин для OBS Studio: одновременные независимые трансляции на **YouTube и Twitch**, общий чат и браузерные источники с отдельными прокси. Windows x64, OBS 32 с Qt 6.8.

### Что умеет

- Общий энкодер или отдельные настройки качества для каждой площадки; запуск, остановка и переподключение независимо от основной трансляции OBS.
- Счётчик зрителей с иконкой глаза: серый при 0, приглушённый красный при 1 и больше; «—», если данные недоступны.
- Докпанель общего чата со значками площадок. Удаление сообщений и бан через правую кнопку при наличии прав.
- Докпанель названия, категории и описания YouTube. Черновик уведомления можно скопировать в кабинет площадки.
- Браузерные источники с отдельными SOCKS5 / HTTP CONNECT прокси.
- Русский и английский интерфейс, зашифрованное хранение ключей, обновления через GitHub без установки Git.

### Установка и обновление

1. Скачайте **setup.exe** из [релиза](https://github.com/AugustusNobilis/StreamBridge/releases), закройте OBS и выберите его корневую папку, содержащую `bin/64bit/obs64.exe`. Portable OBS поддерживается.
2. Уже установлен StreamBridge? Используйте **updater.exe** из релиза 0.2.0 или **Настройки → Обновления**. Для текущих предварительных сборок включите их получение. Настройки и ключи сохранятся.
3. После установки откройте OBS. В меню **Док-панели** включите StreamBridge, StreamBridge — Чат и StreamBridge — Данные трансляции.
4. На вкладке YouTube/Twitch выберите сервер, сохраните ключ и настройки — либо привяжите аккаунт. Для чата нажмите **Подключить чат**.

Установщик не содержит OBS. Для защищённых папок Windows может запросить права администратора. Пакеты пока не подписаны; контрольные суммы находятся в файле SHA256SUMS.txt релиза.

### Привязка аккаунта

Для ручного ключа регистрация приложения не нужна. **Для чата, зрителей и редактора нужен OAuth Client ID**: Google типа Desktop или Twitch типа Public. Разверните «Настройка подключения · Client ID» и воспользуйтесь кнопкой регистрации. Затем «Привязать аккаунт» откроет сайт площадки; для Twitch появится код подтверждения.

YouTube: включите YouTube Data API v3, разрешите свой аккаунт в тестовых пользователях, создайте трансляцию в Studio, загрузите список в StreamBridge и выберите нужную. Старые подключения нужно переподключить для новых прав. [Подробная настройка OAuth](docs/OAUTH.md).

**0.2.0 — предварительная версия.** Чат и действия API проверены автоматическими сценариями; вход и модерация на реальных аккаунтах ещё требуют проверки с зарегистрированными Client ID. Чат YouTube расходует квоту API. Отдельный текст уведомления о начале стрима не поддерживается публичными API: в плагине это локальный черновик, а описание YouTube публикуется отдельно. [Возможности и ограничения](docs/PLATFORMS.md).

## English

A free OBS Studio plugin for independent simultaneous **YouTube and Twitch** streaming, combined chat and browser sources with individual proxies. Windows x64; OBS 32 with Qt 6.8.

### Features

- Shared encoding or separate quality profiles; independent start, stop and reconnect alongside OBS's main stream.
- Viewer eye badges: grey for zero, muted red for positive counts, a dash when unavailable.
- Combined chat with platform icons; right-click to delete messages or ban users when authorized.
- Stream information dock for titles, categories and YouTube descriptions; copyable local go-live notification drafts.
- Per-source SOCKS5 / HTTP CONNECT browser proxies, encrypted credentials, Russian/English UI and GitHub updates.

### Setup

1. Download **setup.exe** from [Releases](https://github.com/AugustusNobilis/StreamBridge/releases), close OBS and select its root folder containing `bin/64bit/obs64.exe`. Portable installations are supported.
2. Existing users can run **updater.exe** from release 0.2.0, or use **Settings → Updates**. Enable preview releases for current development builds. Settings and credentials are preserved.
3. Enable the StreamBridge, Chat and Stream information panels in OBS's **Docks** menu.
4. Select a server and save your stream key, or connect an account. Click **Connect chat** in the chat dock.

For account features, register a **Google Desktop** / **Twitch Public** application and enter its Client ID under Account setup. Connect opens the platform website; Twitch uses a displayed device code. YouTube needs Data API v3 enabled, a permitted test user and an existing selected broadcast. Reconnect older accounts to grant new permissions. [OAuth setup](docs/OAUTH.md).

**0.2.0 is a development preview.** Real-account consent and moderation acceptance are pending registered Client IDs; automated tests use synthetic provider responses. YouTube chat consumes API quota. Separate go-live notification text is not exposed by these public APIs, so that field is a copyable local draft. [Platform behavior and limits](docs/PLATFORMS.md).

## License / Лицензия

Автор / Author: **[AugustusNobilis](https://github.com/AugustusNobilis)** and contributors. The official version is free / официальная версия бесплатна.

**GPL-2.0-or-later**: preserve the license and copyright notices and provide corresponding source code when distributing copies or modifications, as required by the GPL. При распространении сохраняйте лицензию, уведомления об авторстве и предоставляйте соответствующий исходный код на условиях GPL. Third-party components retain their own licenses.

[Build / Сборка](docs/BUILD-WINDOWS.md) · [Proxy browser](docs/PROXY-BROWSER.md) · [Security](SECURITY.md) · [Changelog](CHANGELOG.md) · [Third-party licenses](THIRD_PARTY_NOTICES.md)
