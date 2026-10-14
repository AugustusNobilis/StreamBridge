// SPDX-License-Identifier: GPL-2.0-or-later
#include "platform-docks.hpp"
#include "locale.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>
#include <memory>
Q_DECLARE_METATYPE(sb::ChatMessage)
namespace sb {
static QLabel *label(const QString &value, QWidget *parent) {
    auto *widget = new QLabel(value, parent);
    widget->setTextFormat(Qt::PlainText);
    widget->setWordWrap(true);
    return widget;
}
QIcon platformIcon(int platform) {
    QPixmap pix(20, 20);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(platform == 0 ? QColor("#d65b64") : QColor("#a47add"));
    if (platform == 0) {
        p.drawRoundedRect(QRectF(1, 4, 18, 12), 4, 4);
        p.setBrush(Qt::white);
        p.drawPolygon(QPolygonF{{8, 7}, {8, 13}, {13, 10}});
    } else {
        p.drawPolygon(QPolygonF{{3, 2}, {18, 2}, {18, 13}, {13, 17}, {9, 17}, {6, 20}, {6, 17}, {3, 17}});
        p.setPen(QPen(Qt::white, 2));
        p.drawLine(9, 6, 9, 11);
        p.drawLine(14, 6, 14, 11);
    }
    return QIcon(pix);
}
ViewerBadge::ViewerBadge(QWidget *parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    setCount(-1);
}
QSize ViewerBadge::sizeHint() const {
    return {30 + fontMetrics().horizontalAdvance(displayCount()), 24};
}
QString ViewerBadge::displayCount() const {
    return count_ < 0 ? QString("—") : unreported_ ? QString("0*") : QString::number(count_);
}
void ViewerBadge::setCount(qint64 count, bool unreported) {
    count_ = count;
    unreported_ = unreported && count == 0;
    const auto value = count < 0     ? text("ViewersUnknown")
                       : unreported_ ? text("ViewersUnreported")
                                     : text("Viewers").arg(count);
    setToolTip(value);
    setAccessibleName(value);
    updateGeometry();
    update();
}
void ViewerBadge::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor color(count_ > 0 ? "#ce7780" : "#92969e");
    QPainterPath eye;
    eye.moveTo(1, 12);
    eye.quadTo(11, 0, 21, 12);
    eye.quadTo(11, 24, 1, 12);
    QPainterPath pupil;
    pupil.addEllipse(QRectF(7, 8, 8, 8));
    p.setPen(Qt::NoPen);
    p.fillPath(eye.subtracted(pupil), color);
    p.setBrush(color);
    p.drawEllipse(QRectF(9, 10, 4, 4));
    p.setPen(color);
    p.drawText(QRect(27, 0, width() - 27, height()), Qt::AlignVCenter, displayCount());
}
QWidget *createChatDock(PlatformHub &hub) {
    auto *panel = new QWidget;
    panel->setMinimumSize(260, 200);
    auto *layout = new QVBoxLayout(panel);
    auto *enabled = new QCheckBox(text("ChatEnable"), panel);
    enabled->setToolTip(text("ChatPrivacy"));
    layout->addWidget(enabled);
    auto *status = label({}, panel);
    layout->addWidget(status);
    auto *list = new QListWidget(panel);
    list->setWordWrap(true);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(list, 1);
    layout->addWidget(label(text("ChatModerationHint"), panel));
    auto *retry = new QPushButton(text("PlatformRefresh"), panel);
    layout->addWidget(retry);
    QObject::connect(enabled, &QCheckBox::toggled, &hub, &PlatformHub::setChatEnabled);
    QObject::connect(retry, &QPushButton::clicked, &hub, &PlatformHub::refresh);
    const auto refresh = [status, &hub] {
        status->setText("YouTube · " + text(hub.chatStatus(0).toUtf8().constData()) + "\nTwitch · " +
                        text(hub.chatStatus(1).toUtf8().constData()));
    };
    QObject::connect(&hub, &PlatformHub::changed, panel, refresh);
    QObject::connect(&hub, &PlatformHub::messageReceived, panel, [list](const ChatMessage &m) {
        const bool bottom = list->verticalScrollBar()->value() >= list->verticalScrollBar()->maximum() - 4;
        auto *item = new QListWidgetItem(
            platformIcon(m.platform),
            m.time.toLocalTime().toString("HH:mm") + "  " + m.author + "\n" + m.text, list);
        item->setData(Qt::UserRole, QVariant::fromValue(m));
        item->setToolTip(m.platform == 0 ? "YouTube" : "Twitch");
        while (list->count() > 500)
            delete list->takeItem(0);
        if (bottom)
            list->scrollToBottom();
    });
    QObject::connect(&hub, &PlatformHub::messagesRemoved, panel,
                     [list](int platform, const QString &message, const QString &user) {
                         for (int row = list->count() - 1; row >= 0; --row) {
                             const auto m = list->item(row)->data(Qt::UserRole).value<ChatMessage>();
                             if (m.platform == platform && (message.isEmpty() || m.id == message) &&
                                 (user.isEmpty() || m.userId == user))
                                 delete list->takeItem(row);
                         }
                     });
    QObject::connect(list, &QListWidget::customContextMenuRequested, panel, [list, panel, &hub](QPoint pos) {
        auto *item = list->itemAt(pos);
        if (!item)
            return;
        const auto m = item->data(Qt::UserRole).value<ChatMessage>();
        QMenu menu(panel);
        auto *copy = menu.addAction(text("CopyMessage"));
        auto *remove = menu.addAction(text("DeleteMessage"));
        auto *ban = menu.addAction(text("BanUser"));
        remove->setEnabled(hub.canModerate(m, false) && !hub.pending(m.platform));
        ban->setEnabled(hub.canModerate(m, true) && !hub.pending(m.platform));
        const auto chosen = menu.exec(list->mapToGlobal(pos));
        if (chosen == copy)
            QApplication::clipboard()->setText(m.author + ": " + m.text);
        else if (chosen && (chosen == remove || chosen == ban)) {
            const auto question = text(chosen == ban ? "ConfirmBan" : "ConfirmDelete")
                                      .arg(m.author, m.platform == 0 ? "YouTube" : "Twitch");
            QMessageBox box(QMessageBox::Question, "StreamBridge", question,
                            QMessageBox::Yes | QMessageBox::No, panel);
            box.setTextFormat(Qt::PlainText);
            box.button(QMessageBox::Yes)->setText(text("ConfirmYes"));
            box.button(QMessageBox::No)->setText(text("ConfirmNo"));
            box.setDefaultButton(QMessageBox::No);
            if (box.exec() == QMessageBox::Yes)
                hub.moderate(m, chosen == ban);
        }
    });
    refresh();
    return panel;
}
QWidget *createInfoDock(PlatformHub &hub, const QString &directory) {
    auto *tabs = new QTabWidget;
    tabs->setMinimumSize(280, 250);
    auto settings = std::make_shared<QSettings>(directory + "/ui.ini", QSettings::IniFormat);
    for (int p = 0; p < 2; ++p) {
        auto *panel = new QWidget(tabs);
        auto *form = new QFormLayout(panel);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        auto *status = label({}, panel);
        form->addRow(status);
        auto *title = new QLineEdit(panel);
        title->setMaxLength(p == 0 ? 100 : 140);
        form->addRow(text("StreamTitle"), title);
        auto *search = new QLineEdit(panel);
        search->setPlaceholderText(text(p == 0 ? "CategoryRegion" : "CategorySearch"));
        if (p == 0) {
            search->setText(settings->value("youtubeRegion", "US").toString());
            search->setMaxLength(2);
        }
        form->addRow(text(p == 0 ? "Region" : "CategorySearch"), search);
        auto *find = new QPushButton(text("LoadCategories"), panel);
        form->addRow(find);
        auto *category = new QComboBox(panel);
        form->addRow(text("Category"), category);
        auto *game = new QPushButton(text("YouTubeChooseGame"), panel);
        game->setObjectName("youtubeChooseGame");
        auto *gameHint = label(text("YouTubeGameHint"), panel);
        form->addRow(game);
        form->addRow(gameHint);
        QObject::connect(game, &QPushButton::clicked, panel,
                         [&hub] { QDesktopServices::openUrl(hub.youtubeStudioUrl()); });
        auto *save = new QPushButton(text("PublishInfo"), panel);
        form->addRow(save);
        auto *reload = new QPushButton(text("ReloadInfo"), panel);
        form->addRow(reload);
        auto dirty = std::make_shared<bool>(false);
        auto loadedIdentity = std::make_shared<QString>();
        auto fill = [=, &hub] {
            const QSignalBlocker titleSignals(title), categorySignals(category);
            const auto &info = hub.info(p);
            title->setText(info.title);
            category->clear();
            if (!info.categoryId.isEmpty())
                category->addItem(
                    info.category.isEmpty()
                        ? (p == 0 && info.categoryId == "20" ? text("YouTubeGaming") : info.categoryId)
                        : info.category,
                    info.categoryId);
            *dirty = false;
        };
        const auto refresh = [=, &hub] {
            status->setText(text(hub.status(p).toUtf8().constData()));
            const auto identity = hub.account(p).clientId() + "/" + hub.account(p).userId() + "/" +
                                  hub.account(p).broadcastId();
            if (*loadedIdentity != identity || !hub.account(p).linked()) {
                *loadedIdentity = identity;
                *dirty = false;
            }
            if (!*dirty)
                fill();
            title->setEnabled(hub.info(p).loaded);
            game->setVisible(p == 0 && category->currentData().toString() == "20");
            gameHint->setVisible(p == 0 && category->currentData().toString() == "20");
            game->setEnabled(hub.info(p).loaded && !hub.account(p).broadcastId().isEmpty());
            category->setEnabled(hub.info(p).loaded);
            save->setEnabled(hub.info(p).loaded && !hub.pending(p) && !title->text().trimmed().isEmpty() &&
                             !category->currentData().toString().isEmpty());
            find->setEnabled(hub.account(p).linked() && !hub.pending(p));
        };
        QObject::connect(&hub, &PlatformHub::changed, panel, refresh);
        QObject::connect(title, &QLineEdit::textEdited, panel, [=] {
            *dirty = true;
            refresh();
        });
        QObject::connect(category, &QComboBox::activated, panel, [=] {
            *dirty = true;
            refresh();
        });
        QObject::connect(reload, &QPushButton::clicked, panel, [=, &hub] {
            *dirty = false;
            hub.refresh();
            refresh();
        });
        QObject::connect(find, &QPushButton::clicked, panel, [=, &hub] {
            const auto region = search->text().trimmed().toUpper();
            if (p == 0 && (region.size() != 2 || !region[0].isLetter() || !region[1].isLetter()))
                return;
            if (p == 0)
                settings->setValue("youtubeRegion", region);
            hub.searchCategories(p, search->text(), region,
                                 text("SetupGuideLanguage") == "ru-RU" ? "ru_RU" : "en_US");
        });
        QObject::connect(&hub, &PlatformHub::categoriesReady, panel,
                         [=](int platform, const QList<Category> &items) {
                             if (platform != p)
                                 return;
                             const auto selected = category->currentData();
                             const auto selectedText = category->currentText();
                             *dirty = true;
                             category->clear();
                             for (const auto &item : items)
                                 category->addItem(item.name, item.id);
                             if (selected.isValid() && category->findData(selected) < 0)
                                 category->addItem(selectedText, selected);
                             if (category->findData(selected) >= 0)
                                 category->setCurrentIndex(category->findData(selected));
                             refresh();
                         });
        QObject::connect(save, &QPushButton::clicked, panel, [=, &hub] {
            hub.saveInfo(p, title->text(), category->currentData().toString());
            refresh();
        });
        QObject::connect(&hub, &PlatformHub::saved, panel, [=](int platform) {
            if (p == platform) {
                *dirty = false;
                status->setText(text("InfoSaved"));
            }
        });
        if (p == 0) {
            auto *goLive = new QPushButton(text("YouTubeGoLive"), panel);
            goLive->setObjectName("youtubeGoLive");
            auto *endLive = new QPushButton(text("YouTubeEndLive"), panel);
            endLive->setObjectName("youtubeEndLive");
            auto *controlStatus = label({}, panel);
            form->addRow(goLive);
            form->addRow(endLive);
            form->addRow(controlStatus);
            QObject::connect(goLive, &QPushButton::clicked, panel, [&hub] { hub.transitionYouTube(true); });
            QObject::connect(endLive, &QPushButton::clicked, panel, [panel, &hub] {
                const auto selected = hub.account(0).broadcastId();
                QMessageBox box(QMessageBox::Question, "YouTube",
                                text("YouTubeConfirmEnd").arg(hub.info(0).title),
                                QMessageBox::Yes | QMessageBox::No, panel);
                box.setTextFormat(Qt::PlainText);
                box.button(QMessageBox::Yes)->setText(text("ConfirmYes"));
                box.button(QMessageBox::No)->setText(text("ConfirmNo"));
                box.setDefaultButton(QMessageBox::No);
                if (box.exec() == QMessageBox::Yes && selected == hub.account(0).broadcastId())
                    hub.transitionYouTube(false);
            });
            const auto controls = [=, &hub] {
                const auto state = hub.info(0).broadcastState;
                const bool ready = hub.account(0).linked() && !hub.account(0).broadcastId().isEmpty() &&
                                   !hub.youtubeControlPending();
                goLive->setEnabled(ready && state != "complete" && state != "live");
                endLive->setEnabled(ready && state != "complete");
                controlStatus->setText(text(hub.youtubeControlStatus().toUtf8().constData()));
            };
            QObject::connect(&hub, &PlatformHub::changed, panel, controls);
            controls();
        }
        auto *dashboard = new QPushButton(text("OpenDashboard"), panel);
        form->addRow(dashboard);
        QObject::connect(dashboard, &QPushButton::clicked, panel, [p, &hub] {
            QDesktopServices::openUrl(p == 0 ? hub.youtubeStudioUrl() : QUrl("https://dashboard.twitch.tv/"));
        });
        auto *scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        scroll->setWidget(panel);
        tabs->addTab(scroll, platformIcon(p), p == 0 ? "YouTube" : "Twitch");
        refresh();
    }
    return tabs;
}
} // namespace sb
