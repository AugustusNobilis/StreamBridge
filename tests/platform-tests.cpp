// SPDX-License-Identifier: GPL-2.0-or-later
#include "platform/hub.hpp"
#include "platform/irc.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
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
    QJsonObject live{{"activeLiveChatId", "chat-1"}, {"actualStartTime", "2026-10-14T12:00:00Z"}};
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
static QJsonObject broadcast(const QString &state, bool monitor = false) {
    return {{"items", QJsonArray{QJsonObject{
                          {"id", "broadcast-1"},
                          {"status", QJsonObject{{"lifeCycleStatus", state}}},
                          {"contentDetails",
                           QJsonObject{{"boundStreamId", "stream-1"},
                                       {"monitorStream", QJsonObject{{"enableMonitorStream", monitor}}}}}}}}};
}
static QJsonObject stream(const QString &state) {
    return {{"items",
             QJsonArray{QJsonObject{{"id", "stream-1"}, {"status", QJsonObject{{"streamStatus", state}}}}}}};
}
static QJsonObject transition(const QString &state) {
    return {{"id", "broadcast-1"}, {"status", QJsonObject{{"lifeCycleStatus", state}}}};
}
static void awaitRequest(FakeHttp &http, int milliseconds = 3500) {
    QElapsedTimer timeout;
    timeout.start();
    while (http.calls.empty() && timeout.elapsed() < milliseconds) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    require(!http.calls.empty(), "bounded transition poll arrives");
}
static void recoveryTests() {
    QTemporaryDir directory;
    SecretStore store(directory.path());
    QString error;
    const QJsonObject session{{"client_id", "recovery-client"},
                              {"access_token", "fixture-token"},
                              {"refresh_token", "fixture-refresh"},
                              {"expires_at", QDateTime::currentSecsSinceEpoch() + 14400},
                              {"scopes", QJsonArray{"https://www.googleapis.com/auth/youtube.force-ssl"}}};
    require(store.put("YouTube/oauth", QJsonDocument(session).toJson(), error) &&
                store.put("YouTube/stream-key", "unchanged-key", error),
            "recovery fixture saved");
    FakeHttp ya, ta, yh, th;
    OAuthAccount youtube(Provider::YouTube, store, nullptr, &ya),
        twitch(Provider::Twitch, store, nullptr, &ta);
    youtube.resume();
    unsigned ingestChanges = 0;
    QObject::connect(&youtube, &OAuthAccount::ingestReady, [&] { ++ingestChanges; });
    PlatformHub hub(youtube, twitch, nullptr, &yh, &th);
    QCoreApplication::processEvents();
    auto discovery = yh.take();
    require(QUrlQuery(discovery.url).queryItemValue("broadcastStatus") == "active" &&
                !QUrlQuery(discovery.url).hasQueryItem("mine"),
            "active discovery uses one supported API filter");
    auto live = broadcast("live")["items"].toArray()[0].toObject();
    live["snippet"] = QJsonObject{{"title", "Selected live"}, {"liveChatId", "canonical-chat"}};
    auto other = live;
    other["id"] = "broadcast-2";
    discovery.callback({200, {{"items", QJsonArray{live, other}}}, false});
    require(youtube.broadcastId().isEmpty() && hub.status(0) == "YouTubeMultipleLive",
            "ambiguous simultaneous broadcasts are never guessed");
    hub.refresh();
    yh.answer(200, {{"items", QJsonArray{live}}});
    require(youtube.broadcastId() == "broadcast-1" && youtube.broadcastTitle() == "Selected live" &&
                ingestChanges == 0 && store.get("YouTube/stream-key", error) == "unchanged-key",
            "unique active discovery persists monitoring without changing the stream key");
    auto withoutChat = video("42");
    auto item = withoutChat["items"].toArray()[0].toObject();
    auto details = item["liveStreamingDetails"].toObject();
    details.remove("activeLiveChatId");
    item["liveStreamingDetails"] = details;
    withoutChat["items"] = QJsonArray{item};
    yh.answer(200, withoutChat);
    auto fallback = yh.take();
    require(fallback.url.path().endsWith("/liveBroadcasts") &&
                QUrlQuery(fallback.url).queryItemValue("id") == "broadcast-1",
            "missing video chat ID falls back to authenticated broadcast metadata");
    fallback.callback({200, {{"items", QJsonArray{live}}}, false});
    require(hub.info(0).chatId == "canonical-chat" && hub.info(0).viewers == 42,
            "canonical broadcast chat and actual viewer count coexist");
    hub.setChatEnabled(true);
    yh.answer(200, withoutChat);
    yh.answer(200, {{"items", QJsonArray{live}}});
    QCoreApplication::processEvents();
    unsigned silentMessages = 0, audibleMessages = 0;
    QObject::connect(&hub, &PlatformHub::messageReceived, [&](const ChatMessage &message) {
        message.notify ? ++audibleMessages : ++silentMessages;
    });
    QJsonObject chatItem{
        {"id", "initial"},
        {"snippet", QJsonObject{{"displayMessage", "Hello"},
                                {"publishedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}}}};
    yh.answer(200, {{"items", QJsonArray{chatItem}}, {"pollingIntervalMillis", 5000}});
    require(silentMessages == 1 && audibleMessages == 0, "initial YouTube history has no notification sound");
    awaitRequest(yh, 6500);
    yh.answer(503);
    require(hub.chatStatus(0) == "ChatRetrying", "temporary chat outage schedules automatic retry");
    awaitRequest(yh, 6500);
    chatItem["id"] = "new";
    yh.answer(200, {{"items", QJsonArray{chatItem}}, {"pollingIntervalMillis", 60000}});
    require(audibleMessages == 1 && hub.chatStatus(0) == "ChatConnected",
            "chat recovers without manual reconnect and notifies only new messages");
    hub.setChatEnabled(false);
    hub.setChatEnabled(true);
    yh.answer(200, withoutChat);
    yh.answer(200, {{"items", QJsonArray{live}}});
    QCoreApplication::processEvents();
    yh.answer(404,
              {{"error", QJsonObject{{"errors", QJsonArray{QJsonObject{{"reason", "liveChatNotFound"}}}}}}});
    require(hub.chatStatus(0) == "ChatWaitingForBroadcast", "stale chat ID triggers broadcast lookup");
    hub.refresh();
    yh.answer(200, video("43"));
    live["snippet"] = QJsonObject{{"title", "Selected live"}, {"liveChatId", "replacement-chat"}};
    yh.answer(200, {{"items", QJsonArray{live}}});
    require(hub.info(0).chatId == "replacement-chat" && hub.info(0).viewers == 43,
            "stale video chat ID is replaced by broadcast metadata while viewers keep refreshing");
    hub.setChatEnabled(false);
    auto completed = video();
    item = completed["items"].toArray()[0].toObject();
    details = item["liveStreamingDetails"].toObject();
    details["actualEndTime"] = "2026-10-14T15:00:00Z";
    item["liveStreamingDetails"] = details;
    completed["items"] = QJsonArray{item};
    hub.refresh();
    yh.answer(200, completed);
    other["snippet"] = QJsonObject{{"title", "Next live"}, {"liveChatId", "next-chat"}};
    yh.answer(200, {{"items", QJsonArray{other}}});
    require(youtube.broadcastId() == "broadcast-2" && ingestChanges == 0,
            "completed selection follows the new active broadcast without changing ingest");
    auto nextVideo = video("7");
    item = nextVideo["items"].toArray()[0].toObject();
    item["id"] = "broadcast-2";
    nextVideo["items"] = QJsonArray{item};
    yh.answer(200, nextVideo);
    require(hub.info(0).viewers == 7, "viewer requests follow the recovered broadcast ID");
    hub.shutdown();
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
        require(hub.info(0).viewers == 0 && hub.info(0).viewersUnreported,
                "omitted live count has explicit unreported marker, not a measured zero");
        hub.refresh();
        yh.answer(200, video("0"));
        require(hub.info(0).viewers == 0 && !hub.info(0).viewersUnreported, "explicit zero is known");
        for (const QJsonValue value : {QJsonValue(8), QJsonValue(0), QJsonValue(-1), QJsonValue(2.5),
                                       QJsonValue(true), QJsonValue("invalid")}) {
            auto sample = video();
            auto item = sample["items"].toArray()[0].toObject();
            auto live = item["liveStreamingDetails"].toObject();
            live["concurrentViewers"] = value;
            item["liveStreamingDetails"] = live;
            sample["items"] = QJsonArray{item};
            hub.refresh();
            yh.answer(200, sample);
            const auto expected = value == QJsonValue(8) ? 8 : value == QJsonValue(0) ? 0 : -1;
            require(hub.info(0).viewers == expected && !hub.info(0).viewersUnreported,
                    "numeric counts supported; malformed counts remain unavailable");
        }
        auto upcoming = video("");
        auto item = upcoming["items"].toArray()[0].toObject();
        item["liveStreamingDetails"] = QJsonObject{{"scheduledStartTime", "2026-10-15T12:00:00Z"}};
        upcoming["items"] = QJsonArray{item};
        hub.refresh();
        yh.answer(200, upcoming);
        yh.answer(200, broadcast("ready"));
        yh.answer(200, {{"items", QJsonArray{}}});
        require(hub.info(0).viewers == -1 && !hub.info(0).viewersUnreported,
                "upcoming broadcast is not reported as a live zero");
        auto ended = video("12");
        item = ended["items"].toArray()[0].toObject();
        auto details = item["liveStreamingDetails"].toObject();
        details["actualEndTime"] = "2026-10-14T13:00:00Z";
        item["liveStreamingDetails"] = details;
        ended["items"] = QJsonArray{item};
        hub.refresh();
        yh.answer(200, ended);
        yh.answer(200, {{"items", QJsonArray{}}});
        require(hub.info(0).viewers == -1 && hub.info(0).broadcastState == "complete",
                "completed broadcast does not retain a stale audience count");
        hub.refresh();
        yh.answer(403);
        require(hub.info(0).viewers == -1, "API failure is never displayed as zero");
        hub.saveInfo(0, "New title", "22");
        auto fresh = yh.take();
        require(fresh.method == "GET", "YouTube snippet fetched immediately before update");
        fresh.callback({200, video(), false});
        auto write = yh.take();
        const auto snippet = write.body["snippet"].toObject();
        require(write.method == "PUT" && snippet["tags"].toArray() == QJsonArray{"keep-me"} &&
                    snippet["defaultLanguage"] == "ru" && snippet["title"] == "New title" &&
                    snippet["description"] == "Original description",
                "title/category update preserves the existing description, tags and language");
        write.callback({200, {}, false});
        yh.answer(200, video());
        hub.searchCategories(0, {}, "RU", "ru_RU");
        auto categories = yh.take();
        require(QUrlQuery(categories.url).queryItemValue("hl") == "ru_RU", "category names use UI language");
        categories.callback({200, {{"items", QJsonArray{}}}, false});
        require(hub.youtubeStudioUrl().path() == "/video/broadcast-1/livestreaming",
                "Studio shortcut targets the selected broadcast");
        unsigned endedSignals = 0;
        QObject::connect(&hub, &PlatformHub::youtubeBroadcastEnded, [&] { ++endedSignals; });
        hub.transitionYouTube(true);
        hub.transitionYouTube(true);
        require(yh.calls.size() == 1 && hub.youtubeControlPending(), "duplicate lifecycle action suppressed");
        yh.answer(200, broadcast("ready"));
        yh.answer(200, stream("inactive"));
        require(yh.calls.empty() && !hub.youtubeControlPending() &&
                    hub.youtubeControlStatus() == "YouTubeNoSignal",
                "no video means no go-live mutation");
        hub.transitionYouTube(true);
        yh.answer(200, broadcast("ready"));
        yh.answer(200, stream("active"));
        auto goLive = yh.take();
        require(goLive.method == "POST" && goLive.body.isEmpty() &&
                    goLive.url.path().endsWith("/liveBroadcasts/transition") &&
                    QUrlQuery(goLive.url).queryItemValue("id") == "broadcast-1" &&
                    QUrlQuery(goLive.url).queryItemValue("broadcastStatus") == "live",
                "go-live uses selected broadcast, empty POST body and active ingest");
        goLive.callback({200, transition("live"), false});
        yh.answer(200, video());
        require(!hub.youtubeControlPending() && hub.youtubeControlStatus() == "YouTubeIsLive",
                "go-live is confirmed by provider state");
        hub.transitionYouTube(false);
        yh.answer(200, broadcast("live"));
        auto endRequest = yh.take();
        require(QUrlQuery(endRequest.url).queryItemValue("broadcastStatus") == "complete",
                "end action requests broadcast completion");
        endRequest.callback({403, {}, false});
        require(endedSignals == 0 && !hub.youtubeControlPending(), "failed end never stops video output");
        hub.transitionYouTube(false);
        yh.answer(200, broadcast("live"));
        yh.answer(200,
                  {{"id", "different-broadcast"}, {"status", QJsonObject{{"lifeCycleStatus", "complete"}}}});
        require(endedSignals == 0 && hub.youtubeControlStatus() == "YouTubeTransitionUnknown",
                "unverified completion cannot stop the selected output");
        hub.transitionYouTube(false);
        yh.answer(200, broadcast("live"));
        yh.answer(200, transition("complete"));
        require(endedSignals == 1, "confirmed end signals only YouTube output to stop");
        yh.answer(200, ended);
        yh.answer(200, {{"items", QJsonArray{}}});
        hub.transitionYouTube(true);
        yh.answer(200, broadcast("complete"));
        require(yh.calls.empty() && hub.youtubeControlStatus() == "YouTubeInvalidTransition",
                "completed broadcast cannot be restarted");
        hub.transitionYouTube(true);
        yh.answer(200, broadcast("ready", true));
        yh.answer(200, stream("active"));
        auto testing = yh.take();
        require(QUrlQuery(testing.url).queryItemValue("broadcastStatus") == "testing",
                "monitor-enabled broadcast enters testing before live");
        testing.callback({200, transition("testStarting"), false});
        require(hub.youtubeControlPending(), "testing transition is not reported as complete early");
        awaitRequest(yh);
        yh.answer(200, broadcast("testing", true));
        auto afterTest = yh.take();
        require(QUrlQuery(afterTest.url).queryItemValue("broadcastStatus") == "live",
                "confirmed testing automatically continues user-requested go-live");
        afterTest.callback({200, transition("liveStarting"), false});
        awaitRequest(yh);
        yh.answer(200, broadcast("live", true));
        yh.answer(200, video());
        require(!hub.youtubeControlPending() && hub.youtubeControlStatus() == "YouTubeIsLive",
                "asynchronous live transition waits for confirmation");
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
        hub.refresh();
        hub.refresh();
        hub.setChatEnabled(false);
        lateChat.callback(
            {200,
             {{"items", QJsonArray{QJsonObject{{"id", "late"},
                                               {"snippet", QJsonObject{{"displayMessage", "late"}}}}}}},
             false});
        require(messages == 1, "chat disabled while request is pending rejects late messages");
        auto delayedInfo = yh.take();
        require(delayedInfo.url.path().endsWith("/videos"),
                "viewer refresh is queued behind chat instead of skipped");
        delayedInfo.callback({200, video("9"), false});
        require(hub.info(0).viewers == 9 && yh.calls.empty(), "duplicate viewer refreshes are coalesced");
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
        hub.saveInfo(1, "New Twitch title", "99");
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
        recoveryTests();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
