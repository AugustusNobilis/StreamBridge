// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings.hpp"
#include "account-panel.hpp"
#include "locale.hpp"
#include "updates/checker.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <cstring>

namespace sb {
static QLabel *note(const QString &value, QWidget *parent) {
    auto *label = new QLabel(value, parent);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
}
class ProfileEditor : public QWidget {
  public:
    ProfileEditor(const EncoderProfile &profile, QWidget *parent) : QWidget(parent) {
        auto *form = new QFormLayout(this);
        encoder_ = new QComboBox(this);
        const char *id = nullptr;
        for (size_t i = 0; obs_enum_encoder_types(i, &id); ++i) {
            const char *codec = obs_get_encoder_codec(id);
            if (obs_get_encoder_type(id) == OBS_ENCODER_VIDEO && codec && std::strcmp(codec, "h264") == 0 &&
                !(obs_get_encoder_caps(id) & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL)))
                encoder_->addItem(QString::fromUtf8(obs_encoder_get_display_name(id)), QString::fromUtf8(id));
        }
        encoder_->setCurrentIndex(encoder_->findData(profile.id));
        form->addRow(text("Encoder"), encoder_);
        bitrate_ = spin(form, "VideoBitrate", 100, 100000, profile.bitrate);
        width_ = spin(form, "Width", 0, 7680, profile.width);
        height_ = spin(form, "Height", 0, 4320, profile.height);
        width_->setSingleStep(2);
        height_->setSingleStep(2);
        divisor_ = new QComboBox(this);
        obs_video_info info{};
        const double fps =
            obs_get_video_info(&info) && info.fps_den ? double(info.fps_num) / info.fps_den : 0;
        for (int d = 1; d <= 6; ++d)
            divisor_->addItem(QString("÷%1 — %2 FPS").arg(d).arg(fps / d, 0, 'f', 3), d);
        divisor_->setCurrentIndex(profile.divisor - 1);
        form->addRow(text("FPSDivisor"), divisor_);
        keyframe_ = spin(form, "Keyframe", 1, 10, profile.keyframe);
        rate_ = new QComboBox(this);
        preset_ = new QComboBox(this);
        form->addRow(text("RateControl"), rate_);
        form->addRow(text("Preset"), preset_);
        audio_ = new QComboBox(this);
        for (int rate : {64, 96, 128, 160, 192, 256, 320})
            audio_->addItem(QString::number(rate), rate);
        audio_->setCurrentIndex(audio_->findData(profile.audioBitrate));
        form->addRow(text("AudioBitrate"), audio_);
        track_ = spin(form, "AudioTrack", 1, 6, profile.audioTrack);
        form->addRow(note(text("EncodingHint"), this));
        connect(encoder_, &QComboBox::currentIndexChanged, this, [this] { refreshOptions(); });
        refreshOptions();
        select(rate_, profile.rateControl);
        select(preset_, profile.preset);
    }
    EncoderProfile value() const {
        EncoderProfile p;
        p.id = encoder_->currentData().toString();
        p.bitrate = bitrate_->value();
        p.width = width_->value();
        p.height = height_->value();
        p.divisor = divisor_->currentData().toInt();
        p.keyframe = keyframe_->value();
        p.rateControl = rate_->currentData().toString();
        p.preset = preset_->currentData().toString();
        p.audioBitrate = audio_->currentData().toInt();
        p.audioTrack = track_->value();
        return p;
    }

