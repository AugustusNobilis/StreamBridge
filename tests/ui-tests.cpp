// SPDX-License-Identifier: GPL-2.0-or-later
// Production widgets rendered with synthetic data and no platform network access.
#include "ui/dock.hpp"
#include "ui/platform-docks.hpp"
#include "ui/settings.hpp"
#include "updates/checker.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtEndian>
#include <iostream>
#include <stdexcept>
#include <util/text-lookup.h>
static lookup_t *strings = nullptr;
class NoNetwork final : public sb::HttpClient {
    void send(const QUrl &, const QUrlQuery &, const QByteArray &, const QByteArray &, bool,
              Callback cb) override {
        cb({0, {}, true});
    }
    void cancel() override {}
};
namespace sb {
QString text(const char *key) {
    const char *value = nullptr;
    return strings && text_lookup_getstr(strings, key, &value) ? QString::fromUtf8(value) : key;
}
QString language() {
    return {};
}
bool setLanguage(const QString &) {
    return true;
}
QString browserSummary() {
    return "Proxy browser sources: 0";
}
} // namespace sb
static void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
    std::cout << "PASS: " << message << std::endl;
}
static void render(QWidget &widget, const QString &file) {
    widget.setAttribute(Qt::WA_DontShowOnScreen);
    widget.show();
    QApplication::processEvents();
    require(widget.grab().save(file), "production widget preview saved");
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#26282f"));
    palette.setColor(QPalette::WindowText, QColor("#e6e6e8"));
    palette.setColor(QPalette::Base, QColor("#1e2027"));
    palette.setColor(QPalette::AlternateBase, QColor("#30323a"));
    palette.setColor(QPalette::Text, QColor("#e6e6e8"));
    palette.setColor(QPalette::Button, QColor("#3b3e49"));
    palette.setColor(QPalette::ButtonText, QColor("#e6e6e8"));
    palette.setColor(QPalette::Highlight, QColor("#4e659f"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(palette);
    try {
        require(argc == 3, "locale directory and screenshot directory supplied");
        require(obs_startup("en-US", nullptr, nullptr), "isolated OBS core initialized");
        const QString localeDir = QString::fromLocal8Bit(argv[1]), images = QString::fromLocal8Bit(argv[2]);
        QDir().mkpath(images);
        {
            QTemporaryDir directory;
            sb::OutputManager manager(directory.path());
            sb::PlatformHub hub(manager.account(0), manager.account(1));
            sb::initializeUpdates(directory.path());
            for (const auto *locale : {"ru-RU", "en-US"}) {
                if (strings)
                    text_lookup_destroy(strings);
                strings = text_lookup_create((localeDir + "/" + locale + ".ini").toUtf8().constData());
                require(strings != nullptr, "real locale file loaded");
                QWidget canvas;
                auto *row = new QHBoxLayout(&canvas);
                auto *main = new sb::Dock(manager, hub, &canvas);
                main->setFixedWidth(290);
                int badge = 0;
                for (auto *widget : main->findChildren<QWidget *>())
                    if (auto *viewer = dynamic_cast<sb::ViewerBadge *>(widget))
                        viewer->setCount(badge++ == 0 ? 0 : 12);
                require(badge == 2, "one accessible viewer badge per platform");
                row->addWidget(main);
                unsigned notifications = 0;
                auto *chat = sb::createChatDock(hub, directory.path(), [&] { ++notifications; });
                chat->setFixedWidth(320);
                row->addWidget(chat);
                require(main->findChild<QPushButton *>("youtubeGoLive") &&
                            main->findChild<QPushButton *>("youtubeEndLive"),
                        "YouTube lifecycle controls moved to the main dock");
                emit hub.messageReceived({0, "demo-y", "viewer-a", "Алексей", "Привет! Отлично слышно.",
                                          "demo", QDateTime::currentDateTime(), false});
                emit hub.messageReceived({1, "demo-t", "viewer-b", "Nora", "Hello from Twitch 👋", "demo",
                                          QDateTime::currentDateTime(), false});
                emit hub.messageReceived({0, "demo-plain", "viewer-c", "<b>plain text</b>",
                                          "<script>never executed</script>", "demo",
                                          QDateTime::currentDateTime(), false});
                auto *list = chat->findChild<QListWidget *>();
                require(list && list->count() == 3 && !list->item(0)->icon().isNull() &&
                            list->item(2)->text().contains("<script>"),
                        "mixed chat has platform icons and plain-text content");
                require(list->item(0)->background().color().alpha() == 32 &&
                            list->item(1)->background().color().alpha() == 34 &&
                            list->item(0)->background().color().red() >
                                list->item(0)->background().color().blue(),
                        "chat rows use translucent platform colors");
                auto *sound = chat->findChild<QCheckBox *>("chatSound");
                require(sound && !sound->isChecked() && notifications == 0, "chat sound is off by default");
                sound->setChecked(true);
                emit hub.messageReceived({0, "history", "viewer-a", "User", "Old", "demo", {}, false, false});
                require(notifications == 0, "chat history remains silent");
                emit hub.messageReceived({0, "fresh", "viewer-a", "User", "New", "demo", {}, false, true});
                emit hub.messageReceived({1, "burst", "viewer-a", "User", "New", "demo", {}, false, true});
                require(notifications == 1, "new-message sound is throttled across platforms");
                QSettings preferences(directory.path() + "/ui.ini", QSettings::IniFormat);
                require(preferences.value("chat/sound").toBool(), "sound preference is persisted");
                sound->setChecked(false);
                canvas.resize(650, 730);
                QApplication::processEvents();
                badge = 0;
                for (auto *widget : main->findChildren<QWidget *>())
                    if (auto *viewer = dynamic_cast<sb::ViewerBadge *>(widget))
                        viewer->setCount(badge++ == 0 ? 0 : 12);
                render(canvas, images + "/docks-" + locale + ".png");
                for (int i = 0; i < 510; ++i)
                    emit hub.messageReceived(
                        {0, QString::number(i), "viewer-a", "User", "Test", "demo", {}, false});
                require(list->count() == 500, "chat history remains bounded");
                emit hub.messagesRemoved(0, {}, "viewer-a");
                require(list->count() == 0, "user moderation removes matching displayed messages");
                sb::Settings settings(manager);
                settings.resize(720, 760);
                auto *tabs = settings.findChild<QTabWidget *>();
                require(tabs && tabs->count() == 7,
                        "settings contain platform, encoding, help, logs, updates and about tabs");
                require(tabs->widget(5)->findChildren<QPushButton *>().size() == 1,
                        "updates tab offers one primary action");
                for (int tab : {0, 1, 3, 5, 6}) {
                    tabs->setCurrentIndex(tab);
                    render(settings, images + "/settings-" + locale + "-" + QString::number(tab) + ".png");
                }
            }
            const auto wave = sb::chatNotificationWave();
            int peak = 0;
            for (int i = 44; i < wave.size(); i += 2)
                peak = std::max(peak,
                                std::abs(static_cast<int>(qFromLittleEndian<qint16>(wave.constData() + i))));
            require(wave.startsWith("RIFF") && wave.size() == 7724 && peak > 0 && peak <= 900 &&
                        qFromLittleEndian<qint16>(wave.constData() + 44) == 0 &&
                        qFromLittleEndian<qint16>(wave.constData() + wave.size() - 2) == 0,
                    "notification PCM is short, quiet and fades in and out without clipping");
            QString configError;
            auto customConfig = manager.config();
            customConfig.endpoints[0].server = "rtmps://b.rtmps.youtube.com:443/live2?backup=1";
            require(manager.configure(customConfig, configError), "official YouTube backup URL is accepted");
            const auto restoredConfig = sb::loadConfig(directory.path(), configError);
            require(configError.isEmpty() &&
                        restoredConfig.endpoints[0].server == customConfig.endpoints[0].server,
                    "YouTube backup URL survives config reload");
            {
                sb::Settings reopened(manager);
                require(reopened.findChild<QComboBox *>("serverChoice0")->currentIndex() == 1 &&
                            reopened.findChild<QLineEdit *>("serverUrl0")->text() ==
                                customConfig.endpoints[0].server,
                        "server preset and URL survive settings reopen");
                reopened.findChild<QComboBox *>("serverChoice0")->setCurrentIndex(2);
                reopened.findChild<QLineEdit *>("serverUrl0")->setText("rtmps://custom.example:443/live");
                reopened.findChild<QLineEdit *>("streamKey0")->setText("synthetic-key");
                reopened.findChild<QPushButton *>("saveIngest0")->click();
                require(manager.config().endpoints[0].server == "rtmps://custom.example:443/live" &&
                            manager.secrets().get("YouTube/stream-key", configError) == "synthetic-key",
                        "immediate key save also persists the custom server");
                emit manager.account(0).ingestReady("rtmps://a.rtmps.youtube.com:443/live2",
                                                    "synthetic-new-key");
                require(
                    manager.config().endpoints[0].server == "rtmps://custom.example:443/live" &&
                        reopened.findChild<QComboBox *>("serverChoice0")->currentIndex() == 2,
                    "applying a YouTube broadcast key preserves the chosen server and its visible preset");
                reopened.findChild<QComboBox *>("serverChoice1")->setCurrentIndex(1);
                reopened.findChild<QLineEdit *>("serverUrl1")->setText("rtmps://custom.example:443/twitch");
                emit manager.account(1).ingestReady("rtmps://live.twitch.tv:443/app", "twitch-fixture-key");
                require(reopened.findChild<QComboBox *>("serverChoice1")->currentIndex() == 0 &&
                            reopened.findChild<QLineEdit *>("serverUrl1")->text() ==
                                manager.config().endpoints[1].server,
                        "Twitch account ingest keeps its visible server synchronized");
            }
            require(sb::loadConfig(directory.path(), configError).endpoints[0].server ==
                        "rtmps://custom.example:443/live",
                    "custom ingest survives closing the dialog without another Save");
            require(!sb::validateServer("rtmps://b.rtmps.youtube.com:443/live2?key=secret").isEmpty(),
                    "backup exception does not permit credentials in query strings");
            {
                QTemporaryDir restoredDirectory;
                sb::OutputManager restored(restoredDirectory.path());
                QJsonObject saved{{"client_id", "ui-fixture"},
                                  {"access_token", "fixture-only"},
                                  {"expires_at", QDateTime::currentSecsSinceEpoch() + 14400},
                                  {"broadcast_id", "remembered-id"},
                                  {"broadcast_title", "Remembered title"},
                                  {"broadcast_stream_id", "remembered-stream"}};
                require(restored.secrets().put("YouTube/oauth", QJsonDocument(saved).toJson(), configError),
                        "saved UI account fixture");
                restored.account(0).resume();
                for (int reopen = 0; reopen < 2; ++reopen) {
                    sb::Settings selection(restored);
                    auto *combo = selection.findChild<QComboBox *>("youtubeBroadcasts");
                    require(combo && combo->currentData() == "remembered-id" &&
                                combo->currentText() == "Remembered title",
                            "saved broadcast is visible after reopening settings without reloading the list");
                }
                NoNetwork api;
                sb::PlatformHub restoredHub(restored.account(0), restored.account(1), nullptr, &api, &api);
                sb::Dock restoredDock(restored, restoredHub);
                restoredDock.resize(320, 780);
                render(restoredDock, images + "/main-connected.png");
                restoredHub.shutdown();
                restored.shutdown();
            }
            hub.shutdown();
            sb::shutdownUpdates();
            manager.shutdown();
        }
        text_lookup_destroy(strings);
        strings = nullptr;
        obs_shutdown();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
