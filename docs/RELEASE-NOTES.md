# StreamBridge 0.2.2 — обновление с перезапуском и вход в YouTube

Во вкладке «Обновления» теперь одна основная кнопка: проверить → скачать → **Установить и перезапустить OBS**. Настройки обновлений свёрнуты в отдельный блок. Перед установкой нужно остановить эфир, запись, буфер повтора и виртуальную камеру. OBS закрывается штатно и запускается после успешной установки с прежними правами. Отмена закрытия отменяет установку.

Для YouTube добавлен **импорт JSON Desktop-клиента из Google Cloud**. Это позволяет передать client_secret, когда Google требует его после согласия в браузере. Данные сохраняются локально в DPAPI вместе с токенами. Общего проекта YouTube нет: каждый использует свой.

Если после Google появляется «Return to OBS StreamBridge», а затем ошибка в OBS: откройте Google Auth Platform → Clients → свой Desktop-клиент → Download JSON. В StreamBridge выберите YouTube → Настройка подключения → Импортировать JSON из Google Cloud, затем подключите аккаунт заново. JSON никому отправлять не нужно.

[Подробная инструкция YouTube](https://github.com/AugustusNobilis/StreamBridge/blob/main/docs/YOUTUBE-SETUP.ru.md).

Для перехода со старых версий используйте **updater.exe из этого релиза** или встроенную загрузку с включёнными предварительными версиями. В старом интерфейсе ещё требуется вручную закрыть и открыть OBS. Новый автоматический перезапуск работает после установки 0.2.2 для следующих обновлений. Настройки и ключи сохраняются.

English: one primary update action and collapsible preferences; Install and restart OBS closes normally and relaunches only after successful installation, at the original privilege level. Active outputs block installation and a cancelled close cancels the update.

Google Desktop JSON import supplies client credentials during token exchange and refresh and stores them with DPAPI. Each user keeps their own Google project. Import the JSON locally after updating if consent returns to OBS but token exchange fails. [YouTube setup](https://github.com/AugustusNobilis/StreamBridge/blob/main/docs/YOUTUBE-SETUP.en.md).

Use this release's updater.exe to upgrade older versions. Older plugin interfaces still need a manual OBS close/reopen; automatic restart becomes available for subsequent updates after installing 0.2.2.

Preview limits: real Google/Twitch account acceptance and protected-folder UAC restart are not yet verified manually. Protocol tests use synthetic provider replies. Unsigned Windows x64 preview for OBS 32 / Qt 6.8. GPL-2.0-or-later; the official version is free. Author: [AugustusNobilis](https://github.com/AugustusNobilis).