  private:
    static void select(QComboBox *combo, const QString &value) {
        const int index = combo->findData(value);
        if (index >= 0)
            combo->setCurrentIndex(index);
    }
    QSpinBox *spin(QFormLayout *form, const char *label, int min, int max, int value) {
        auto *box = new QSpinBox(this);
        box->setRange(min, max);
        box->setValue(value);
        form->addRow(text(label), box);
        return box;
    }
    void choices(QComboBox *combo, obs_properties_t *props, const char *key) {
        combo->clear();
        combo->addItem(text("EncoderDefault"), QString());
        auto *p = key ? obs_properties_get(props, key) : nullptr;
        const bool supported = p && obs_property_get_type(p) == OBS_PROPERTY_LIST &&
                               obs_property_list_format(p) == OBS_COMBO_FORMAT_STRING;
        combo->setEnabled(supported);
        if (!supported)
            return;
        for (size_t i = 0; i < obs_property_list_item_count(p); ++i) {
            if (!obs_property_list_item_disabled(p, i))
                combo->addItem(QString::fromUtf8(obs_property_list_item_name(p, i)),
                               QString::fromUtf8(obs_property_list_item_string(p, i)));
        }
    }
    void refreshOptions() {
        auto *props = obs_get_encoder_properties(encoder_->currentData().toString().toUtf8().constData());
        if (!props) {
            bitrate_->setEnabled(false);
            keyframe_->setEnabled(false);
            return;
        }
        choices(rate_, props, "rate_control");
        choices(preset_, props, presetProperty(props));
        keyframe_->setEnabled(obs_properties_get(props, "keyint_sec") != nullptr);
        auto *bitrate = obs_properties_get(props, "bitrate");
        bitrate_->setEnabled(bitrate && obs_property_get_type(bitrate) == OBS_PROPERTY_INT);
        if (bitrate && obs_property_get_type(bitrate) == OBS_PROPERTY_INT)
            bitrate_->setRange(std::max(100, obs_property_int_min(bitrate)),
                               std::min(100000, obs_property_int_max(bitrate)));
        obs_properties_destroy(props);
    }
    QComboBox *encoder_, *divisor_, *rate_, *preset_, *audio_;
    QSpinBox *bitrate_, *width_, *height_, *keyframe_, *track_;
};
Settings::Settings(OutputManager &manager, QWidget *parent) : QDialog(parent) {
    setWindowTitle(text("Settings"));
    resize(650, 660);
    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    layout->addWidget(tabs);
    std::array<QLineEdit *, 2> servers{};
    for (size_t i = 0; i < 2; ++i) {
        auto *page = new QWidget(tabs);
        auto *form = new QFormLayout(page);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        servers[i] = new QLineEdit(manager.config().endpoints[i].server, page);
        auto *serverList = new QComboBox(page);
        if (i == 0) {
            serverList->addItem(text("YouTubePrimary"), "rtmps://a.rtmps.youtube.com:443/live2");
            serverList->addItem(text("YouTubeBackup"), "rtmps://b.rtmps.youtube.com:443/live2?backup=1");
        } else
            serverList->addItem(text("TwitchAutomatic"), "rtmps://live.twitch.tv:443/app");
        serverList->addItem(text("CustomServer"), QString());
        form->addRow(text("Server"), serverList);
        form->addRow(servers[i]);
        const auto chooseServer = [editor = servers[i], serverList] {
            const auto url = serverList->currentData().toString();
            editor->setVisible(url.isEmpty());
            if (!url.isEmpty())
                editor->setText(url);
        };
        int currentServer = serverList->findData(servers[i]->text());
        serverList->setCurrentIndex(currentServer >= 0 ? currentServer : serverList->count() - 1);
        chooseServer();
        connect(serverList, &QComboBox::currentIndexChanged, page, chooseServer);
        auto *key = new QLineEdit(page);
        key->setEchoMode(QLineEdit::Password);
        key->setPlaceholderText(text("KeyHint"));
        key->setMaxLength(8192);
        auto *keyRow = new QHBoxLayout;
        keyRow->addWidget(key);
        auto *reveal = new QPushButton(text("ShowKey"), page);
        reveal->setCheckable(true);
        keyRow->addWidget(reveal);
        form->addRow(text("StreamKey"), keyRow);
        connect(reveal, &QPushButton::toggled, page, [&, key, reveal, i](bool show) {
            if (show && key->text().isEmpty()) {
                QString error;
                auto bytes = manager.secrets().get(endpointName(i) + "/stream-key", error);
                key->setText(QString::fromUtf8(bytes));
                bytes.fill('\0');
            }
            key->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
            reveal->setText(text(show ? "HideKey" : "ShowKey"));
        });
        auto *getKey = new QPushButton(text("GetStreamKey"), page);
        form->addRow(getKey);
        connect(getKey, &QPushButton::clicked, page, [i] {
            QDesktopServices::openUrl(
                QUrl(i == 0 ? "https://studio.youtube.com/" : "https://dashboard.twitch.tv/settings/stream"));
        });
        auto *save = new QPushButton(text("SaveKey"), page);
        form->addRow(save);
        auto *remove = new QPushButton(text("DeleteKey"), page);
        form->addRow(remove);
        auto *feedback = note("", page);
        form->addRow(feedback);
        connect(save, &QPushButton::clicked, page, [&, key, feedback, i] {
            if (manager.busy()) {
                feedback->setText(text("StopFirst"));
                return;
            }
            if (key->text().isEmpty()) {
                feedback->setText(text("EnterKey"));
                return;
            }
            QString error;
            auto bytes = key->text().toUtf8();
            const bool ok = manager.secrets().put(endpointName(i) + "/stream-key", bytes, error);
            bytes.fill('\0');
            if (ok)
                key->clear();
            feedback->setText(ok ? text("KeySaved") : error);
        });
        connect(remove, &QPushButton::clicked, page, [&, feedback, key, i] {
            if (manager.busy()) {
                feedback->setText(text("StopFirst"));
                return;
            }
            QString error;
            feedback->setText(
                manager.secrets().erase(endpointName(i) + "/stream-key", error) ? text("KeyDeleted") : error);
            key->clear();
        });
        form->addRow(accountPanel(manager, i, page));
        connect(&manager.account(i), &OAuthAccount::ingestReady, page,
                [&, editor = servers[i], serverList, i](const QString &server, const QByteArray &) {
                    if (manager.config().endpoints[i].server == server) {
                        const auto found = serverList->findData(server);
                        serverList->setCurrentIndex(found >= 0 ? found : serverList->count() - 1);
                        editor->setText(server);
                    }
                });
        page->setEnabled(!manager.busy());
        auto *scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        scroll->setWidget(page);
        tabs->addTab(scroll, endpointName(i));
    }
    auto *encoding = new QWidget(tabs);
    auto *encLayout = new QVBoxLayout(encoding);
    auto *advanced = new QCheckBox(text("Advanced"), encoding);
    advanced->setChecked(manager.config().advanced);
    encLayout->addWidget(advanced);
    auto *profiles = new QTabWidget(encoding);
    encLayout->addWidget(profiles);
    std::array<ProfileEditor *, 3> editors{};
    for (size_t i = 0; i < 3; ++i) {
        editors[i] = new ProfileEditor(
            i == 0 ? manager.config().common : manager.config().endpoints[i - 1].encoder, profiles);
        auto *scroll = new QScrollArea(profiles);
        scroll->setWidgetResizable(true);
        scroll->setWidget(editors[i]);
        profiles->addTab(scroll, i == 0 ? text("Common") : endpointName(i - 1));
    }
    auto selectMode = [profiles](bool isAdvanced) {
        profiles->setTabEnabled(0, !isAdvanced);
        profiles->setTabEnabled(1, isAdvanced);
        profiles->setTabEnabled(2, isAdvanced);
        profiles->setCurrentIndex(isAdvanced ? 1 : 0);
    };
    connect(advanced, &QCheckBox::toggled, encoding, selectMode);
    selectMode(advanced->isChecked());
    encoding->setEnabled(!manager.busy());
    tabs->addTab(encoding, text("Encoding"));
    auto infoTab = [&](const char *title, const char *message) {
        auto *page = new QWidget(tabs);
        auto *v = new QVBoxLayout(page);
        v->addWidget(note(text(message), page));
        v->addStretch();
        auto *scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        scroll->setWidget(page);
        tabs->addTab(scroll, text(title));
    };
    infoTab("Help", "HelpDetails");
    auto *logsPage = new QWidget(tabs);
    auto *logsLayout = new QVBoxLayout(logsPage);
    auto *logs = new QPlainTextEdit(manager.diagnostics().join('\n'), logsPage);
    logs->setReadOnly(true);
    logsLayout->addWidget(logs);
    auto *exportLogs = new QPushButton(text("ExportLogs"), logsPage);
    logsLayout->addWidget(exportLogs);
    connect(exportLogs, &QPushButton::clicked, this, [this, logs] {
        const QString filename = QFileDialog::getSaveFileName(this, text("ExportLogs"),
                                                              "streambridge-diagnostics.txt", "Text (*.txt)");
        if (filename.isEmpty())
            return;
        QSaveFile file(filename);
        const auto bytes = logs->toPlainText().toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            QMessageBox::warning(this, "StreamBridge", text("SaveFailed"));
    });
    tabs->addTab(logsPage, text("Logs"));
    auto *updates = new QWidget(tabs);
    auto *updateLayout = new QVBoxLayout(updates);
    updateLayout->addWidget(note(text("LocalVersion").arg(STREAMBRIDGE_VERSION), updates));
    auto *latestVersion = note(text("LatestVersion").arg(text("VersionUnchecked")), updates);
    updateLayout->addWidget(latestVersion);
    updateLayout->addWidget(note(text("UpdateDisclosure"), updates));
    if (auto *checker = updateChecker()) {
        auto *repository = new QLineEdit(checker->repository(), updates);
        repository->setPlaceholderText("AugustusNobilis/StreamBridge");
        updateLayout->addWidget(note(text("UpdateRepository"), updates));
        updateLayout->addWidget(repository);
        auto *automatic = new QCheckBox(text("UpdateAutomatic"), updates);
        automatic->setChecked(checker->automatic());
        updateLayout->addWidget(automatic);
        auto *autoDownload = new QCheckBox(text("UpdateAutoDownload"), updates);
        autoDownload->setChecked(checker->autoDownload());
        updateLayout->addWidget(autoDownload);
        auto *previews = new QCheckBox(text("UpdatePreviews"), updates);
        previews->setChecked(checker->previews());
        updateLayout->addWidget(previews);
        auto *saveUpdates = new QPushButton(text("UpdateSave"), updates);
        updateLayout->addWidget(saveUpdates);
        auto *feedback = note(text(checker->state().toUtf8().constData()), updates);
        updateLayout->addWidget(feedback);
        auto *progress = new QProgressBar(updates);
        progress->setRange(0, 100);
        updateLayout->addWidget(progress);
        auto *check = new QPushButton(text("UpdateCheck"), updates);
        check->setEnabled(checker->available());
        updateLayout->addWidget(check);
        auto *open = new QPushButton(text("UpdateOpen"), updates);
        open->setEnabled(!checker->releaseUrl().isEmpty());
        updateLayout->addWidget(open);
        auto *download = new QPushButton(text("UpdateDownload"), updates);
        updateLayout->addWidget(download);
        auto *cancel = new QPushButton(text("UpdateCancel"), updates);
        updateLayout->addWidget(cancel);
        auto *install = new QPushButton(text("UpdateInstall"), updates);
        updateLayout->addWidget(install);
        const auto saveOptions = [=] {
            if (!checker->configure(repository->text(), automatic->isChecked(), autoDownload->isChecked(),
                                    previews->isChecked())) {
                feedback->setText(text("SaveFailed"));
                return false;
            }
            repository->setText(checker->repository());
            return true;
        };
        connect(saveUpdates, &QPushButton::clicked, updates, saveOptions);
        connect(check, &QPushButton::clicked, updates, [=] {
            if (saveOptions())
                checker->check();
        });
        connect(download, &QPushButton::clicked, checker, &UpdateChecker::download);
        connect(cancel, &QPushButton::clicked, checker, &UpdateChecker::cancel);
        connect(install, &QPushButton::clicked, checker, &UpdateChecker::schedule);
        connect(open, &QPushButton::clicked, updates, [checker] {
            if (!checker->releaseUrl().isEmpty())
                QDesktopServices::openUrl(checker->releaseUrl());
        });
        const auto refreshUpdate = [=] {
            latestVersion->setText(
                text("LatestVersion")
                    .arg(checker->version().isEmpty() ? text("VersionUnchecked") : checker->version()));
            QString message = text(checker->state().toUtf8().constData());
            if (!checker->version().isEmpty())
                message += "\n" + text("UpdateVersion").arg(checker->version());
            feedback->setText(message);
            open->setEnabled(!checker->releaseUrl().isEmpty());
            const bool scheduled = checker->state() == "UpdateScheduled";
            check->setEnabled(!checker->busy() && checker->readyFile().isEmpty() && !scheduled);
            saveUpdates->setEnabled(!checker->busy() && !scheduled);
            repository->setEnabled(!checker->busy() && !scheduled);
            automatic->setEnabled(!checker->busy() && !scheduled);
            autoDownload->setEnabled(!checker->busy() && !scheduled);
            previews->setEnabled(!checker->busy() && !scheduled);
            download->setEnabled(checker->canDownload());
            install->setEnabled(checker->ready());
            cancel->setEnabled(checker->busy());
            progress->setVisible(checker->state() == "UpdateDownloading");
            progress->setValue(checker->progress());
        };
        connect(checker, &UpdateChecker::changed, updates, refreshUpdate);
        refreshUpdate();
    }
    updateLayout->addStretch();
    auto *updatesScroll = new QScrollArea(tabs);
    updatesScroll->setWidgetResizable(true);
    updatesScroll->setWidget(updates);
    tabs->addTab(updatesScroll, text("Updates"));
    auto *about = new QWidget(tabs);
    auto *aboutLayout = new QVBoxLayout(about);
    aboutLayout->addWidget(note(text("AboutSummary"), about));
    auto *author = new QLabel(about);
    author->setText(text("Author") + " <a href=\"https://github.com/AugustusNobilis\">AugustusNobilis</a>");
    author->setOpenExternalLinks(true);
    aboutLayout->addWidget(author);
    aboutLayout->addWidget(note(text("AboutLicense"), about));
    auto *languages = new QComboBox(about);
    languages->addItem(text("LanguageOBS"), QString());
    languages->addItem("Русский", "ru-RU");
    languages->addItem("English", "en-US");
    languages->setCurrentIndex(languages->findData(language()));
    aboutLayout->addWidget(note(text("Language"), about));
    aboutLayout->addWidget(languages);
    auto *languageHint = note(text("LanguageRestart"), about);
    aboutLayout->addWidget(languageHint);
    connect(languages, &QComboBox::activated, about, [languages, languageHint] {
        languageHint->setText(
            text(setLanguage(languages->currentData().toString()) ? "LanguageRestart" : "SaveFailed"));
    });
    aboutLayout->addStretch();
    auto *aboutScroll = new QScrollArea(tabs);
    aboutScroll->setWidgetResizable(true);
    aboutScroll->setWidget(about);
    tabs->addTab(aboutScroll, text("About"));
    if (manager.busy())
        layout->addWidget(note(text("StopFirst"), this));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(text("SaveSettings"));
    buttons->button(QDialogButtonBox::Cancel)->setText(text("CancelSettings"));
    buttons->button(QDialogButtonBox::Save)->setEnabled(!manager.busy());
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [&, servers, advanced, editors] {
        auto config = manager.config();
        config.advanced = advanced->isChecked();
        config.common = editors[0]->value();
        for (size_t i = 0; i < 2; ++i) {
            config.endpoints[i].server = servers[i]->text().trimmed();
            config.endpoints[i].encoder = editors[i + 1]->value();
        }
        QString error;
        if (manager.configure(config, error))
            accept();
        else
            QMessageBox::warning(this, "StreamBridge", error);
    });
}
} // namespace sb
