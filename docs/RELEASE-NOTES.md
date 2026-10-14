# StreamBridge 0.2.1 — setup help and updater fix

Русский: исправлено обновление при быстром закрытии OBS. Раньше установщик мог завершить распаковку после выхода OBS и ошибочно отклонить обновление. Проверки занятых файлов, папки установки и прав сохранены.

На вкладках YouTube и Twitch появилась кнопка пошаговой инструкции. Инструкции встроены в плагин, работают без интернета и доступны на русском и английском.

- [YouTube: Google Cloud, тестировщики и ошибка 403](https://github.com/AugustusNobilis/StreamBridge/blob/main/docs/YOUTUBE-SETUP.ru.md).
- [Twitch: поля регистрации, Public и вход по коду](https://github.com/AugustusNobilis/StreamBridge/blob/main/docs/TWITCH-SETUP.ru.md).

Для обновления используйте **updater.exe из этого релиза**, либо встроенные обновления с включённым получением предварительных версий. Автозагрузка только скачивает пакет: после её окончания нажмите **Установить после закрытия OBS** и закройте OBS. Настройки и ключи сохраняются.

English: fixes installation when OBS exits before the compressed updater initializes. Destination, permissions and in-use-file checks remain active. Adds offline step-by-step YouTube and Twitch guides in both languages beside the account buttons.

Use this release's **updater.exe**, or enable preview releases and click **Install after OBS closes** after downloading through the plugin. Automatic downloads do not schedule installation on their own. Settings and credentials are preserved.

Known limits: registered Google Desktop/Twitch Public Client IDs are still required for account features. Real-account consent and moderation acceptance have not been completed in the development environment. Google Testing sessions for YouTube scopes expire after seven days. The release is an unsigned Windows x64 preview for OBS 32 / Qt 6.8.

The official version is free; GPL-2.0-or-later and third-party license obligations remain unchanged. Author: [AugustusNobilis](https://github.com/AugustusNobilis).
