# StreamBridge 0.2.4

## Русский

Предварительная версия для Windows x64 и OBS Studio 32 с Qt 6.8.

- Панель «Данные трансляции» удалена. Начало и завершение эфира YouTube перенесены в основной блок YouTube. Название, описание, категорию и игру меняйте в YouTube Studio.
- Исправлено сохранение сервера: применение эфира YouTube сохраняет выбранный маршрут, а «Сохранить сервер и ключ» записывает оба значения. Резервный адрес с `backup=1` проходит проверку.
- Выбранный эфир и его название восстанавливаются после закрытия настроек, перезапуска OBS и обновления OAuth-токена.
- Чат получает резервный идентификатор из данных эфира. Временные сетевые ошибки повторяются автоматически. Если сохранённый эфир отсутствует или завершён, плагин ищет активный; при неоднозначности просит выбрать вручную. Чат и зрители следуют за выбранным эфиром без замены ключа работающего выхода.
- Сообщения YouTube имеют полупрозрачный красный фон, Twitch — фиолетовый. Добавлен отключаемый тихий звук: по умолчанию выключен, история не озвучивается, между сигналами не меньше 1,5 секунды.

### Как обновиться

1. В настройках плагина откройте **Обновления**, включите предварительные версии и установите **0.2.4**. Либо скачайте **updater.exe** из Assets.
2. Для первой установки скачайте **obs-streambridge-0.2.4-windows-x64-setup.exe** и выберите корневую папку OBS после его закрытия.
3. Откройте настройки YouTube и проверьте сервер и название выбранного эфира. В чате включите **Подключить чат**, при желании — **Тихий звук новых сообщений**.

[Настройка YouTube](https://github.com/AugustusNobilis/StreamBridge/blob/main/docs/YOUTUBE-SETUP.ru.md) · [Настройка Twitch](https://github.com/AugustusNobilis/StreamBridge/blob/main/docs/TWITCH-SETUP.ru.md)

`0*` у зрителей означает отсутствие числа в ответе YouTube: возможны ноль зрителей или скрытая статистика. `—` означает недоступные данные; подсказка показывает статус и выбранный эфир. Исправления проверяются на изолированных ответах API; аккаунт знакомого пользователя не проверялся. Официальная версия бесплатна, GPL-2.0-or-later, автор [AugustusNobilis](https://github.com/AugustusNobilis).

---

## English

A preview for Windows x64 and OBS Studio 32 with Qt 6.8.

- Removed the stream information dock. YouTube start/end controls moved to the main YouTube section. Edit title, description, category and game in YouTube Studio.
- Fixed server persistence: applying a YouTube broadcast preserves the selected route; **Save server and key** saves both. The official backup address with `backup=1` passes validation.
- The selected broadcast and its title survive reopening Settings, restarting OBS and refreshing OAuth tokens.
- Chat falls back to the broadcast's chat identifier. Temporary network failures retry automatically. Missing or completed selections trigger active-broadcast discovery; ambiguous choices require explicit selection. Chat and viewers follow the selected broadcast without replacing a running output's key.
- YouTube chat rows have a translucent red background; Twitch rows are purple. Optional quiet notifications are off by default, silent for history and limited to one sound per 1.5 seconds.

### Update

1. Open **Updates** in plugin settings, enable previews and install **0.2.4**. Alternatively, download **updater.exe** from Assets.
2. For a first installation, download **obs-streambridge-0.2.4-windows-x64-setup.exe**, close OBS and select its root folder.
3. Check the server and selected broadcast title in YouTube settings. Enable **Connect chat** and optionally **Quiet sound for new messages**.

[YouTube setup](https://github.com/AugustusNobilis/StreamBridge/blob/main/docs/YOUTUBE-SETUP.en.md) · [Twitch setup](https://github.com/AugustusNobilis/StreamBridge/blob/main/docs/TWITCH-SETUP.en.md)

`0*` means YouTube omitted the count: there may be zero viewers or hidden statistics. A dash means unavailable data; hover for status and the selected broadcast. Fixes use isolated provider-response tests; the friend's live account was not inspected. The official version is free, GPL-2.0-or-later, by [AugustusNobilis](https://github.com/AugustusNobilis).
