// SPDX-License-Identifier: GPL-2.0-or-later
#include "http.hpp"
#include <QJsonDocument>
#include <QNetworkReply>
#include <QTimer>
#include <memory>

namespace sb {
QByteArray encodeForm(const QUrlQuery &form) {
    QByteArray bytes;
    for (const auto &item : form.queryItems(QUrl::FullyDecoded)) {
        if (!bytes.isEmpty())
            bytes += '&';
        bytes += QUrl::toPercentEncoding(item.first) + '=' + QUrl::toPercentEncoding(item.second);
    }
    return bytes;
}
OAuthHttp::OAuthHttp(QObject *parent) : HttpClient(parent), network_(this) {
}
void OAuthHttp::cancel() {
    if (!reply_)
        return;
    auto *reply = reply_.data();
    reply_ = nullptr;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}
void OAuthHttp::send(const QUrl &url, const QUrlQuery &form, const QByteArray &token,
                     const QByteArray &clientId, bool post, Callback callback) {
    request(url, post ? "POST" : "GET", encodeForm(form), "application/x-www-form-urlencoded", token,
            clientId, std::move(callback));
}
void OAuthHttp::sendJson(const QUrl &url, const QByteArray &method, const QJsonObject &json,
                         const QByteArray &token, const QByteArray &clientId, Callback callback) {
    request(url, method, json.isEmpty() ? QByteArray() : QJsonDocument(json).toJson(QJsonDocument::Compact),
            "application/json", token, clientId, std::move(callback));
}
void OAuthHttp::request(const QUrl &url, const QByteArray &method, const QByteArray &payload,
                        const QByteArray &contentType, const QByteArray &token, const QByteArray &clientId,
                        Callback callback) {
    cancel();
    // Credentials never follow a redirect or go to caller-supplied servers.
    const QStringList hosts{"id.twitch.tv", "api.twitch.tv", "oauth2.googleapis.com", "www.googleapis.com"};
    if (url.scheme() != "https" || !hosts.contains(url.host()) || !url.userInfo().isEmpty() ||
        url.port(443) != 443 || url.hasFragment()) {
        QTimer::singleShot(0, this, [callback] { callback({0, {}, true}); });
        return;
    }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    request.setRawHeader("Accept", "application/json");
    if (!token.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + token);
    if (!clientId.isEmpty())
        request.setRawHeader("Client-Id", clientId);
    request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    auto *reply = network_.sendCustomRequest(request, method, payload);
    reply_ = reply;
    reply->setReadBufferSize(1024 * 1024 + 1);
    auto body = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, body] {
        body->append(reply->readAll());
        if (body->size() > 1024 * 1024)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, body, callback] {
        body->append(reply->readAll());
        HttpResult result;
        result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.transportError = result.status == 0 || body->size() > 1024 * 1024 ||
                                (reply->error() != QNetworkReply::NoError && result.status < 400);
        if (!result.transportError && !body->isEmpty()) {
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(*body, &error);
            if (error.error != QJsonParseError::NoError || !document.isObject())
                result.transportError = true;
            else
                result.json = document.object();
        }
        body->fill('\0');
        reply_ = nullptr;
        reply->deleteLater();
        callback(std::move(result));
    });
}
} // namespace sb
