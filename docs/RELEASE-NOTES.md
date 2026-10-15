# StreamBridge 0.2.5

## Русский

Предварительная версия для Windows x64 и OBS Studio 32 с Qt 6.8.

- Время потока отображается как **часы:минуты:секунды**, без сброса часов через сутки.
- Чат получил собственную отрисовку: полупрозрачный красный/фиолетовый фон сохраняется при оформлении OBS, ники выделены жирным и цветом площадки, рядом значки ролей. Клик по нику открывает карточку участника.
- Поддерживаются смайлики Twitch и глобальные/канальные BetterTTV, включая анимированные GIF. Если сервер картинок недоступен, остаётся текст смайлика. Прямые GIF-ссылки поддерживаются с медиа-серверов Giphy и Tenor; страницы с GIF не встраиваются.
- Чаты подключаются автоматически, а после начала потока подключение обновляется. В доке — компактные **Online / Offline / Error + код**, подробности при наведении и кнопка переподключения со стрелкой.
- «Помощь» заменена вкладкой **Настройки** с параметрами чата. Звук: **Выключен**, **Astra (по умолчанию)** или локальный PCM WAV. У Astra прежняя мелодия с удвоенной амплитудой; добавлены громкость и прослушивание. Звук выключен по умолчанию, история не озвучивается.
- Журнал открывается отдельным окном по кнопке **О плагине → Открыть журнал**.

### После обновления

1. В **Обновлениях** разрешите предварительные версии и установите **0.2.5**, либо скачайте **updater.exe**. Для первой установки используйте **obs-streambridge-0.2.5-windows-x64-setup.exe**.
2. Проверьте привязку аккаунтов и выбранную трансляцию YouTube. Нажимать «Подключить чат» больше не требуется.
3. Для даты отслеживания в карточке пользователя один раз переподключите Twitch и подтвердите дополнительное разрешение `moderator:read:followers`. YouTube API не предоставляет такую дату для участников чата.
4. В **Настройках** выберите звук и громкость. Свой файл — PCM WAV: 8/16 бит, моно/стерео, 8–96 кГц, до 10 секунд и 4 МБ. Настройки применяются сразу. Звук может попасть в захват аудио рабочего стола.

Значки Twitch загружаются с площадки; YouTube передаёт только признаки ролей, поэтому для него используются локальные символы. GIF ограничены 512×512 и 2 МБ. Доступность картинок зависит от сети; в текущей проверке каталог BetterTTV и CDN Twitch доступны, CDN BetterTTV отвечал тайм-аутом. Анимация проверена на локальном GIF. Реальные эфиры при тестировании не запускались и не завершались.

[Настройка YouTube](https://github.com/AugustusNobilis/StreamBridge/blob/main/docs/YOUTUBE-SETUP.ru.md) · [Настройка Twitch](https://github.com/AugustusNobilis/StreamBridge/blob/main/docs/TWITCH-SETUP.ru.md)

Официальная версия бесплатна. GPL-2.0-or-later. Автор — [AugustusNobilis](https://github.com/AugustusNobilis).

---

## English

A preview for Windows x64 and OBS Studio 32 with Qt 6.8.

- Stream uptime uses **hours:minutes:seconds**, without a 24-hour rollover.
- Chat uses custom rendering: translucent platform backgrounds survive OBS themes, names are bold and platform-colored, role badges appear alongside them, and clicking a name opens a user card.
- Twitch and global/channel BetterTTV emotes support animated GIFs. Unavailable images fall back to readable emote text. Direct GIF links support Giphy and Tenor media hosts; GIF web pages are not embedded.
- Chats connect automatically and refresh when a stream starts. The dock shows compact **Online / Offline / Error + code**, detailed tooltips and an arrow reconnect button.
- Help is replaced by **Settings** containing chat controls. Choose **Off**, **Astra (default)** or a local PCM WAV, with volume and preview. Astra keeps the same melody at twice the amplitude. Sound remains off by default and history is silent.
- The log opens in a separate **About → Open log** dialog.

### After updating

1. Enable previews in **Updates** and install **0.2.5**, or download **updater.exe**. First installations use **obs-streambridge-0.2.5-windows-x64-setup.exe**.
2. Check linked accounts and the selected YouTube broadcast. A Connect chat click is no longer required.
3. Reconnect Twitch once and grant `moderator:read:followers` to see follow dates in user cards. YouTube API does not provide this date for arbitrary chat viewers.
4. Choose sound and volume under **Settings**. Custom files support PCM WAV: 8/16-bit mono/stereo, 8–96 kHz, up to 10 seconds and 4 MB. Changes apply immediately. Desktop Audio may capture playback.

Twitch badge artwork comes from the platform; YouTube provides role flags only, represented by local symbols. GIFs are limited to 512×512 and 2 MB. Image availability depends on the network; this check reached the BetterTTV catalog and Twitch CDN, but the BetterTTV image CDN timed out. Animation was validated with a local GIF fixture. No real broadcasts were started or ended during testing.

[YouTube setup](https://github.com/AugustusNobilis/StreamBridge/blob/main/docs/YOUTUBE-SETUP.en.md) · [Twitch setup](https://github.com/AugustusNobilis/StreamBridge/blob/main/docs/TWITCH-SETUP.en.md)

The official version is free. GPL-2.0-or-later. By [AugustusNobilis](https://github.com/AugustusNobilis).
