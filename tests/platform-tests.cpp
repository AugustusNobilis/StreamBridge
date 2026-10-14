// SPDX-License-Identifier: GPL-2.0-or-later
#include "platform/hub.hpp"
#include "platform/irc.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
using namespace sb;
static void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
    std::cout << "PASS: " << message << std::endl;
}
class FakeHttp : public HttpClient {
  public:
    struct Call {
        QUrl url;
        QByteArray method, token;
        QJsonObject body;
        Callback callback;
    };
    std::deque<Call> calls;
    void send(const QUrl &url, const QUrlQuery &, const QByteArray &token, const QByteArray &, bool post,
              Callback cb) override {
        calls.push_back({url, post ? "POST" : "GET", token, {}, std::move(cb)});
    }
    void sendJson(const QUrl &url, const QByteArray &method, const QJsonObject &body, const QByteArray &token,
                  const QByteArray &, Callback cb) override {
        calls.push_back({url, method, token, body, std::move(cb)});
    }
    void cancel() override { calls.clear(); }
    Call take() {
        require(!calls.empty(), "expected request exists");
        auto call = std::move(calls.front());
        calls.pop_front();
        return call;
    }
    void answer(int status, const QJsonObject &body = {}) { take().callback({status, body, false}); }
};
static QJsonObject video(const QString &viewers = "12") {
    QJsonObject live{{"activeLiveChatId", "chat-1"}};
    if (!viewers.isEmpty())
        live["concurrentViewers"] = viewers;
    return {{"items", QJsonArray{QJsonObject{{"id", "broadcast-1"},
                                             {"snippet", QJsonObject{{"title", "Original"},
                                                                     {"description", "Original description"},
                                                                     {"categoryId", "20"},
                                                                     {"tags", QJsonArray{"keep-me"}},
                                                                     {"defaultLanguage", "ru"}}},
                                             {"liveStreamingDetails", live}}}}};
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto irc = parseIrc(
            "@id=m1;user-id=u1;display-name=User\\sOne;room-id=r1 :user!u@h PRIVMSG #owner :hello : world");
        require(irc.command == "PRIVMSG" && irc.tags["display-name"] == "User One" &&
                    irc.parameters == QStringList{"#owner", "hello : world"},
                "IRC tags, spaces and trailing colon are preserved");
        require(parseIrc("PING :tmi.twitch.tv\r\n").parameters.value(0) == "tmi.twitch.tv",
                "IRC PING payload");
        require(parseIrc("PRIVMSG #x :bad\r\nJOIN #y").command.isEmpty() &&
                    parseIrc(QByteArray(20000, 'x')).command.isEmpty(),
                "embedded line injection and oversized IRC are rejected");
        require(parseIrc("@target-msg-id=target :tmi.twitch.tv CLEARMSG #room :old").tags["target-msg-id"] ==
                    "target",
                "IRC moderation targets preserve exact message IDs");
        QTemporaryDir temp;
        SecretStore vault(temp.path());
        QString error;
        const auto scopes = QJsonArray{"https://www.googleapis.com/auth/youtube.force-ssl"};
        QJsonObject session{
            {"client_id", "client"},         {"access_token", "fixture-only"},
            {"refresh_token", "refresh"},    {"expires_at", QDateTime::currentSecsSinceEpoch() + 14400},
            {"broadcast_id", "broadcast-1"}, {"scopes", scopes}};
        require(vault.put("YouTube/oauth", QJsonDocument(session).toJson(), error),
                "isolated account fixture saved");
        FakeHttp ya, ta, yh, th;
        OAuthAccount youtube(Provider::YouTube, vault, nullptr, &ya),
            twitch(Provider::Twitch, vault, nullptr, &ta);
        youtube.resume();
        PlatformHub hub(youtube, twitch, nullptr, &yh, &th);
        QCoreApplication::processEvents();
        auto read = yh.take();
        require(read.url.host() == "www.googleapis.com" && read.token == "fixture-only" &&
                    QUrlQuery(read.url).queryItemValue("id") == "broadcast-1",
                "API is authenticated and targets the selected broadcast");
        read.callback({200, video(), false});
        require(hub.info(0).viewers == 12 && hub.info(1).viewers == -1,
                "known viewer count and unknown disconnected count differ");
        hub.refresh();
        yh.answer(200, video(""));
        require(hub.info(0).viewers == -1, "missing YouTube count is unavailable, never false zero");
        hub.saveInfo(0, "New title", "New description", "22");
        auto fresh = yh.take();
        require(fresh.method == "GET", "YouTube snippet fetched immediately before update");
        fresh.callback({200, video(), false});
        auto write = yh.take();
        const auto snippet = write.body["snippet"].toObject();
        require(write.method == "PUT" && snippet["tags"].toArray() == QJsonArray{"keep-me"} &&
                    snippet["defaultLanguage"] == "ru" && snippet["title"] == "New title" &&
                    snippet["description"] == "New description",
                "metadata update preserves unrelated tags and language");
        write.callback({200, {}, false});
        yh.answer(200, video());
        ChatMessage message{0, "message-1", "viewer-1", "Viewer", "hello", "chat-1", {}, true};
        require(hub.canModerate(message, true), "owner with scope can moderate selected chat");
        auto foreign = message;
        foreign.room = "foreign-chat";
        require(!hub.canModerate(foreign, true), "cross-broadcast moderation rejected");
        unsigned removed = 0;
        QObject::connect(&hub, &PlatformHub::messagesRemoved, [&](int, QString, QString) { ++removed; });
        hub.moderate(message, false);
        auto deletion = yh.take();
        require(deletion.method == "DELETE" && QUrlQuery(deletion.url).queryItemValue("id") == "message-1",
                "deletion targets only selected message");
        deletion.callback({403, {}, false});
        require(removed == 0, "failed moderation does not remove messages locally");
        hub.moderate(message, true);
        auto ban = yh.take();
        require(ban.method == "POST" &&
                    ban.body["snippet"].toObject()["bannedUserDetails"].toObject()["channelId"] == "viewer-1",
                "ban targets selected user with platform API");
        ban.callback({200, {}, false});
        require(removed == 1, "successful moderation updates local chat");
        unsigned messages = 0;
        QObject::connect(&hub, &PlatformHub::messageReceived, [&](const ChatMessage &) { ++messages; });
        hub.setChatEnabled(true);
        yh.answer(200, video());
        QCoreApplication::processEvents();
        auto chatRequest = yh.take();
        require(chatRequest.url.path().endsWith("/liveChat/messages"),
                "YouTube chat uses the selected active chat");
        const QJsonObject chatItem{
            {"id", "chat-message"},
            {"snippet", QJsonObject{{"type", "textMessageEvent"},
                                    {"displayMessage", "Hello"},
                                    {"publishedAt", "2026-10-14T12:00:00Z"}}},
            {"authorDetails", QJsonObject{{"channelId", "reader"}, {"displayName", "Reader"}}}};
        chatRequest.callback({200,
                              {{"items", QJsonArray{chatItem, chatItem}},
                               {"nextPageToken", "next"},
                               {"pollingIntervalMillis", 60000}},
                              false});
        require(messages == 1, "duplicate YouTube chat IDs are suppressed");
        QCoreApplication::processEvents();
        require(yh.calls.empty(), "server polling interval prevents immediate repeat");
        hub.setChatEnabled(false);
        hub.setChatEnabled(true);
        yh.answer(200, video());
        QCoreApplication::processEvents();
        auto lateChat = yh.take();
        hub.setChatEnabled(false);
        lateChat.callback(
            {200,
             {{"items", QJsonArray{QJsonObject{{"id", "late"},
                                               {"snippet", QJsonObject{{"displayMessage", "late"}}}}}}},
             false});
        require(messages == 1, "chat disabled while request is pending rejects late messages");
        QJsonObject twitchSession{
            {"client_id", "twitch-client"},
            {"access_token", "twitch-fixture"},
            {"refresh_token", "refresh"},
            {"expires_at", QDateTime::currentSecsSinceEpoch() + 14400},
            {"scopes", QJsonArray{"channel:read:stream_key", "channel:manage:broadcast",
                                  "moderator:manage:chat_messages", "moderator:manage:banned_users"}}};
        require(vault.put("Twitch/oauth", QJsonDocument(twitchSession).toJson(), error),
                "Twitch fixture saved");
        twitch.resume();
        ta.answer(200, {{"client_id", "twitch-client"},
                        {"user_id", "owner"},
                        {"login", "owner"},
                        {"scopes", twitchSession["scopes"]}});
        th.answer(200, {{"data", QJsonArray{}}});
        th.answer(200, {{"data", QJsonArray{QJsonObject{
                                     {"title", "Twitch title"}, {"game_id", "20"}, {"game_name", "Game"}}}}});
        require(hub.info(1).viewers == 0, "offline Twitch API response means known zero");
        hub.saveInfo(1, "New Twitch title", {}, "99");
        auto patch = th.take();
        require(patch.method == "PATCH" && patch.body["game_id"] == "99" &&
                    QUrlQuery(patch.url).queryItemValue("broadcaster_id") == "owner",
                "Twitch title/category update targets authorized owner");
        patch.callback({204, {}, false});
        th.answer(200, {{"data", QJsonArray{QJsonObject{{"viewer_count", 7}}}}});
        th.answer(200, {{"data", QJsonArray{QJsonObject{{"title", "New Twitch title"}, {"game_id", "99"}}}}});
        require(hub.info(1).viewers == 7, "positive Twitch viewer count parsed");
        ChatMessage twitchMessage{1, "tw-message", "tw-user", "User", "Hello", "owner", {}, true};
        auto shared = twitchMessage;
        shared.moderatable = false;
        require(hub.canModerate(twitchMessage, false) && !hub.canModerate(shared, true),
                "Twitch foreign shared-chat moderation disabled");
        hub.moderate(twitchMessage, false);
        auto twDelete = th.take();
        require(twDelete.method == "DELETE" &&
                    QUrlQuery(twDelete.url).queryItemValue("message_id") == "tw-message" &&
                    QUrlQuery(twDelete.url).queryItemValue("moderator_id") == "owner",
                "Twitch deletion includes exact message and owner moderator IDs");
        twDelete.callback({204, {}, false});
        hub.moderate(twitchMessage, true);
        auto twBan = th.take();
        require(twBan.method == "POST" && twBan.body["data"].toObject()["user_id"] == "tw-user",
                "Twitch ban targets exact selected user");
        twBan.callback({200, {}, false});
        hub.refresh();
        auto stale = yh.take();
        youtube.disconnectAccount();
        stale.callback({200, video("999"), false});
        require(hub.info(0).viewers == -1 && !hub.canModerate(message, true),
                "disconnect cancels stale responses and permissions");
        hub.shutdown();
        require(yh.calls.empty() && th.calls.empty(), "shutdown cancels queued API requests");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
