// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QUrlQuery>
#include <functional>

namespace sb {
QByteArray encodeForm(const QUrlQuery &form);
struct HttpResult {
    int status = 0;
    QJsonObject json;
    bool transportError = false;
    bool ok() const { return !transportError && status >= 200 && status < 300; }
};
class HttpClient : public QObject {
  public:
    using Callback = std::function<void(HttpResult)>;
    using QObject::QObject;
    virtual void send(const QUrl &url, const QUrlQuery &form, const QByteArray &token,
                      const QByteArray &clientId, bool post, Callback callback) = 0;
    virtual void cancel() = 0;
    virtual void sendJson(const QUrl &, const QByteArray &, const QJsonObject &, const QByteArray &,
                          const QByteArray &, Callback callback) {
        callback({0, {}, true});
    }
};
class OAuthHttp final : public HttpClient {
  public:
    explicit OAuthHttp(QObject *parent);
    void send(const QUrl &, const QUrlQuery &, const QByteArray &, const QByteArray &, bool,
              Callback) override;
    void cancel() override;
    void sendJson(const QUrl &, const QByteArray &, const QJsonObject &, const QByteArray &,
                  const QByteArray &, Callback) override;

  private:
    void request(const QUrl &, const QByteArray &, const QByteArray &, const QByteArray &, const QByteArray &,
                 const QByteArray &, Callback);
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
};
} // namespace sb
