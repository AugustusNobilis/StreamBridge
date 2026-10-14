// SPDX-License-Identifier: GPL-2.0-or-later
// Production widgets rendered with synthetic data and no platform network access.
#include "ui/dock.hpp"
#include "ui/platform-docks.hpp"
#include "ui/settings.hpp"
#include "updates/checker.hpp"
#include <QApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPalette>
#include <QTabWidget>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#include <util/text-lookup.h>
static lookup_t *strings = nullptr;
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
                auto *chat = sb::createChatDock(hub);
                chat->setFixedWidth(320);
                row->addWidget(chat);
                auto *info = sb::createInfoDock(hub, directory.path());
                info->setFixedWidth(340);
                row->addWidget(info);
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
                canvas.resize(1000, 730);
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
                for (int tab : {0, 1, 3, 5, 6}) {
                    tabs->setCurrentIndex(tab);
                    render(settings, images + "/settings-" + locale + "-" + QString::number(tab) + ".png");
                }
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
