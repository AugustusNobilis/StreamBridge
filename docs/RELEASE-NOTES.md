# StreamBridge 0.2.2

## Русский

Предварительная версия для **Windows x64 и OBS Studio 32 с Qt 6.8**.

### Что изменилось

- Во вкладке «Обновления» одна основная кнопка последовательно проверяет наличие версии, скачивает её и предлагает **Установить и перезапустить OBS**.
- После успешной установки OBS запускается снова. Перед обновлением нужно остановить трансляции, запись, буфер повтора и виртуальную камеру.
- Для входа в YouTube добавлен **импорт JSON Desktop-клиента Google Cloud**. Он устраняет проблему, при которой согласие в браузере получено, но Google требует дополнительные данные клиента для завершения входа.

### Что скачать

| Файл | Когда использовать |
| --- | --- |
| **obs-streambridge-0.2.2-windows-x64-setup.exe** | Первая установка или установка поверх существующей версии. |
| **updater.exe** | Обновление старого StreamBridge: небольшая программа скачает установщик и обновит выбранную установку OBS. |
| **SHA256SUMS.txt** | Проверка контрольных сумм скачанных файлов. |

1. Закройте OBS и запустите выбранную программу.
2. Укажите корневую папку OBS, содержащую **bin → 64bit → obs64.exe**.
3. Дождитесь окончания установки и откройте OBS. Настройки и ключи сохраняются.

При переходе со старой версии на 0.2.2 OBS нужно закрыть и открыть вручную. Кнопка установки с автоматическим перезапуском станет доступна в самом плагине для следующих обновлений. Отдельный **updater.exe** не закрывает и не перезапускает OBS автоматически.

Для получения этой версии через настройки плагина включите **Получать опубликованные предварительные версии**. Сборка пока не имеет цифровой подписи.

### Как подключить YouTube

1. В своём проекте Google Cloud включите YouTube Data API v3 и добавьте аккаунт в тестовые пользователи.
2. В **Google Auth Platform → Clients → Create client** создайте **Desktop app**.
3. **Скачайте JSON в окне создания клиента до его закрытия.** Если прежний файл потерян, создайте ещё один Desktop-клиент в том же проекте.
4. В **Настройки StreamBridge → YouTube → Настройка подключения · Client ID** нажмите **Импортировать JSON из Google Cloud…**, выберите файл и подключите аккаунт.

[Подробная настройка YouTube](https://github.com/AugustusNobilis/StreamBridge/blob/main/docs/YOUTUBE-SETUP.ru.md) · [Подробная настройка Twitch](https://github.com/AugustusNobilis/StreamBridge/blob/main/docs/TWITCH-SETUP.ru.md) · [Установка и использование](https://github.com/AugustusNobilis/StreamBridge#русский)

Автор — [AugustusNobilis](https://github.com/AugustusNobilis). Официальная версия бесплатна; исходный код распространяется под GPL-2.0-or-later.

---

## English

A preview release for **Windows x64 and OBS Studio 32 with Qt 6.8**.

### What changed

- One main button on the Updates tab checks for a release, downloads it and offers **Install and restart OBS**.
- OBS launches again after successful installation. Stop streams, recording, replay buffer and virtual camera before updating.
- **Google Cloud Desktop client JSON import** completes YouTube sign-in when browser consent succeeds but Google requires additional client credentials.

### What to download

| File | When to use it |
| --- | --- |
| **obs-streambridge-0.2.2-windows-x64-setup.exe** | First installation or installation over an existing version. |
| **updater.exe** | Updating an older StreamBridge installation: this small program downloads the installer and updates the selected OBS installation. |
| **SHA256SUMS.txt** | Verifying downloaded file checksums. |

1. Close OBS and run the chosen program.
2. Select the OBS root folder containing **bin → 64bit → obs64.exe**.
3. Wait for installation to finish, then open OBS. Settings and credentials are preserved.

When upgrading from an older version to 0.2.2, close and reopen OBS manually. The in-plugin installation button with automatic restart becomes available for subsequent updates. The standalone **updater.exe** does not automatically close or restart OBS.

Enable **Include published preview releases** to receive this version through plugin settings. The build is currently unsigned.

### How to connect YouTube

1. Enable YouTube Data API v3 in your own Google Cloud project and add your account as a test user.
2. In **Google Auth Platform → Clients → Create client**, create a **Desktop app**.
3. **Download JSON in the creation dialog before closing it.** If the previous file was lost, create another Desktop client in the same project.
4. Open **StreamBridge Settings → YouTube → Account setup · Client ID**, click **Import JSON from Google Cloud…**, select the file and connect your account.

[Complete YouTube setup](https://github.com/AugustusNobilis/StreamBridge/blob/main/docs/YOUTUBE-SETUP.en.md) · [Complete Twitch setup](https://github.com/AugustusNobilis/StreamBridge/blob/main/docs/TWITCH-SETUP.en.md) · [Installation and usage](https://github.com/AugustusNobilis/StreamBridge#english)

Created by [AugustusNobilis](https://github.com/AugustusNobilis). The official version is free; source code is licensed under GPL-2.0-or-later.
