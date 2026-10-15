# OBS StreamBridge

## Русский

Бесплатный плагин для OBS Studio, который позволяет одновременно вести трансляции на **YouTube и Twitch**, читать сообщения обеих площадок в одном чате и управлять эфиром YouTube прямо в OBS.

**[Скачать StreamBridge](https://github.com/AugustusNobilis/StreamBridge/releases/tag/v0.2.5)** · [Настроить YouTube](docs/YOUTUBE-SETUP.ru.md) · [Настроить Twitch](docs/TWITCH-SETUP.ru.md)

### Возможности

- Независимый запуск и остановка трансляции на каждой площадке.
- Общие настройки кодирования или отдельное качество для YouTube и Twitch.
- Общий чат со значками площадок, удалением сообщений и блокировкой пользователей при наличии прав.
- Счётчики зрителей и управление эфиром YouTube в основной панели.
- Браузерные источники с индивидуальными прокси.
- Русский и английский интерфейс, шифрование сохранённых ключей и обновления из GitHub.

### 1. Скачайте и установите

Текущая сборка **0.2.5 — предварительная**, для **Windows x64 и OBS Studio 32 с Qt 6.8**. OBS нужно установить отдельно; поддерживается также portable-версия.

1. Откройте [страницу версии 0.2.5](https://github.com/AugustusNobilis/StreamBridge/releases/tag/v0.2.5) и разверните список **Assets** внизу.
2. Если устанавливаете плагин впервые, скачайте **obs-streambridge-0.2.5-windows-x64-setup.exe**. Для обновления уже установленного плагина можно скачать **updater.exe**.
3. Закройте OBS. Запустите скачанный файл.
4. Укажите **корневую папку OBS**, внутри которой находится **bin → 64bit → obs64.exe**. Например, **C:\Program Files\obs-studio**. Не выбирайте саму папку bin или obs-plugins.
5. Дождитесь завершения установки и откройте OBS.

Для установки в защищённую папку Windows может запросить права администратора. Сборка пока не имеет цифровой подписи; файл **SHA256SUMS.txt** в релизе содержит контрольные суммы.

### 2. Откройте панели и настройки

1. В меню OBS **Док-панели** включите **StreamBridge** и **StreamBridge — Чат**.
2. В основной панели StreamBridge откройте **Настройки StreamBridge**.
3. При необходимости выберите язык на вкладке **О плагине**, затем перезапустите OBS.
4. Настройте площадки по следующему разделу. Можно использовать только одну из них.

### 3. Настройте YouTube и Twitch

**Если нужно только передавать видео**, достаточно ключа трансляции:

1. Для YouTube откройте [YouTube Studio](https://studio.youtube.com/) → **Создать → Начать трансляцию**, подготовьте эфир через видеокодер и скопируйте его ключ потока. Для Twitch откройте [кабинет автора](https://dashboard.twitch.tv/) → **Настройки → Трансляция** и скопируйте основной ключ.
2. В настройках StreamBridge откройте вкладку нужной площадки.
3. Выберите основной сервер YouTube или автоматический сервер Twitch.
4. Вставьте значение в поле **Ключ трансляции** и нажмите **Сохранить сервер и ключ (с шифрованием)**. После успешного сохранения поле очищается — это нормально.
5. Нажмите **Сохранить** внизу окна, чтобы сохранить остальные настройки. Для второй площадки повторите эти действия.

**Если нужны чат, зрители и управление данными эфира**, подключите аккаунт. Для этого один раз создайте своё приложение площадки:

| Площадка | Что подготовить | Инструкция |
| --- | --- | --- |
| YouTube | Собственный проект Google Cloud, включённый YouTube Data API v3 и JSON клиента типа Desktop app | [Все шаги настройки YouTube](docs/YOUTUBE-SETUP.ru.md) |
| Twitch | Приложение типа Public и его Client ID | [Все шаги настройки Twitch](docs/TWITCH-SETUP.ru.md) |

В настройках площадки раскройте **Настройка подключения · Client ID**. Для YouTube импортируйте JSON, для Twitch вставьте Client ID, затем нажмите подключение аккаунта и подтвердите доступ в браузере.

Для Google скачайте JSON **сразу в окне создания клиента**: у уже созданного клиента Google не показывает полный секрет повторно. Если файл потерян, в [инструкции YouTube](docs/YOUTUBE-SETUP.ru.md#если-json-не-сохранили) описано восстановление настройки в том же проекте.

После входа выберите существующую трансляцию YouTube кнопками **Загрузить существующие трансляции → Использовать выбранную трансляцию**. Для Twitch нажмите **Использовать ключ этого аккаунта**. Дождитесь подтверждения и сохраните настройки.

### 4. Запустите трансляцию

1. На вкладке кодирования выберите доступный энкодер и настройте качество. Для одинакового качества на обеих площадках используйте общее кодирование; для разного включите **Независимое кодирование**.
2. Сохраните настройки и нажмите **Запустить** рядом с нужной площадкой в основной панели StreamBridge. **Запустить всё** включает оба настроенных выхода.
3. Проверьте получение сигнала в кабинете площадки. Если аккаунт YouTube подключён, нажмите **Начать эфир YouTube** в основном блоке **YouTube**. Без привязки аккаунта используйте Studio или автоматический старт.
4. Для завершения остановите нужный выход в StreamBridge и проверьте статус эфира на площадке.

Кнопки StreamBridge управляют его собственными выходами независимо от основной кнопки трансляции OBS. Перед изменением настроек площадок остановите выходы StreamBridge.

### 5. Пользуйтесь чатом и управлением эфиром

- Чаты подключаются автоматически после привязки аккаунтов; при старте потока плагин обновляет подключение. В панели **StreamBridge — Чат** показаны статусы Online / Offline / Error с кодом. Подробности — при наведении. Кнопка со стрелкой переподключает чаты.
- Сообщения YouTube выделены красным, Twitch — фиолетовым; ник жирный, рядом значки ролей. Нажмите на ник, чтобы открыть карточку. Для даты отслеживания Twitch один раз переподключите аккаунт и разрешите чтение отслеживающих канал. YouTube такую дату через API не предоставляет.
- Смайлики Twitch и глобальные/канальные BetterTTV, включая анимированные GIF, поддерживаются автоматически. Прямые GIF-ссылки отображаются с media.tenor.com и медиа-серверов Giphy; обычные ссылки на страницы не являются GIF. Правая кнопка сообщения открывает копирование, удаление и бан при наличии прав.
- Откройте **Настройки StreamBridge → Настройки**. Здесь можно отключить автоподключение, выбрать **Выключен**, **Astra (по умолчанию)** или **Свой звук с компьютера**, настроить громкость и прослушать сигнал. Свой файл: PCM WAV, 8/16 бит, моно/стерео, 8–96 кГц, до 10 секунд и 4 МБ. Настройки применяются сразу.
- Astra — прежний мягкий сигнал с удвоенной амплитудой. Звук по умолчанию выключен, история без звука; между уведомлениями не меньше 1,5 секунды. Звук может попасть в захват рабочего стола. Журнал доступен через **О плагине → Открыть журнал**.
- Выбранный эфир YouTube сохраняется. Если он отсутствует или завершён, плагин ищет активный эфир; при нескольких вариантах выберите нужный в настройках YouTube. Во время отправки видео выбор меняет только отслеживание чата и зрителей.
- **Зрители:** серый счётчик — 0, приглушённый красный — 1 и больше. «0*» означает, что YouTube не передал число: возможны ноль зрителей или скрытая статистика. **—** означает недоступные данные. Наведите указатель для статуса и названия выбранного эфира.
- **Завершить эфир YouTube** в основной панели завершает эфир после подтверждения и останавливает только выход YouTube. Twitch продолжает работать. Завершённый эфир нельзя возобновить.
- Отдельная панель данных трансляции удалена. Название, описание, категорию и игру редактируйте в кабинете площадки. Кнопка **Открыть YouTube Studio** открывает выбранный эфир.

Каждый пользователь YouTube использует собственный проект Google Cloud. Чат и другие запросы расходуют его квоту. [Инструкция и ограничения](docs/YOUTUBE-SETUP.ru.md).
### 6. Обновляйте плагин

1. Откройте **Настройки StreamBridge → Обновления**. Здесь показаны установленная и последняя найденная версии.
2. Разверните **Настройки обновлений**. Для получения текущих предварительных сборок включите **Получать опубликованные предварительные версии**.
3. Нажмите **Проверить сейчас**, затем **Загрузить обновление** — это одна кнопка, действие которой меняется по мере готовности.
4. Остановите трансляции, запись, буфер повтора и виртуальную камеру.
5. Нажмите **Установить и перезапустить OBS**. OBS закроется, обновление установится, после успешной установки OBS запустится снова. Если Windows запросит разрешение на установку, подтвердите его.

Настройки и ключи сохраняются. Устанавливать Git не требуется. Автоматическая проверка и загрузка включаются отдельно в настройках; установку запускает пользователь.

При обновлении **версии ниже 0.2.2** закрыть и открыть OBS ещё нужно вручную. Можно использовать **updater.exe** из релиза: он скачает установщик и обновит выбранную установку OBS. Сам updater.exe не закрывает и не перезапускает OBS автоматически. Новая кнопка перезапуска доступна в плагине начиная с 0.2.2.

### Если возникла проблема

| Что происходит | Что сделать |
| --- | --- |
| Плагин не появился в OBS | Проверьте выбранную при установке папку, совместимость версии OBS и меню «Док-панели». |
| Установщик просит закрыть OBS, хотя окна нет | Проверьте процессы OBS и браузера StreamBridge в диспетчере задач. Убедитесь, что выбрана правильная папка и есть право записи в неё. |
| YouTube показывает 403 или ошибку после входа | Следуйте [разделу об ошибках YouTube](docs/YOUTUBE-SETUP.ru.md#если-не-получается). |
| Не удаётся зарегистрировать или подключить Twitch | Проверьте поля по [инструкции Twitch](docs/TWITCH-SETUP.ru.md). |
| Видео идёт, но чата или зрителей нет | Ключ потока сам по себе не подключает аккаунт. Для YouTube также нужно выбрать нужную трансляцию. |
| Новая версия не найдена | Включите получение предварительных версий и повторите проверку. |
| Нужен прокси для виджета | Добавьте **Proxied Browser Source** и откройте **Настроить URL и прокси безопасно…**. Прокси этого источника не применяется к трансляциям, входу в аккаунты или чату. |

Если проблема остаётся, создайте [обращение на GitHub](https://github.com/AugustusNobilis/StreamBridge/issues). Укажите версии OBS и StreamBridge, шаги и текст ошибки. Не прикладывайте ключи трансляций, токены или JSON Google.

### Автор и лицензия

Автор — **[AugustusNobilis](https://github.com/AugustusNobilis)**. Официальная версия бесплатна, исходный код открыт под **[GPL-2.0-or-later](LICENSE)**. Условия распространения и предоставления исходного кода определяются GPL; лицензии сторонних компонентов указаны отдельно.

Для разработчиков: [сборка под Windows](docs/BUILD-WINDOWS.md), [безопасность](SECURITY.md), [сторонние компоненты](THIRD_PARTY_NOTICES.md).

---

## English

A free OBS Studio plugin for streaming to **YouTube and Twitch** at the same time, reading both platforms in one chat and controlling YouTube broadcasts inside OBS.

**[Download StreamBridge](https://github.com/AugustusNobilis/StreamBridge/releases/tag/v0.2.5)** · [Set up YouTube](docs/YOUTUBE-SETUP.en.md) · [Set up Twitch](docs/TWITCH-SETUP.en.md)

### Features

- Independent start and stop controls for each platform.
- Shared encoding settings or separate quality settings for YouTube and Twitch.
- Combined chat with platform icons, message deletion and user bans when permitted.
- Viewer counts and YouTube broadcast controls in the main dock.
- Browser sources with individual proxies.
- Russian and English interfaces, encrypted saved credentials and updates from GitHub.

### 1. Download and install

The current **0.2.5 preview** targets **Windows x64 and OBS Studio 32 with Qt 6.8**. Install OBS separately; portable installations are also supported.

1. Open the [0.2.5 release page](https://github.com/AugustusNobilis/StreamBridge/releases/tag/v0.2.5) and expand **Assets** at the bottom.
2. For a first installation, download **obs-streambridge-0.2.5-windows-x64-setup.exe**. To update an existing plugin, you can use **updater.exe**.
3. Close OBS and run the downloaded file.
4. Select the **OBS root folder** containing **bin → 64bit → obs64.exe**, for example **C:\Program Files\obs-studio**. Do not select bin or obs-plugins itself.
5. Wait for installation to finish, then open OBS.

Windows may request administrator permission for a protected installation folder. The build is currently unsigned; **SHA256SUMS.txt** in the release lists file checksums.

### 2. Open the docks and settings

1. In OBS's **Docks** menu, enable **StreamBridge** and **StreamBridge — Chat**.
2. Open **StreamBridge Settings** from the main StreamBridge dock.
3. If needed, choose your language on the **About** tab, then restart OBS.
4. Configure your platforms below. Using just one platform is supported.

### 3. Configure YouTube and Twitch

**To send video only**, a stream key is enough:

1. For YouTube, open [YouTube Studio](https://studio.youtube.com/) → **Create → Go live**, prepare an encoder broadcast and copy its stream key. For Twitch, open [Creator Dashboard](https://dashboard.twitch.tv/) → **Settings → Stream** and copy the primary stream key.
2. Open the platform's tab in StreamBridge settings.
3. Select the primary YouTube server or the automatic Twitch server.
4. Paste the value into **Stream key** and click **Save server and key (encrypted)**. The field clears after a successful save; this is expected.
5. Click **Save** at the bottom to save the other settings. Repeat for the second platform if needed.

**For chat, viewers and YouTube broadcast controls**, connect an account. Register your own platform application once:

| Platform | What you need | Instructions |
| --- | --- | --- |
| YouTube | Your own Google Cloud project, YouTube Data API v3 enabled and a Desktop app client JSON file | [Complete YouTube setup](docs/YOUTUBE-SETUP.en.md) |
| Twitch | A Public application and its Client ID | [Complete Twitch setup](docs/TWITCH-SETUP.en.md) |

Expand **Account setup · Client ID** on the platform tab. Import the JSON for YouTube or paste the Client ID for Twitch, then click the account connection button and authorize in your browser.

Download the Google JSON **in the client creation dialog before closing it**. Google does not reveal the existing client's full secret again. If you lost the file, follow [the recovery steps](docs/YOUTUBE-SETUP.en.md#if-you-did-not-save-the-json) within the same project.

After sign-in, select an existing YouTube broadcast using **Load existing broadcasts → Use selected broadcast**. For Twitch, click **Use this account's stream key**. Wait for confirmation and save settings.

### 4. Start streaming

1. On the encoding tab, select an available encoder and configure quality. Use shared encoding for matching quality on both platforms, or enable **Independent encoding** for separate settings.
2. Save settings and click **Start** beside the platform in the main StreamBridge dock. **Start All** starts both configured outputs.
3. Check the incoming signal in the platform dashboard. With a connected YouTube account, click **Go live on YouTube** in the main **YouTube** section. Without an account connection, use Studio or auto-start.
4. To finish, stop the output in StreamBridge and check the broadcast status on the platform.

StreamBridge controls its own outputs independently of OBS's main streaming button. Stop StreamBridge outputs before changing platform settings.

### 5. Use chat and broadcast controls

- Chats connect automatically once accounts are linked and refresh when a stream starts. **StreamBridge — Chat** shows Online / Offline / Error with a code; hover for details and use the arrow button to reconnect.
- YouTube rows are red-tinted, Twitch rows purple, with bold names and role badges. Click a name for its user card. To see Twitch follow dates, reconnect Twitch once and grant follower access. YouTube does not expose this date through its API.
- Twitch and global/channel BetterTTV emotes, including animated GIFs, are automatic. Direct GIF links support media.tenor.com and Giphy media hosts; ordinary page links are not GIFs. Right-click a message for copying, deletion or banning when authorized.
- Open **StreamBridge settings → Settings** to toggle automatic connection, choose **Off**, **Astra (default)** or **Local sound file**, set volume and preview. Local files support PCM WAV, 8/16-bit mono/stereo, 8–96 kHz, up to 10 seconds and 4 MB. Changes apply immediately.
- Astra retains the original soft melody at twice the amplitude. Sound is off by default, history is silent and notifications are spaced at least 1.5 seconds apart. Desktop Audio may capture it. Find the log under **About → Open log**.
- The selected YouTube broadcast is remembered. If missing or completed, the plugin looks for an active broadcast; choose explicitly in YouTube settings when there are multiple candidates. During video transmission, selection changes monitoring only.
- **Viewers:** grey means zero, muted red means one or more. «0*» means YouTube omitted the count: it may be zero or hidden. A dash means unavailable data. Hover for status and the selected broadcast title.
- **End YouTube broadcast** in the main dock ends the broadcast after confirmation and stops only the YouTube output. Twitch continues. Completed broadcasts cannot be resumed.
- The separate stream information dock has been removed. Edit title, description, category and game in the platform dashboard. **Open YouTube Studio** opens the selected broadcast.

Each YouTube user uses their own Google Cloud project. Chat and other API requests use that project's quota. [Setup and limitations](docs/YOUTUBE-SETUP.en.md).
### 6. Update the plugin

1. Open **StreamBridge Settings → Updates** to see the installed version and the latest version found.
2. Expand **Update preferences** and enable **Include published preview releases** to receive the current previews.
3. Click **Check now**, then **Download update**. The same button changes its action as the update becomes ready.
4. Stop streams, recording, replay buffer and virtual camera.
5. Click **Install and restart OBS**. OBS closes, the update installs, and OBS starts again after successful installation. Approve the Windows installation prompt if one appears.

Settings and credentials are preserved. Git is not required. Automatic checks and downloads are optional; the user starts installation.

When upgrading **a version older than 0.2.2**, close and reopen OBS manually. Alternatively, use this release's **updater.exe** to download the installer and update the selected OBS installation. The standalone updater does not automatically close or restart OBS. The new restart button is available inside the plugin from 0.2.2 onward.

### Troubleshooting

| Problem | What to check |
| --- | --- |
| The plugin is missing in OBS | Check the installation folder, OBS version compatibility and the Docks menu. |
| The installer asks to close OBS but no window is open | Check Task Manager for OBS and StreamBridge browser processes. Verify the selected folder and write permissions. |
| YouTube shows a 403 or fails after sign-in | Follow [YouTube troubleshooting](docs/YOUTUBE-SETUP.en.md#troubleshooting). |
| Twitch registration or sign-in fails | Check the fields in the [Twitch guide](docs/TWITCH-SETUP.en.md). |
| Video works but chat or viewers are missing | A stream key alone does not connect an account. YouTube also needs the correct broadcast selected. |
| No update is found | Enable preview releases and check again. |
| A widget needs a proxy | Add **Proxied Browser Source** and open **Configure URL and proxy securely…**. Its proxy does not apply to streams, account sign-in or chat. |

If the problem remains, [open an issue on GitHub](https://github.com/AugustusNobilis/StreamBridge/issues). Include OBS and StreamBridge versions, reproduction steps and the error text. Do not attach stream keys, tokens or Google credential JSON.

### Author and license

Created by **[AugustusNobilis](https://github.com/AugustusNobilis)**. The official version is free and the source is available under **[GPL-2.0-or-later](LICENSE)**. Distribution and corresponding-source obligations are governed by the GPL; third-party licenses are listed separately.

For developers: [Windows build instructions](docs/BUILD-WINDOWS.md), [security](SECURITY.md), [third-party components](THIRD_PARTY_NOTICES.md).
