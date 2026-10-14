// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "oauth/account.hpp"
#include <QQueue>
#include <QSet>
#include <QSslSocket>
#include <array>
#include <deque>
namespace sb {
struct ChatMessage {
    int platform = 0;
    QString id, userId, author, text, room;
    QDateTime time;
    bool moderatable = true;
};
struct ChannelInfo {
    qint64 viewers = -1;
    QString title, categoryId, category, chatId, broadcastState;
    // YouTube omits this field for zero viewers or a hidden count. Never present it as measured.
    bool viewersUnreported = false;
    bool loaded = false;
};
struct Category {
    QString id, name;
};
class PlatformHub : public QObject {
    Q_OBJECT
  public:
    PlatformHub(OAuthAccount &youtube, OAuthAccount &twitch, QObject *parent = nullptr,
                HttpClient *youtubeHttp = nullptr, HttpClient *twitchHttp = nullptr);
    ~PlatformHub() override;
    const ChannelInfo &info(int platform) const { return info_.at(platform); }
    OAuthAccount &account(int platform) const { return *accounts_.at(platform); }
    QString status(int platform) const { return status_.at(platform); }
    QString chatStatus(int platform) const { return chatStatus_.at(platform); }
    bool pending(int platform) const { return working_.at(platform) || !queue_.at(platform).empty(); }
    void refresh();
    void setChatEnabled(bool enabled);
    void searchCategories(int platform, const QString &query, const QString &region,
                          const QString &language = "en_US");
    void saveInfo(int platform, const QString &title, const QString &category);
    void transitionYouTube(bool live);
    bool youtubeControlPending() const { return youtubeControlPending_; }
    QString youtubeControlStatus() const { return youtubeControlStatus_; }
    QUrl youtubeStudioUrl() const;
    bool canModerate(const ChatMessage &, bool ban) const;
    void moderate(const ChatMessage &, bool ban);
    void shutdown();
  signals:
    void changed();
    void messageReceived(sb::ChatMessage message);
    void messagesRemoved(int platform, QString messageId, QString userId);
    void categoriesReady(int platform, QList<sb::Category> categories);
    void saved(int platform);
    void youtubeBroadcastEnded();

  private:
    using Callback = HttpClient::Callback;
    struct Job {
        QUrl url;
        QByteArray method;
        QJsonObject body;
        Callback callback;
    };
    void request(int platform, const QString &path, const QUrlQuery &, const QByteArray &method,
                 const QJsonObject &, Callback);
    void next(int platform);
    void syncAccounts();
    void pollInfo(int platform);
    void sendYouTubeTransition(const QString &id, const QString &target);
    void pollYouTubeTransition(const QString &id, const QString &target, unsigned remaining = 30);
    void completeYouTubeTransition(const QString &id, const QString &target);
    void finishYouTubeControl(const QString &status);
    void pollYouTubeChat();
    void ensureIrc();
    void readIrc();
    void deliver(ChatMessage message);
    bool result(int platform, const HttpResult &);
    std::array<OAuthAccount *, 2> accounts_;
    std::array<HttpClient *, 2> http_;
    std::array<ChannelInfo, 2> info_;
    std::array<QString, 2> identity_, status_;
    std::array<QString, 2> chatStatus_{"ChatDisabled", "ChatDisabled"};
    std::array<std::deque<Job>, 2> queue_;
    std::array<bool, 2> working_{};
    std::array<bool, 2> infoPending_{};
    std::array<unsigned, 2> generation_{};
    QTimer infoTimer_, youtubeTimer_, ircRetry_, ircWatchdog_;
    QSslSocket irc_;
    QByteArray ircBuffer_, ircToken_;
    QString pageToken_;
    QSet<QString> seen_;
    QQueue<QString> seenOrder_;
    bool chatEnabled_ = false, stopped_ = false, ircDenied_ = false, youtubeBlocked_ = false;
    bool youtubePending_ = false;
    bool youtubeControlPending_ = false;
    QString youtubeControlStatus_ = "YouTubeControlHint";
    unsigned chatGeneration_ = 0;
    int retrySeconds_ = 2;
};
} // namespace sb
