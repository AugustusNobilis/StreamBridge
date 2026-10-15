// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat-settings.hpp"
#include "locale.hpp"
#include "platform-docks.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QtEndian>
#include <algorithm>
#include <cstring>
#include <map>
#include <windows.h>

// WinMM declarations require Windows types.
#include <mmsystem.h>
namespace sb {
ChatPreferences::ChatPreferences(const QString &directory)
    : settings_(directory + "/ui.ini", QSettings::IniFormat) {
}
bool ChatPreferences::enabled() const {
    return settings_.value("chat/autoConnect", true).toBool();
}
QString ChatPreferences::sound() const {
    return settings_.value("chat/soundMode", settings_.value("chat/sound", false).toBool() ? "astra" : "off")
        .toString();
}
QString ChatPreferences::customFile() const {
    return settings_.value("chat/customSound").toString();
}
int ChatPreferences::volume() const {
    return std::clamp(settings_.value("chat/volume", 100).toInt(), 0, 100);
}
void ChatPreferences::set(const QString &key, const QVariant &value) {
    settings_.setValue("chat/" + key, value);
    settings_.sync();
    emit changed();
}
std::shared_ptr<ChatPreferences> chatPreferences(const QString &directory) {
    static std::map<QString, std::weak_ptr<ChatPreferences>> instances;
    const auto key = QDir(directory).absolutePath();
    if (auto current = instances[key].lock())
        return current;
    auto result = std::make_shared<ChatPreferences>(directory);
    instances[key] = result;
    return result;
}
QByteArray chatSoundWave(const QString &path, int volume, QString &error) {
    error.clear();
    QByteArray wave;
    if (path.isEmpty())
        wave = chatNotificationWave();
    else {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
            error = "ChatSoundInvalid";
            return {};
        }
        wave = file.readAll();
    }
    const auto invalid = [&error] {
        error = "ChatSoundInvalid";
        return QByteArray();
    };
    if (wave.size() < 44 || wave.first(4) != "RIFF" || wave.mid(8, 4) != "WAVE")
        return invalid();
    QByteArray format, samples;
    for (qsizetype offset = 12; offset + 8 <= wave.size();) {
        const auto size = qFromLittleEndian<quint32>(wave.constData() + offset + 4);
        if (size > wave.size() - offset - 8)
            return invalid();
        const auto name = wave.mid(offset, 4);
        if (name == "fmt ")
            format = wave.mid(offset + 8, size);
        if (name == "data")
            samples = wave.mid(offset + 8, size);
        offset += 8 + size + (size & 1);
    }
    if (format.size() < 16 || samples.isEmpty() || qFromLittleEndian<quint16>(format.constData()) != 1)
        return invalid();
    const int channels = qFromLittleEndian<quint16>(format.constData() + 2);
    const auto rate = qFromLittleEndian<quint32>(format.constData() + 4);
    const int bits = qFromLittleEndian<quint16>(format.constData() + 14);
    const int align = channels * bits / 8;
    if ((channels != 1 && channels != 2) || (bits != 8 && bits != 16) || rate < 8000 || rate > 96000 ||
        qFromLittleEndian<quint16>(format.constData() + 12) != align ||
        qFromLittleEndian<quint32>(format.constData() + 8) != rate * align || samples.size() % align ||
        samples.size() > rate * align * 10)
        return invalid();
    const int gain = std::clamp(volume, 0, 100);
    for (qsizetype i = 0; i < samples.size(); i += bits / 8) {
        if (bits == 16) {
            const int value = qFromLittleEndian<qint16>(samples.constData() + i);
            qToLittleEndian<qint16>(static_cast<qint16>(value * gain / 100), samples.data() + i);
        } else
            samples[i] = static_cast<char>((static_cast<unsigned char>(samples[i]) - 128) * gain / 100 + 128);
    }
    QByteArray result(44, '\0');
    std::memcpy(result.data(), "RIFF", 4);
    qToLittleEndian<quint32>(36 + samples.size(), result.data() + 4);
    std::memcpy(result.data() + 8, "WAVEfmt ", 8);
    qToLittleEndian<quint32>(16, result.data() + 16);
    std::memcpy(result.data() + 20, format.constData(), 16);
    std::memcpy(result.data() + 36, "data", 4);
    qToLittleEndian<quint32>(samples.size(), result.data() + 40);
    return result + samples;
}
struct ChatSound::State {
    HWAVEOUT device = nullptr;
    WAVEHDR header{};
    QByteArray wave;
    void close() {
        if (!device)
            return;
        waveOutReset(device);
        waveOutUnprepareHeader(device, &header, sizeof(header));
        waveOutClose(device);
        device = nullptr;
        header = {};
        wave.clear();
    }
    ~State() { close(); }
};
ChatSound::ChatSound(QObject *parent) : QObject(parent), state_(std::make_unique<State>()) {
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        if (state_->header.dwFlags & WHDR_DONE)
            state_->close();
    });
    timer->start(250);
}
ChatSound::~ChatSound() = default;
bool ChatSound::play(const ChatPreferences &prefs, QString &error) {
    error.clear();
    if (prefs.sound() == "off" || prefs.volume() == 0 ||
        (state_->device && !(state_->header.dwFlags & WHDR_DONE)))
        return true;
    if (prefs.sound() == "custom" && prefs.customFile().isEmpty()) {
        error = "ChatSoundInvalid";
        return false;
    }
    state_->close();
    state_->wave =
        chatSoundWave(prefs.sound() == "custom" ? prefs.customFile() : QString(), prefs.volume(), error);
    if (state_->wave.isEmpty())
        return false;
    WAVEFORMATEX format{};
    std::memcpy(&format, state_->wave.constData() + 20, 16);
    if (waveOutOpen(&state_->device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        state_->device = nullptr;
        error = "ChatSoundDeviceError";
        return false;
    }
    state_->header.lpData = state_->wave.data() + 44;
    state_->header.dwBufferLength = static_cast<DWORD>(state_->wave.size() - 44);
    if (waveOutPrepareHeader(state_->device, &state_->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR ||
        waveOutWrite(state_->device, &state_->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        state_->close();
        error = "ChatSoundDeviceError";
        return false;
    }
    return true;
}
QWidget *createChatSettings(const QString &directory, QWidget *parent) {
    auto *panel = new QWidget(parent);
    auto *form = new QFormLayout(panel);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto prefs = chatPreferences(directory);
    auto *enabled = new QCheckBox(text("ChatAutoConnect"), panel);
    enabled->setObjectName("chatAutoConnect");
    enabled->setChecked(prefs->enabled());
    form->addRow(enabled);
    auto *mode = new QComboBox(panel);
    mode->setObjectName("chatSoundMode");
    mode->addItem(text("ChatSoundOff"), "off");
    mode->addItem(text("ChatSoundAstra"), "astra");
    mode->addItem(text("ChatSoundCustom"), "custom");
    mode->setCurrentIndex(std::max(0, mode->findData(prefs->sound())));
    form->addRow(text("ChatSoundLabel"), mode);
    auto *path = new QLineEdit(prefs->customFile(), panel);
    path->setReadOnly(true);
    auto *browse = new QPushButton(text("ChatSoundBrowse"), panel);
    form->addRow(path, browse);
    auto *volume = new QSlider(Qt::Horizontal, panel);
    volume->setObjectName("chatSoundVolume");
    volume->setRange(0, 100);
    volume->setValue(prefs->volume());
    auto *amount = new QLabel(QString::number(volume->value()) + "%", panel);
    form->addRow(text("ChatSoundVolume"), volume);
    form->addRow(QString(), amount);
    auto *preview = new QPushButton(text("ChatSoundPreview"), panel);
    form->addRow(preview);
    auto *feedback = new QLabel(panel);
    feedback->setWordWrap(true);
    feedback->setTextFormat(Qt::PlainText);
    form->addRow(feedback);
    auto *hint = new QLabel(text("ChatSettingsHint"), panel);
    hint->setWordWrap(true);
    form->addRow(hint);
    auto *player = new ChatSound(panel);
    const auto update = [=] {
        const bool custom = mode->currentData() == "custom";
        path->setVisible(custom);
        browse->setVisible(custom);
        preview->setEnabled(mode->currentData() != "off");
        volume->setEnabled(mode->currentData() != "off");
    };
    QObject::connect(enabled, &QCheckBox::toggled, panel,
                     [prefs](bool value) { prefs->set("autoConnect", value); });
    QObject::connect(mode, &QComboBox::currentIndexChanged, panel, [=] {
        prefs->set("soundMode", mode->currentData());
        update();
    });
    QObject::connect(volume, &QSlider::valueChanged, panel, [=](int value) {
        prefs->set("volume", value);
        amount->setText(QString::number(value) + "%");
    });
    QObject::connect(browse, &QPushButton::clicked, panel, [=] {
        const auto file = QFileDialog::getOpenFileName(panel, text("ChatSoundBrowse"), {}, "PCM WAV (*.wav)");
        if (file.isEmpty())
            return;
        QString error;
        if (chatSoundWave(file, 100, error).isEmpty()) {
            feedback->setText(text(error.toUtf8().constData()));
            return;
        }
        prefs->set("customSound", file);
        path->setText(file);
        feedback->clear();
    });
    QObject::connect(preview, &QPushButton::clicked, panel, [=] {
        QString error;
        player->play(*prefs, error);
        feedback->setText(error.isEmpty() ? QString() : text(error.toUtf8().constData()));
    });
    update();
    return panel;
}
} // namespace sb
