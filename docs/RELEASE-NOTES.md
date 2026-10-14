# StreamBridge 0.2.0 — development preview

Русский: добавлены счётчики зрителей с иконкой глаза, общий чат YouTube/Twitch со значками площадок и действиями модератора, отдельная докпанель названия/категории/описания. Упрощены вкладки подключения: выбор сервера, показ ключа, заметная привязка аккаунта и раскрываемая настройка Client ID. Новая прокручиваемая «Помощь», версии во вкладке обновлений, авторство и выбор языка RU/EN.

Для обновления скачайте updater.exe этого релиза: он обновляет старую установленную версию до 0.2.0 с сохранением настроек. Либо используйте встроенные обновления, включив предварительные версии. Закройте OBS обычным способом, когда обновлятор попросит.

English: viewer eye badges, combined YouTube/Twitch chat with platform icons and moderation, and a separate stream information dock. Simplified connection settings, scrollable Help, installed/latest version labels, author link and Russian/English language selection.

Use this release's updater.exe to migrate an older installation to 0.2.0, or enable preview releases in StreamBridge's Updates tab. Settings are preserved.

Known limits:
- Real-account OAuth/chat/moderation acceptance needs registered Google Desktop and Twitch Public Client IDs, which are not bundled. Older accounts need reconnecting to grant new permissions.
- Go-live notification text is a local copyable draft: the public APIs do not provide that separate setting. YouTube description is published.
- YouTube chat polling consumes API quota. No outgoing chat composer.
- Language selection takes effect after restarting OBS.
- Unsigned Windows x64 preview, built for OBS 32 / Qt 6.8. Windows 11 and hardware encoder acceptance remain separate checks.

The official version is free; GPL-2.0-or-later and third-party license obligations remain unchanged. Author: [AugustusNobilis](https://github.com/AugustusNobilis).
