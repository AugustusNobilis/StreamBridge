// SPDX-License-Identifier: GPL-2.0-or-later
#include "hub.hpp"
#include "irc.hpp"
#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>
namespace sb {
static const QString ytScope = "https://www.googleapis.com/auth/youtube.force-ssl";
static QUrlQuery query(std::initializer_list<std::pair<QString, QString>> items) {
    QUrlQuery q;
    for (const auto &item : items)
        q.addQueryItem(item.first, item.second);
    return q;
}
PlatformHub::PlatformHub(OAuthAccount &youtube, OAuthAccount &twitch, QObject *parent,
                         HttpClient *youtubeHttp, HttpClient *twitchHttp)
    : QObject(parent), accounts_{&youtube, &twitch},
      http_{youtubeHttp ? youtubeHttp : new OAuthHttp(this), twitchHttp ? twitchHttp : new OAuthHttp(this)},
      infoTimer_(this), youtubeTimer_(this), ircRetry_(this), ircWatchdog_(this), irc_(this) {
    status_.fill("PlatformConnect");
    youtubeTimer_.setSingleShot(true);
    ircRetry_.setSingleShot(true);
    ircWatchdog_.setSingleShot(true);
    connect(&youtubeTimer_, &QTimer::timeout, this, &PlatformHub::pollYouTubeChat);
    connect(&infoTimer_, &QTimer::timeout, this, [this] {
        for (int i = 0; i < 2; ++i)
            if (!pending(i))
                pollInfo(i);
        ensureIrc();
    });
    connect(&ircRetry_, &QTimer::timeout, this, &PlatformHub::ensureIrc);
    connect(&ircWatchdog_, &QTimer::timeout, this, [this] { irc_.abort(); });
    connect(&irc_, &QSslSocket::encrypted, this, [this] {
        const auto login = account(1).label().toUtf8();
        if (!QRegularExpression("^[a-zA-Z0-9_]{1,25}$").match(QString::fromUtf8(login)).hasMatch()) {
            ircDenied_ = true;
            irc_.abort();
            return;
        }
        auto token = account(1).accessToken();
        if (token.isEmpty() || token.contains('\r') || token.contains('\n')) {
            irc_.abort();
            return;
        }
        ircToken_ = token;
        irc_.write("CAP REQ :twitch.tv/tags twitch.tv/commands\r\nPASS oauth:" + token + "\r\nNICK " + login +
                   "\r\nJOIN #" + login + "\r\n");
        token.fill('\0');
        ircWatchdog_.start(360000);
    });
    connect(&irc_, &QSslSocket::readyRead, this, &PlatformHub::readIrc);
    connect(&irc_, &QSslSocket::disconnected, this, [this] {
        ircWatchdog_.stop();
        ircBuffer_.clear();
        ircToken_.fill('\0');
        ircToken_.clear();
        if (chatEnabled_ && account(1).linked() && !stopped_ && !ircDenied_) {
            chatStatus_[1] = "ChatReconnecting";
            emit changed();
            ircRetry_.start(retrySeconds_ * 1000);
            retrySeconds_ = std::min(120, retrySeconds_ * 2);
        }
    });
    connect(&irc_, &QSslSocket::errorOccurred, this, [this] {
        if (chatEnabled_ && !stopped_ && !ircDenied_ && !ircRetry_.isActive()) {
            chatStatus_[1] = "PlatformNetwork";
            emit changed();
            ircRetry_.start(retrySeconds_ * 1000);
            retrySeconds_ = std::min(120, retrySeconds_ * 2);
        }
    });
    for (auto *a : accounts_)
        connect(a, &OAuthAccount::changed, this, &PlatformHub::syncAccounts);
    infoTimer_.start(60000);
    QTimer::singleShot(0, this, &PlatformHub::syncAccounts);
}
PlatformHub::~PlatformHub() {
    shutdown();
}
void PlatformHub::shutdown() {
    stopped_ = true;
    chatEnabled_ = false;
    infoTimer_.stop();
    youtubeTimer_.stop();
    ircRetry_.stop();
    ircWatchdog_.stop();
    irc_.abort();
    for (int i = 0; i < 2; ++i) {
        ++generation_[i];
        http_[i]->cancel();
        queue_[i].clear();
        working_[i] = false;
    }
}
void PlatformHub::syncAccounts() {
    if (stopped_)
        return;
    for (int i = 0; i < 2; ++i) {
        const auto &a = account(i);
        const auto id = a.linked() ? a.clientId() + "/" + a.userId() + "/" + a.broadcastId() : QString();
        if (identity_[i] == id)
            continue;
        identity_[i] = id;
        ++generation_[i];
        http_[i]->cancel();
        queue_[i].clear();
        working_[i] = false;
        info_[i] = {};
        emit messagesRemoved(i, {}, {});
        status_[i] = a.linked() ? "PlatformLoading" : "PlatformConnect";
        chatStatus_[i] = chatEnabled_ ? status_[i] : "ChatDisabled";
        if (i == 0) {
            ++chatGeneration_;
            youtubePending_ = false;
            pageToken_.clear();
            youtubeTimer_.stop();
            youtubeBlocked_ = false;
        } else {
            ircRetry_.stop();
            irc_.abort();
            ircDenied_ = false;
            if (!a.linked())
                chatStatus_[i] = chatEnabled_ ? "PlatformConnect" : "ChatDisabled";
        }
        pollInfo(i);
    }
    ensureIrc();
    emit changed();
}
void PlatformHub::request(int p, const QString &path, const QUrlQuery &q, const QByteArray &method,
                          const QJsonObject &body, Callback callback) {
    if (stopped_ || queue_[p].size() >= 12 || account(p).accessToken().isEmpty()) {
        callback({0, {}, true});
        return;
    }
    QUrl url((p == 0 ? "https://www.googleapis.com/youtube/v3/" : "https://api.twitch.tv/helix/") + path);
    url.setQuery(q);
    queue_[p].push_back({url, method, body, std::move(callback)});
    next(p);
}
void PlatformHub::next(int p) {
    if (stopped_ || working_[p] || queue_[p].empty())
        return;
    auto job = std::move(queue_[p].front());
    queue_[p].pop_front();
    if (account(p).accessToken().isEmpty()) {
        job.callback({401, {}, false});
        next(p);
        return;
    }
    working_[p] = true;
    const auto generation = generation_[p];
    http_[p]->sendJson(job.url, job.method, job.body, account(p).accessToken(),
                       p == 1 ? account(p).clientId().toUtf8() : QByteArray(),
                       [this, p, generation, callback = std::move(job.callback)](HttpResult response) {
                           if (generation != generation_[p] || stopped_)
                               return;
                           working_[p] = false;
                           callback(std::move(response));
                           next(p);
                           emit changed();
                       });
}
bool PlatformHub::result(int p, const HttpResult &r) {
    status_[p] = r.ok()            ? "PlatformReady"
                 : r.status == 401 ? "PlatformReconnect"
                 : r.status == 403 ? "PlatformPermission"
                 : r.status == 429 ? "PlatformRateLimit"
                                   : "PlatformNetwork";
    emit changed();
    return r.ok();
}
void PlatformHub::refresh() {
    if (stopped_)
        return;
    ircDenied_ = false;
    youtubeBlocked_ = false;
    if (chatEnabled_) {
        chatStatus_[0] = !account(0).linked()        ? "PlatformConnect"
                         : info_[0].chatId.isEmpty() ? "ChatNoLiveChat"
                                                     : "PlatformLoading";
        if (!account(1).linked())
            chatStatus_[1] = "PlatformConnect";
        else if (irc_.state() == QAbstractSocket::UnconnectedState)
            chatStatus_[1] = "PlatformLoading";
    }
    for (int i = 0; i < 2; ++i)
        if (!pending(i))
            pollInfo(i);
    if (chatEnabled_ && !youtubeTimer_.isActive())
        youtubeTimer_.start(0);
    ensureIrc();
}
void PlatformHub::pollInfo(int p) {
    if (!account(p).linked()) {
        status_[p] = "PlatformConnect";
        emit changed();
        return;
    }
    if (account(p).accessToken().isEmpty()) {
        info_[p].viewers = -1;
        status_[p] = "PlatformReconnect";
        emit changed();
        return;
    }
    if (p == 0) {
        const auto id = account(0).broadcastId();
        if (id.isEmpty()) {
            status_[0] = "PlatformSelectBroadcast";
            emit changed();
            return;
        }
        request(0, "videos", query({{"part", "snippet,liveStreamingDetails"}, {"id", id}}), "GET", {},
                [this](HttpResult r) {
                    if (!result(0, r)) {
                        info_[0].viewers = -1;
                        return;
                    }
                    const auto items = r.json["items"].toArray();
                    if (items.isEmpty()) {
                        info_[0] = {};
                        status_[0] = "PlatformSelectBroadcast";
                        return;
                    }
                    const auto video = items[0].toObject(), snippet = video["snippet"].toObject();
                    const auto live = video["liveStreamingDetails"].toObject();
                    auto &info = info_[0];
                    info.title = snippet["title"].toString();
                    info.description = snippet["description"].toString();
                    info.categoryId = snippet["categoryId"].toString();
                    info.loaded = true;
                    bool valid = false;
                    info.viewers = live["concurrentViewers"].toString().toLongLong(&valid);
                    if (!valid || info.viewers < 0)
                        info.viewers = -1;
                    const auto chat = live["activeLiveChatId"].toString();
                    if (info.chatId != chat) {
                        ++chatGeneration_;
                        youtubePending_ = false;
                        info.chatId = chat;
                        pageToken_.clear();
                        youtubeTimer_.stop();
                    }
                    if (chatEnabled_ && chat.isEmpty())
                        chatStatus_[0] = "ChatNoLiveChat";
                    if (chatEnabled_ && !youtubeBlocked_ && !youtubePending_ && !chat.isEmpty() &&
                        !youtubeTimer_.isActive())
                        youtubeTimer_.start(0);
                });
    } else {
        const auto user = account(1).userId();
        if (user.isEmpty())
            return;
        request(1, "streams", query({{"user_id", user}}), "GET", {}, [this](HttpResult r) {
            if (!result(1, r)) {
                info_[1].viewers = -1;
                return;
            }
            const auto data = r.json["data"].toArray();
            info_[1].viewers = data.isEmpty() ? 0 : data[0].toObject()["viewer_count"].toInteger(-1);
        });
        request(1, "channels", query({{"broadcaster_id", user}}), "GET", {}, [this](HttpResult r) {
            if (!result(1, r))
                return;
            const auto data = r.json["data"].toArray();
            if (data.isEmpty())
                return;
            const auto c = data[0].toObject();
            info_[1].title = c["title"].toString();
            info_[1].categoryId = c["game_id"].toString();
            info_[1].category = c["game_name"].toString();
            info_[1].loaded = true;
        });
    }
}
void PlatformHub::setChatEnabled(bool enabled) {
    if (stopped_ || chatEnabled_ == enabled)
        return;
    chatEnabled_ = enabled;
    ++chatGeneration_;
    youtubePending_ = false;
    if (enabled) {
        ircDenied_ = false;
        refresh();
    } else {
        chatStatus_.fill("ChatDisabled");
        youtubeTimer_.stop();
        ircRetry_.stop();
        irc_.abort();
    }
    emit changed();
}
void PlatformHub::pollYouTubeChat() {
    if (!chatEnabled_ || stopped_ || youtubeBlocked_ || youtubePending_ || info_[0].chatId.isEmpty())
        return;
    auto q =
        query({{"part", "snippet,authorDetails"}, {"liveChatId", info_[0].chatId}, {"maxResults", "200"}});
    if (!pageToken_.isEmpty())
        q.addQueryItem("pageToken", pageToken_);
    youtubePending_ = true;
    const auto generation = chatGeneration_;
    request(0, "liveChat/messages", q, "GET", {}, [this, generation](HttpResult r) {
        if (generation != chatGeneration_)
            return;
        youtubePending_ = false;
        if (!chatEnabled_)
            return;
        if (!result(0, r)) {
            youtubeBlocked_ = true;
            chatStatus_[0] = status_[0];
            return;
        }
        chatStatus_[0] = "ChatConnected";
        pageToken_ = r.json["nextPageToken"].toString();
        for (const auto &value : r.json["items"].toArray()) {
            const auto item = value.toObject(), s = item["snippet"].toObject(),
                       a = item["authorDetails"].toObject();
            const auto type = s["type"].toString();
            if (type == "messageDeletedEvent") {
                const auto id = s["messageDeletedDetails"].toObject()["deletedMessageId"].toString();
                if (!id.isEmpty())
                    emit messagesRemoved(0, id, {});
            } else if (type == "userBannedEvent") {
                const auto user =
                    s["userBannedDetails"].toObject()["bannedUserDetails"].toObject()["channelId"].toString();
                if (!user.isEmpty())
                    emit messagesRemoved(0, {}, user);
            } else if (!s["displayMessage"].toString().isEmpty()) {
                deliver({0, item["id"].toString(), a["channelId"].toString(), a["displayName"].toString(),
                         s["displayMessage"].toString(), info_[0].chatId,
                         QDateTime::fromString(s["publishedAt"].toString(), Qt::ISODate),
                         !a["isChatOwner"].toBool() && !a["isChatModerator"].toBool()});
            }
        }
        const auto interval = std::max<qint64>(5000, r.json["pollingIntervalMillis"].toInteger(10000));
        youtubeTimer_.start(static_cast<int>(std::min<qint64>(interval, 3600000)));
    });
}
void PlatformHub::ensureIrc() {
    if (!chatEnabled_ || stopped_ || ircDenied_ || ircRetry_.isActive() ||
        account(1).accessToken().isEmpty() || account(1).label().isEmpty())
        return;
    if (!account(1).hasScope("chat:read")) {
        chatStatus_[1] = "PlatformReconnect";
        emit changed();
        return;
    }
    if (irc_.state() != QAbstractSocket::UnconnectedState) {
        if (!ircToken_.isEmpty() && ircToken_ != account(1).accessToken())
            irc_.abort();
        return;
    }
    ircBuffer_.clear();
    irc_.connectToHostEncrypted("irc.chat.twitch.tv", 6697);
    ircWatchdog_.start(30000);
}
void PlatformHub::readIrc() {
    ircBuffer_ += irc_.readAll();
    if (ircBuffer_.size() > 65536) {
        irc_.abort();
        return;
    }
    ircWatchdog_.start(360000);
    qsizetype end;
    while ((end = ircBuffer_.indexOf("\r\n")) >= 0) {
        auto line = parseIrc(ircBuffer_.left(end));
        ircBuffer_.remove(0, end + 2);
        if (line.command == "PING")
            irc_.write("PONG :" + line.parameters.value(0).toUtf8() + "\r\n");
        else if (line.command == "001") {
            retrySeconds_ = 2;
            chatStatus_[1] = "ChatConnected";
            emit changed();
        } else if (line.command == "RECONNECT") {
            irc_.abort();
            return;
        } else if (line.command == "NOTICE" &&
                   line.parameters.join(' ').contains("auth", Qt::CaseInsensitive)) {
            ircDenied_ = true;
            chatStatus_[1] = "PlatformReconnect";
            irc_.abort();
            emit changed();
            return;
        } else if (line.command == "PRIVMSG" && line.parameters.size() == 2) {
            const auto room = line.tags.value("room-id");
            if (room != account(1).userId())
                continue;
            const auto source = line.tags.value("source-room-id");
            deliver({1, line.tags.value("id"), line.tags.value("user-id"),
                     line.tags.value("display-name", line.prefix.section('!', 0, 0)), line.parameters[1],
                     room, QDateTime::fromMSecsSinceEpoch(line.tags.value("tmi-sent-ts").toLongLong()),
                     (source.isEmpty() || source == room) && line.tags.value("mod") != "1" &&
                         line.tags.value("user-id") != room});
        } else if (line.command == "CLEARMSG" && !line.tags.value("target-msg-id").isEmpty() &&
                   line.parameters.value(0) == "#" + account(1).label())
            emit messagesRemoved(1, line.tags.value("target-msg-id"), {});
        else if (line.command == "CLEARCHAT" && line.parameters.value(0) == "#" + account(1).label())
            emit messagesRemoved(1, {}, line.tags.value("target-user-id"));
    }
}
void PlatformHub::deliver(ChatMessage message) {
    if (message.id.isEmpty() || message.id.size() > 1024 || message.text.size() > 16384)
        return;
    const auto key = QString::number(message.platform) + "/" + message.id;
    if (seen_.contains(key))
        return;
    seen_.insert(key);
    seenOrder_.enqueue(key);
    while (seenOrder_.size() > 2000)
        seen_.remove(seenOrder_.dequeue());
    emit messageReceived(std::move(message));
}
void PlatformHub::searchCategories(int p, const QString &term, const QString &region) {
    const auto path = p == 0 ? "videoCategories" : "search/categories";
    const auto q = p == 0 ? query({{"part", "snippet"}, {"regionCode", region}, {"hl", "en_US"}})
                          : query({{"query", term.left(100)}, {"first", "30"}});
    request(p, path, q, "GET", {}, [this, p](HttpResult r) {
        if (!result(p, r))
            return;
        QList<Category> categories;
        for (const auto &value : r.json[p == 0 ? "items" : "data"].toArray()) {
            const auto item = value.toObject(), snippet = item["snippet"].toObject();
            if (p == 0 && !snippet["assignable"].toBool())
                continue;
            categories.append(
                {item["id"].toString(), p == 0 ? snippet["title"].toString() : item["name"].toString()});
        }
        emit categoriesReady(p, categories);
    });
}
void PlatformHub::saveInfo(int p, const QString &title, const QString &description, const QString &category) {
    if (!info_[p].loaded || title.trimmed().isEmpty() || title.size() > (p == 0 ? 100 : 140) ||
        category.isEmpty() || description.toUtf8().size() > 5000 || pending(p))
        return;
    if (!account(p).hasScope(p == 0 ? ytScope : "channel:manage:broadcast")) {
        status_[p] = "PlatformReconnect";
        emit changed();
        return;
    }
    if (p == 1) {
        request(1, "channels", query({{"broadcaster_id", account(1).userId()}}), "PATCH",
                {{"title", title}, {"game_id", category}}, [this](HttpResult r) {
                    if (result(1, r)) {
                        emit saved(1);
                        pollInfo(1);
                    }
                });
        return;
    }
    const auto id = account(0).broadcastId();
    // Fetch immediately before updating: videos.update replaces snippet fields.
    request(0, "videos", query({{"part", "snippet"}, {"id", id}}), "GET", {},
            [this, id, title, description, category](HttpResult r) {
                if (!result(0, r))
                    return;
                const auto items = r.json["items"].toArray();
                if (items.isEmpty()) {
                    status_[0] = "PlatformSelectBroadcast";
                    return;
                }
                const auto original = items[0].toObject()["snippet"].toObject();
                QJsonObject snippet{{"title", title}, {"description", description}, {"categoryId", category}};
                for (const auto *key : {"tags", "defaultLanguage", "defaultAudioLanguage"})
                    if (original.contains(key))
                        snippet[key] = original[key];
                request(0, "videos", query({{"part", "snippet"}}), "PUT", {{"id", id}, {"snippet", snippet}},
                        [this](HttpResult updated) {
                            if (result(0, updated)) {
                                emit saved(0);
                                pollInfo(0);
                            }
                        });
            });
}
bool PlatformHub::canModerate(const ChatMessage &m, bool ban) const {
    if (m.platform < 0 || m.platform > 1 || !m.moderatable || m.id.isEmpty() || (ban && m.userId.isEmpty()))
        return false;
    const auto &a = account(m.platform);
    if (a.accessToken().isEmpty())
        return false;
    return m.platform == 0
               ? m.room == info_[0].chatId && !m.room.isEmpty() && a.hasScope(ytScope)
               : m.room == a.userId() && m.userId != a.userId() &&
                     a.hasScope(ban ? "moderator:manage:banned_users" : "moderator:manage:chat_messages");
}
void PlatformHub::moderate(const ChatMessage &m, bool ban) {
    if (!canModerate(m, ban) || pending(m.platform))
        return;
    QString path;
    QUrlQuery q;
    QJsonObject body;
    if (m.platform == 0) {
        path = ban ? "liveChat/bans" : "liveChat/messages";
        q = ban ? query({{"part", "snippet"}}) : query({{"id", m.id}});
        if (ban)
            body = {{"snippet", QJsonObject{{"liveChatId", m.room},
                                            {"type", "permanent"},
                                            {"bannedUserDetails", QJsonObject{{"channelId", m.userId}}}}}};
    } else {
        path = ban ? "moderation/bans" : "moderation/chat";
        q = query({{"broadcaster_id", account(1).userId()}, {"moderator_id", account(1).userId()}});
        if (ban)
            body = {{"data", QJsonObject{{"user_id", m.userId}}}};
        else
            q.addQueryItem("message_id", m.id);
    }
    request(m.platform, path, q, ban ? "POST" : "DELETE", body, [this, m, ban](HttpResult r) {
        if (result(m.platform, r))
            emit messagesRemoved(m.platform, ban ? QString() : m.id, ban ? m.userId : QString());
    });
}
} // namespace sb
