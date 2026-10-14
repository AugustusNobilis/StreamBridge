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
class OAuthAccount final : public QObject {
    Q_OBJECT
  public:
    OAuthAccount(Provider provider, SecretStore &store, QObject *parent = nullptr,
                 HttpClient *transport = nullptr);
    ~OAuthAccount() override;
    void resume();
    void link(const QString &clientId);
    void cancel();
    void disconnectAccount();
    void fetchIngest();
    void selectBroadcast(const QString &id);
    bool busy() const { return busy_; }
    bool linked() const { return !tokens_.value("access_token").toString().isEmpty(); }
    QString clientId() const { return tokens_.value("client_id").toString(clientId_); }
    QString label() const { return label_; }
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
    QByteArray verifier_, deviceCode_;
    QUrl redirect_;
    QDateTime deviceExpiry_;
    int interval_ = 5, pages_ = 0;
    unsigned generation_ = 0;
    bool busy_ = false;
    QList<Broadcast> broadcasts_;
};
} // namespace sb
