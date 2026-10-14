// SPDX-License-Identifier: GPL-2.0-or-later
#include "probe.hpp"
#include "oauth/loopback.hpp"
#include <QAuthenticator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QTimer>
namespace sb {
BrowserProbe::BrowserProbe(QObject *parent)
    : QObject(parent), browser_(this), proxy_(this), network_(this), deadline_(new QTimer(this)) {
    deadline_->setSingleShot(true);
    connect(deadline_, &QTimer::timeout, this, [this] { finish("BrowserLoadTimeout"); });
    connect(
        &browser_, &BrowserHost::status, this,
        [this](unsigned code) {
            if (code == 2)
                finish("BrowserTestPassed");
            else if (code == 3 || code == 5)
                finish("BrowserLoadFailed");
        },
        Qt::QueuedConnection);
    connect(&browser_, &BrowserHost::failed, this, &BrowserProbe::finish, Qt::QueuedConnection);
}
void BrowserProbe::finish(const QString &message) {
    if (finished_)
        return;
    finished_ = true;
    deadline_->stop();
    emit result(message);
    deleteLater();
}
void BrowserProbe::start(const QString &helper, const BrowserConfig &config, bool externalIp) {
    QString error = validateBrowser(config);
    if (!error.isEmpty()) {
        finish(error);
        return;
    }
    deadline_->start(30000);
    if (!externalIp) {
        if (!browser_.start(helper, config, error))
            finish(error);
        return;
    }
    const auto capability = randomUrlToken();
    if (config.proxyEnabled) {
        if (!proxy_.start(config.proxy, capability, error)) {
            finish(error);
            return;
        }
        network_.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", proxy_.port(), "streambridge",
                                        QString::fromLatin1(capability)));
    } else
        network_.setProxy(QNetworkProxy::NoProxy);
    // The UI names this service before the click; no automatic background request.
    QNetworkRequest request(QUrl("https://api.ipify.org?format=json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    auto *reply = network_.get(request);
    reply_ = reply;
    reply->setReadBufferSize(8193);
    auto data = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, data] {
        data->append(reply->readAll());
        if (data->size() > 8192)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, data] {
        data->append(reply->readAll());
        const auto address = QJsonDocument::fromJson(*data).object().value("ip").toString();
        QHostAddress parsed;
        const bool ok = reply->error() == QNetworkReply::NoError &&
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
                        data->size() <= 8192 && parsed.setAddress(address);
        finish(ok ? "IP: " + parsed.toString() : "BrowserIpFailed");
    });
}
} // namespace sb
