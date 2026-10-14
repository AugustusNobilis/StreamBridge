// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "http.hpp"
#include "loopback.hpp"
#include "security/secret-store.hpp"
#include <QDateTime>
#include <QJsonArray>
#include <QTimer>

namespace sb {
enum class Provider { YouTube, Twitch };
struct Broadcast {
    QString id, title, streamId;
};
struct GoogleDesktopClient {
    QString id;
    QByteArray secret;
};
GoogleDesktopClient parseGoogleDesktopClient(const QByteArray &json, QString &error);
class OAuthAccount final : public QObject {
    Q_OBJECT
  public:
    OAuthAccount(Provider provider, SecretStore &store, QObject *parent = nullptr,
                 HttpClient *transport = nullptr);
    ~OAuthAccount() override;
    void resume();
    void link(const QString &clientId, const QByteArray &desktopSecret = {});
    void cancel();
    void cancelPending();
    void disconnectAccount();
    void fetchIngest();
    void selectBroadcast(const QString &id);
    bool busy() const { return busy_; }
    bool linked() const { return !tokens_.value("access_token").toString().isEmpty(); }
    QString clientId() const { return tokens_.value("client_id").toString(clientId_); }
    QString label() const { return label_; }
    QByteArray accessToken() const;
    QString userId() const { return tokens_.value("user_id").toString(); }
    QString broadcastId() const { return tokens_.value("broadcast_id").toString(); }
    QString broadcastTitle() const { return tokens_.value("broadcast_title").toString(); }
    QString broadcastStreamId() const { return tokens_.value("broadcast_stream_id").toString(); }
    bool rememberBroadcast(const Broadcast &broadcast);
    bool hasScope(const QString &scope) const { return tokens_.value("scopes").toArray().contains(scope); }
    QString statusKey() const { return status_; }
    const QList<Broadcast> &broadcasts() const { return broadcasts_; }
  signals:
    void changed();
    void authorizationRequired(QUrl url, QString userCode);
    void ingestReady(QString server, QByteArray key);

  private:
    void setStatus(const QString &key, bool busy = false);
    void request(const QUrl &, const QUrlQuery &, bool post, bool authenticated, HttpClient::Callback);
    void fail(const QString &key);
    bool saveTokens(const QJsonObject &json);
    bool persist();
    void pollDevice();
    void validateTwitch(bool fetchKey);
    void refresh(std::function<void()> continuation);
    void withAccess(std::function<void()> continuation);
    void listBroadcasts(QString page = {});
    void armMaintenance();
    void expireSession();
    QString vaultKey() const;
    QUrl tokenUrl() const;
    Provider provider_;
    SecretStore &store_;
    HttpClient *http_;
    OAuthLoopback loopback_;
    QTimer poll_, maintenance_;
    QJsonObject tokens_;
    QString clientId_, label_, status_ = "OAuthDisconnected";
    QByteArray verifier_, deviceCode_, desktopSecret_;
    QUrl redirect_;
    QDateTime deviceExpiry_;
    int interval_ = 5, pages_ = 0;
    unsigned generation_ = 0;
    bool busy_ = false;
    QList<Broadcast> broadcasts_;
};
} // namespace sb
