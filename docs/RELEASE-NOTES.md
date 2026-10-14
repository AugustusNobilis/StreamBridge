# StreamBridge 0.2.3

## Русский

Предварительная версия для Windows x64 и OBS Studio 32 с Qt 6.8.

- **Счётчик YouTube:** обновления больше не пропускаются при загрузке чата. Поддерживаются числовые и строковые значения. Подтверждённый ноль отображается как **0**; отсутствующее поле в активном эфире — как `0*` с пояснением «ноль или скрытый счётчик». Ошибка API, ещё не начатый и завершённый эфир показывают **—**.
- **Начало и завершение эфира:** в панели **StreamBridge — Данные трансляции → YouTube** появились соответствующие кнопки. Сначала запустите отправку видео в основной панели, затем нажмите **Начать эфир YouTube**. Плагин проверяет сигнал и ждёт подтверждения YouTube.
- **Завершение:** после подтверждения в плагине и успешного ответа YouTube эфир завершается, а выход YouTube останавливается. Twitch продолжает работать. Завершённый эфир нельзя возобновить.
- **Панель данных упрощена:** описание и уведомления удалены. Изменение названия и категории сохраняет существующее описание на YouTube.
- **Категория «Игры»:** названия категорий переведены на язык интерфейса; кнопка **Выбрать игру в YouTube Studio** открывает выбранный эфир. Отдельное название игры публичный API YouTube не позволяет задать.

### Установка

1. Для первой установки скачайте **obs-streambridge-0.2.3-windows-x64-setup.exe** из Assets.
2. Для старой установки используйте **updater.exe** либо **Настройки StreamBridge → Обновления** с включёнными предварительными версиями.
3. При ручной установке закройте OBS и выберите его корневую папку, содержащую **bin → 64bit → obs64.exe**. Настройки и ключи сохраняются.

[Инструкция YouTube](https://github.com/AugustusNobilis/StreamBridge/blob/main/docs/YOUTUBE-SETUP.ru.md) · [Инструкция Twitch](https://github.com/AugustusNobilis/StreamBridge/blob/main/docs/TWITCH-SETUP.ru.md)

Новые действия API проверены автоматическими сценариями; их проверка на реальном эфире остаётся за пользователем. Пакеты пока не подписаны. Официальная версия бесплатна, лицензия GPL-2.0-or-later. Автор — [AugustusNobilis](https://github.com/AugustusNobilis).

---

## English

A preview for Windows x64 and OBS Studio 32 with Qt 6.8.

- **YouTube viewers:** chat traffic no longer causes viewer refreshes to be skipped. Numeric and string counts are supported. A confirmed zero displays **0**; an omitted count during a live broadcast displays `0*` with a zero-or-hidden explanation. API failures, upcoming broadcasts and ended broadcasts display **—**.
- **Go live and end broadcast:** controls are available under **StreamBridge — Stream information → YouTube**. Start sending video from the main dock, then click **Go live on YouTube**. The plugin checks the signal and waits for YouTube confirmation.
- **Ending:** after confirmation in the plugin and a successful YouTube response, the broadcast ends and the YouTube output stops. Twitch continues. A completed broadcast cannot be resumed.
- **Simpler information dock:** description and notification fields have been removed. Title/category edits preserve the existing YouTube description.
- **Gaming:** category names follow the interface language. **Choose game in YouTube Studio** opens the selected broadcast because the public API does not support setting the specific game title.

### Installation

1. For a first installation, download **obs-streambridge-0.2.3-windows-x64-setup.exe** from Assets.
2. For an existing installation, use **updater.exe** or **StreamBridge Settings → Updates** with preview releases enabled.
3. For manual installation, close OBS and select its root folder containing **bin → 64bit → obs64.exe**. Settings and credentials are preserved.

[YouTube instructions](https://github.com/AugustusNobilis/StreamBridge/blob/main/docs/YOUTUBE-SETUP.en.md) · [Twitch instructions](https://github.com/AugustusNobilis/StreamBridge/blob/main/docs/TWITCH-SETUP.en.md)

New API actions have automated coverage; real-broadcast acceptance remains for the user. Packages are currently unsigned. The official version is free, licensed under GPL-2.0-or-later. Created by [AugustusNobilis](https://github.com/AugustusNobilis).
