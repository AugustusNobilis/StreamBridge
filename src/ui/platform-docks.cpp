// SPDX-License-Identifier: GPL-2.0-or-later
#include "platform-docks.hpp"
#include "locale.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
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
#include <QVBoxLayout>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <memory>
#include <windows.h>

// WinMM declarations require the Windows types above.
#include <mmsystem.h>
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
            900 * envelope * (0.75 * std::sin(2 * pi * 660 * t) + 0.25 * std::sin(2 * pi * 880 * t)));
        qToLittleEndian<qint16>(sample, wave.data() + 44 + i * 2);
    }
    return wave;
}
QWidget *createChatDock(PlatformHub &hub, const QString &directory, std::function<void()> notify) {
    auto *panel = new QWidget;
    panel->setMinimumSize(260, 200);
    auto *layout = new QVBoxLayout(panel);
    auto *enabled = new QCheckBox(text("ChatEnable"), panel);
    enabled->setToolTip(text("ChatPrivacy"));
    layout->addWidget(enabled);
    auto preferences = std::make_shared<QSettings>(directory + "/ui.ini", QSettings::IniFormat);
    auto *sound = new QCheckBox(text("ChatSound"), panel);
    sound->setObjectName("chatSound");
    sound->setToolTip(text("ChatSoundHint"));
    sound->setChecked(preferences->value("chat/sound", false).toBool());
    layout->addWidget(sound);
    QObject::connect(sound, &QCheckBox::toggled, panel, [preferences](bool checked) {
        preferences->setValue("chat/sound", checked);
        preferences->sync();
    });
    if (!notify)
        notify = [] {
            static const auto wave = chatNotificationWave();
            PlaySoundA(wave.constData(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT | SND_NOSTOP);
        };
    auto lastSound = std::make_shared<QElapsedTimer>();
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
        const auto title = hub.account(0).broadcastTitle();
        status->setText("YouTube · " + text(hub.chatStatus(0).toUtf8().constData()) +
                        (title.isEmpty() ? QString() : "\n" + text("SelectedBroadcast").arg(title)) +
                        "\nTwitch · " + text(hub.chatStatus(1).toUtf8().constData()));
    };
    QObject::connect(&hub, &PlatformHub::changed, panel, refresh);
    QObject::connect(
        &hub, &PlatformHub::messageReceived, panel, [list, sound, notify, lastSound](const ChatMessage &m) {
            const bool bottom =
                list->verticalScrollBar()->value() >= list->verticalScrollBar()->maximum() - 4;
            auto *item = new QListWidgetItem(
                platformIcon(m.platform),
                m.time.toLocalTime().toString("HH:mm") + "  " + m.author + "\n" + m.text, list);
            item->setData(Qt::UserRole, QVariant::fromValue(m));
            item->setToolTip(m.platform == 0 ? "YouTube" : "Twitch");
            item->setBackground(m.platform == 0 ? QColor(214, 91, 100, 32) : QColor(164, 122, 221, 34));
            while (list->count() > 500)
                delete list->takeItem(0);
            if (bottom)
                list->scrollToBottom();
            if (sound->isChecked() && m.notify && (!lastSound->isValid() || lastSound->elapsed() >= 1500)) {
                lastSound->start();
                notify();
            }
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
