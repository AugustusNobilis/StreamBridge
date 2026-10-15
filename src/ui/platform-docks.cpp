// SPDX-License-Identifier: GPL-2.0-or-later
#include "platform-docks.hpp"
#include "chat-settings.hpp"
#include "chat-view.hpp"
#include "locale.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
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
#include <QToolButton>
#include <QVBoxLayout>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <memory>
#include <windows.h>

// WinMM declarations require the Windows types above.
#include <mmsystem.h>
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
QByteArray chatNotificationWave() {
    constexpr int rate = 24000, samples = 3840;
    QByteArray wave(44 + samples * 2, '\0');
    std::memcpy(wave.data(), "RIFF", 4);
    qToLittleEndian<quint32>(wave.size() - 8, wave.data() + 4);
    std::memcpy(wave.data() + 8, "WAVEfmt ", 8);
    qToLittleEndian<quint32>(16, wave.data() + 16);
    qToLittleEndian<quint16>(1, wave.data() + 20);
    qToLittleEndian<quint16>(1, wave.data() + 22);
    qToLittleEndian<quint32>(rate, wave.data() + 24);
    qToLittleEndian<quint32>(rate * 2, wave.data() + 28);
    qToLittleEndian<quint16>(2, wave.data() + 32);
    qToLittleEndian<quint16>(16, wave.data() + 34);
    std::memcpy(wave.data() + 36, "data", 4);
    qToLittleEndian<quint32>(samples * 2, wave.data() + 40);
    constexpr double pi = 3.141592653589793;
    for (int i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / rate;
        const double envelope = std::pow(std::sin(pi * i / (samples - 1)), 2);
        const auto sample = static_cast<qint16>(
            1800 * envelope * (0.75 * std::sin(2 * pi * 660 * t) + 0.25 * std::sin(2 * pi * 880 * t)));
        qToLittleEndian<qint16>(sample, wave.data() + 44 + i * 2);
    }
    return wave;
}
QString streamDuration(qint64 seconds) {
    seconds = std::max<qint64>(0, seconds);
    return QString("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QChar('0'))
        .arg(seconds / 60 % 60, 2, 10, QChar('0'))
        .arg(seconds % 60, 2, 10, QChar('0'));
}
QWidget *createChatDock(PlatformHub &hub, const QString &directory, std::function<void()> notify) {
    auto *panel = new QWidget;
    panel->setMinimumSize(260, 200);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 6, 6, 6);
    auto preferences = chatPreferences(directory);
    auto *header = new QHBoxLayout;
    std::array<QLabel *, 2> status{label({}, panel), label({}, panel)};
    for (int i = 0; i < 2; ++i) {
        status[i]->setObjectName(QString("chatStatus%1").arg(i));
        header->addWidget(status[i]);
    }
    header->addStretch();
    auto *retry = new QToolButton(panel);
    retry->setObjectName("chatReconnect");
    retry->setToolTip(text("PlatformRefresh"));
    retry->setAccessibleName(text("PlatformRefresh"));
    retry->setAutoRaise(true);
    retry->setFixedSize(28, 28);
    QPixmap icon(24, 24);
    icon.fill(Qt::transparent);
    {
        QPainter p(&icon);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(panel->palette().color(QPalette::WindowText), 2.2, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(QRectF(4, 4, 16, 16), 30 * 16, 280 * 16);
        p.drawPolyline(QPolygonF{{15, 2}, {21, 4}, {19, 10}});
    }
    retry->setIcon(QIcon(icon));
    header->addWidget(retry);
    layout->addLayout(header);
    auto *media = new ChatMedia(panel);
    auto *list = new ChatView(*media, panel);
    list->setObjectName("chatMessages");
    layout->addWidget(list, 1);
    auto *player = new ChatSound(panel);
    auto lastSound = std::make_shared<QElapsedTimer>();
    QObject::connect(retry, &QToolButton::clicked, panel, [preferences, media, &hub] {
        media->reloadCatalogs();
        hub.setChatEnabled(false);
        hub.setChatEnabled(preferences->enabled());
        hub.refresh();
        hub.loadChatBadges();
    });
    const auto refresh = [=, &hub] {
        for (int i = 0; i < 2; ++i) {
            const auto state = hub.chatStatus(i);
            const bool online = state == "ChatConnected";
            const bool error = state == "ChatRetrying" || state == "PlatformNetwork" ||
                               state == "PlatformReconnect" || state == "PlatformPermission" ||
                               state == "PlatformRateLimit" || state == "ChatDisabledOnYouTube";
            const auto code = hub.chatError(i).isEmpty() ? QString("AUTH") : hub.chatError(i);
            status[i]->setText(QString(i == 0 ? "YouTube · " : "Twitch · ") + (online  ? "Online"
                                                                               : error ? "Error " + code
                                                                                       : "Offline"));
            status[i]->setStyleSheet(QString("color:%1;")
                                         .arg(online  ? "#83bd9a"
                                              : error ? "#df858c"
                                                      : "#969ba5"));
            status[i]->setToolTip(text(state.toUtf8().constData()) +
                                  (i == 0 && !hub.account(0).broadcastId().isEmpty()
                                       ? "\n" + text("SelectedBroadcast").arg(hub.account(0).broadcastTitle())
                                       : QString()));
        }
    };
    QObject::connect(&hub, &PlatformHub::changed, panel, refresh);
    QObject::connect(&hub, &PlatformHub::chatBadgesReady, media, &ChatMedia::setBadges);
    const auto syncMedia = [media, &hub] {
        media->setChannel(hub.chatEnabled() && hub.account(1).linked() ? hub.account(1).userId() : QString());
        hub.loadChatBadges();
    };
    QObject::connect(&hub.account(1), &OAuthAccount::changed, panel, syncMedia);
    QObject::connect(preferences.get(), &ChatPreferences::changed, panel, [=, &hub] {
        if (hub.chatEnabled() != preferences->enabled()) {
            hub.setChatEnabled(preferences->enabled());
            syncMedia();
        }
    });
    hub.setChatEnabled(preferences->enabled());
    syncMedia();
    QObject::connect(&hub, &PlatformHub::messageReceived, panel, [=](const ChatMessage &m) {
        list->append(m);
        if (preferences->sound() != "off" && preferences->volume() > 0 && m.notify &&
            (!lastSound->isValid() || lastSound->elapsed() >= 1500)) {
            lastSound->start();
            if (notify)
                notify();
            else {
                QString error;
                if (!player->play(*preferences, error))
                    list->setToolTip(text(error.toUtf8().constData()));
            }
        }
    });
    QObject::connect(list, &ChatView::authorClicked, panel, [panel, &hub](const ChatMessage &m) {
        auto *dialog = new QDialog(panel);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(m.author);
        auto *body = new QVBoxLayout(dialog);
        auto *name = label(m.author + (m.platform == 0 ? " · YouTube" : " · Twitch"), dialog);
        auto font = name->font();
        font.setBold(true);
        name->setFont(font);
        body->addWidget(name);
        QStringList roles;
        for (const auto &badge : m.badges)
            roles << chatRoleName(badge);
        if (!roles.isEmpty())
            body->addWidget(label(roles.join(", "), dialog));
        auto *follow = label(text("PlatformLoading"), dialog);
        body->addWidget(follow);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
        body->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
        QObject::connect(
            &hub, &PlatformHub::followerReady, dialog,
            [follow, m](int platform, const QString &user, const QString &state, const QDateTime &since) {
                if (platform != m.platform || user != m.userId)
                    return;
                follow->setText(state == "FollowSince"
                                    ? text("FollowSince")
                                          .arg(since.toLocalTime().toString("dd.MM.yyyy HH:mm"))
                                          .arg(since.daysTo(QDateTime::currentDateTimeUtc()))
                                    : text(state.toUtf8().constData()));
            });
        dialog->resize(380, 180);
        dialog->show();
        hub.lookupFollower(m);
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
QWidget *createYouTubeControls(PlatformHub &hub, QWidget *parent) {
    auto *panel = new QWidget(parent);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *goLive = new QPushButton(text("YouTubeGoLive"), panel);
    goLive->setObjectName("youtubeGoLive");
    auto *endLive = new QPushButton(text("YouTubeEndLive"), panel);
    endLive->setObjectName("youtubeEndLive");
    auto *controlStatus = label({}, panel);
    layout->addWidget(goLive);
    layout->addWidget(endLive);
    layout->addWidget(controlStatus);
    QObject::connect(goLive, &QPushButton::clicked, panel, [&hub] { hub.transitionYouTube(true); });
    QObject::connect(endLive, &QPushButton::clicked, panel, [panel, &hub] {
        const auto selected = hub.account(0).broadcastId();
        QMessageBox box(QMessageBox::Question, "YouTube", text("YouTubeConfirmEnd").arg(hub.info(0).title),
                        QMessageBox::Yes | QMessageBox::No, panel);
        box.setTextFormat(Qt::PlainText);
        box.button(QMessageBox::Yes)->setText(text("ConfirmYes"));
        box.button(QMessageBox::No)->setText(text("ConfirmNo"));
        box.setDefaultButton(QMessageBox::No);
        if (box.exec() == QMessageBox::Yes && selected == hub.account(0).broadcastId())
            hub.transitionYouTube(false);
    });
    const auto controls = [=, &hub] {
        panel->setVisible(hub.account(0).linked());
        const auto state = hub.info(0).broadcastState;
        const bool ready = hub.account(0).linked() && !hub.account(0).broadcastId().isEmpty() &&
                           !hub.youtubeControlPending();
        goLive->setEnabled(ready && state != "complete" && state != "live");
        endLive->setEnabled(ready && state != "complete");
        controlStatus->setText(text(hub.youtubeControlStatus().toUtf8().constData()));
    };
    QObject::connect(&hub, &PlatformHub::changed, panel, controls);
    controls();
    auto *dashboard = new QPushButton(text("OpenYouTubeStudio"), panel);
    layout->addWidget(dashboard);
    QObject::connect(dashboard, &QPushButton::clicked, panel,
                     [&hub] { QDesktopServices::openUrl(hub.youtubeStudioUrl()); });
    return panel;
}
} // namespace sb
