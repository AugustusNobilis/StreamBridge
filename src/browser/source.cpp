// SPDX-License-Identifier: GPL-2.0-or-later
#include "source.hpp"
#include "probe.hpp"
#include "security/secret-store.hpp"
#include "settings.hpp"
#include "ui/locale.hpp"
#include "worker.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QThread>
#include <QVBoxLayout>
#include <atomic>
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/bmem.h>
namespace sb {
namespace {
std::atomic<unsigned> sourceCount{0};
QString modulePath(bool data) {
    char *path = data ? obs_module_file("browser/streambridge-browser.exe") : obs_module_config_path("");
    const auto result = QString::fromUtf8(path ? path : "");
    bfree(path);
    return result;
}
struct Source {
    obs_source_t *obs;
    QThread thread;
    BrowserWorker *worker;
    QString key, directory, helper, state = "BrowserUnconfigured";
    QMutex mutex;
    explicit Source(obs_source_t *source)
        : obs(source), key("browser/" + QString::fromUtf8(obs_source_get_uuid(source))),
          directory(modulePath(false)), helper(modulePath(true)) {
        worker = new BrowserWorker(source, helper);
        worker->moveToThread(&thread);
        QObject::connect(worker, &BrowserWorker::changed, worker, [this](const QString &value) {
            QMutexLocker lock(&mutex);
            state = value;
        });
        QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
        thread.start();
        ++sourceCount;
        reload();
    }
    ~Source() {
        QMetaObject::invokeMethod(worker, [this] { worker->stop(); }, Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
        --sourceCount;
    }
    void reload() {
        QString error;
        auto bytes = SecretStore(directory).get(key, error);
        if (!error.isEmpty()) {
            QMutexLocker lock(&mutex);
            state = "BrowserVaultFailed";
            return;
        }
        const auto config = decodeBrowserConfig(bytes, error);
        bytes.fill('\0');
        if (!error.isEmpty()) {
            QMutexLocker lock(&mutex);
            state = "BrowserVaultFailed";
            return;
        }
        QMetaObject::invokeMethod(
            worker, [this, config] { worker->configure(config); }, Qt::QueuedConnection);
    }
    QString status() {
        QMutexLocker lock(&mutex);
        return state;
    }
};
bool configure(obs_properties_t *, obs_property_t *, void *data) {
    auto *source = static_cast<Source *>(data);
    if (!source)
        return false;
    // OBS property button callbacks run on its Qt UI thread. No source/vault secrets enter obs_data.
    QDialog dialog(static_cast<QWidget *>(obs_frontend_get_main_window()));
    dialog.setWindowTitle(text("BrowserName"));
    dialog.resize(620, 650);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    QString error;
    auto bytes = SecretStore(source->directory).get(source->key, error);
    const auto config = error.isEmpty() ? decodeBrowserConfig(bytes, error) : BrowserConfig{};
    bytes.fill('\0');
    auto label = [&](const QString &value) {
        auto *l = new QLabel(value, &dialog);
        l->setWordWrap(true);
        l->setTextFormat(Qt::PlainText);
        return l;
    };
    form->addRow(label(text("BrowserPrivacy")));
    auto *preset = new QComboBox(&dialog);
    preset->addItems({text("BrowserCustom"), "DonationAlerts"});
    form->addRow(text("BrowserPreset"), preset);
    auto *url = new QLineEdit(config.url, &dialog);
    url->setEchoMode(QLineEdit::Password);
    url->setPlaceholderText("https://...");
    form->addRow(text("BrowserUrl"), url);
    auto *reveal = new QCheckBox(text("BrowserReveal"), &dialog);
    form->addRow(reveal);
    QObject::connect(reveal, &QCheckBox::toggled, &dialog, [url](bool checked) {
        url->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
    });
    auto spin = [&](const char *name, int low, int high, int value) {
        auto *s = new QSpinBox(&dialog);
        s->setRange(low, high);
        s->setValue(value);
        form->addRow(text(name), s);
        return s;
    };
    auto *width = spin("BrowserWidth", 16, 1920, config.width),
         *height = spin("BrowserHeight", 16, 1080, config.height);
    auto *fps = spin("BrowserFps", 1, 60, config.fps);
    auto *proxy = new QCheckBox(text("BrowserProxyEnabled"), &dialog);
    proxy->setChecked(config.proxyEnabled);
    form->addRow(proxy);
    auto *protocol = new QComboBox(&dialog);
    protocol->addItems({"HTTP CONNECT", "HTTPS CONNECT", "SOCKS5 (remote DNS, TCP)"});
    protocol->setCurrentIndex(static_cast<int>(config.proxy.protocol));
    form->addRow(text("BrowserProtocol"), protocol);
    auto *host = new QLineEdit(config.proxy.host, &dialog);
    form->addRow(text("BrowserHost"), host);
    auto *port = spin("BrowserPort", 1, 65535, config.proxy.port);
    auto *user = new QLineEdit(QString::fromUtf8(config.proxy.username), &dialog);
    form->addRow(text("BrowserUser"), user);
    auto *password = new QLineEdit(QString::fromUtf8(config.proxy.password), &dialog);
    password->setEchoMode(QLineEdit::Password);
    form->addRow(text("BrowserPassword"), password);
    auto *reconnect = new QCheckBox(text("BrowserReconnect"), &dialog);
    reconnect->setChecked(config.reconnect);
    form->addRow(reconnect);
    auto *feedback =
        label(error.isEmpty() ? text(source->status().toUtf8().constData()) : text("BrowserVaultFailed"));
    form->addRow(feedback);
    auto value = [=] {
        BrowserConfig c;
        c.url = url->text().trimmed();
        c.width = width->value();
        c.height = height->value();
        c.fps = fps->value();
        c.proxyEnabled = proxy->isChecked();
        c.reconnect = reconnect->isChecked();
        c.proxy.protocol = static_cast<ProxyProtocol>(protocol->currentIndex());
        c.proxy.host = host->text().trimmed();
        c.proxy.port = static_cast<quint16>(port->value());
        c.proxy.username = user->text().toUtf8();
        c.proxy.password = password->text().toUtf8();
        return c;
    };
    QObject::connect(preset, &QComboBox::currentIndexChanged, &dialog, [=](int index) {
        if (index == 1) {
            width->setValue(800);
            height->setValue(600);
            fps->setValue(30);
            proxy->setChecked(true);
            url->setPlaceholderText(text("DonationAlertsHint"));
            feedback->setText(text("DonationAlertsHint"));
        }
    });
    auto *test = new QPushButton(text("BrowserTest"), &dialog),
         *ip = new QPushButton(text("BrowserIp"), &dialog);
    form->addRow(test, ip);
    form->addRow(label(text("BrowserIpDisclosure")));
    auto probe = [&, value](bool externalIp) {
        auto c = value();
        if (externalIp)
            c.url = "https://api.ipify.org?format=json";
        const auto invalid = validateBrowser(c);
        if (!invalid.isEmpty()) {
            feedback->setText(text(invalid.toUtf8().constData()));
            return;
        }
        test->setEnabled(false);
        ip->setEnabled(false);
        feedback->setText(text("BrowserConnecting"));
        auto *job = new BrowserProbe;
        job->moveToThread(&source->thread);
        QObject::connect(&dialog, &QObject::destroyed, job, &QObject::deleteLater);
        QObject::connect(&source->thread, &QThread::finished, job, &QObject::deleteLater);
        QObject::connect(job, &BrowserProbe::result, &dialog, [=](const QString &result) {
            feedback->setText(result.startsWith("IP: ") ? result : text(result.toUtf8().constData()));
            test->setEnabled(true);
            ip->setEnabled(true);
        });
        QMetaObject::invokeMethod(
            job, [job, helper = source->helper, c, externalIp] { job->start(helper, c, externalIp); },
            Qt::QueuedConnection);
    };
    QObject::connect(test, &QPushButton::clicked, &dialog, [&] { probe(false); });
    QObject::connect(ip, &QPushButton::clicked, &dialog, [&] { probe(true); });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto c = value();
        QString invalid = validateBrowser(c);
        if (!invalid.isEmpty()) {
            feedback->setText(text(invalid.toUtf8().constData()));
            return;
        }
        auto encoded = encodeBrowserConfig(c);
        const bool saved = SecretStore(source->directory).put(source->key, encoded, invalid);
        encoded.fill('\0');
        if (!saved) {
            feedback->setText(text("BrowserVaultFailed"));
            return;
        }
        source->reload();
        dialog.accept();
    });
    dialog.exec();
    return false;
}
bool refresh(obs_properties_t *, obs_property_t *, void *data) {
    if (auto *s = static_cast<Source *>(data))
        QMetaObject::invokeMethod(s->worker, [s] { s->worker->refresh(); }, Qt::QueuedConnection);
    return false;
}
const char *name(void *) {
    return obs_module_text("BrowserName");
}
void *create(obs_data_t *, obs_source_t *source) {
    obs_source_set_async_unbuffered(source, true);
    obs_source_set_async_decoupled(source, true);
    try {
        return new Source(source);
    } catch (...) {
        return nullptr;
    }
}
void destroy(void *source) {
    delete static_cast<Source *>(source);
}
obs_properties_t *properties(void *data) {
    auto *p = obs_properties_create();
    obs_properties_add_text(p, "privacy", obs_module_text("BrowserPrivacy"), OBS_TEXT_INFO);
    obs_properties_add_button(p, "configure", obs_module_text("BrowserConfigure"), configure);
    obs_properties_add_button(p, "refresh", obs_module_text("BrowserRefresh"), refresh);
    if (auto *source = static_cast<Source *>(data)) {
        const auto status = text(source->status().toUtf8().constData()).toUtf8();
        obs_properties_add_text(p, "status", status.constData(), OBS_TEXT_INFO);
    }
    return p;
}
} // namespace
void registerBrowserSource() {
    obs_source_info info{};
    info.id = "streambridge_browser";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;
    info.get_name = name;
    info.create = create;
    info.destroy = destroy;
    info.get_properties = properties;
    obs_register_source(&info);
}
QString browserSummary() {
    return text("BrowserSummary").arg(sourceCount.load());
}
} // namespace sb
