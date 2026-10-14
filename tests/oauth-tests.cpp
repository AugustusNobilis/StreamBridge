// SPDX-License-Identifier: GPL-2.0-or-later
#include "oauth/account.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <deque>
#include <iostream>
#include <stdexcept>

using namespace sb;
static void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
    std::cout << "PASS: " << message << std::endl;
}
static bool until(const std::function<bool()> &condition, int ms = 3000) {
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents();
        if (condition())
            return true;
        QThread::msleep(5);
    } while (timer.elapsed() < ms);
    return false;
}
class FakeHttp final : public HttpClient {
  public:
    struct Request {
        QUrl url;
        QUrlQuery form;
        QByteArray token, client;
        bool post;
        Callback callback;
    };
    std::deque<Request> requests;
    unsigned sent = 0;
    void send(const QUrl &url, const QUrlQuery &form, const QByteArray &token, const QByteArray &client,
              bool post, Callback callback) override {
        ++sent;
        requests.push_back({url, form, token, client, post, std::move(callback)});
    }
    void cancel() override { requests.clear(); }
    Request take() {
        if (requests.empty())
            throw std::runtime_error("expected HTTP request");
        auto request = std::move(requests.front());
        requests.pop_front();
        return request;
    }
    void answer(int status, QJsonObject json) { take().callback({status, std::move(json), false}); }
};
static QJsonObject token(const QString &access = "fixture-access",
                         const QString &refresh = "fixture-refresh") {
    return {{"access_token", access},
            {"refresh_token", refresh},
            {"expires_in", 14400},
            {"token_type", "bearer"}};
}
static QJsonObject validTwitch() {
    return {{"client_id", "public-client"},
            {"user_id", "123"},
            {"login", "test-user"},
            {"scopes", QJsonArray{"channel:read:stream_key"}},
            {"expires_in", 14400}};
}
static void callback(const QUrl &url, const QString &state, const QString &code, bool expectSuccess) {
    QTcpSocket socket;
    socket.connectToHost("127.0.0.1", static_cast<quint16>(url.port()));
    require(socket.waitForConnected(1000), "loopback callback TCP connected");
    QUrl target(url);
    QUrlQuery query;
    query.addQueryItem("state", state);
    query.addQueryItem("code", code);
    target.setQuery(query);
    socket.write("GET " + target.path().toUtf8() + '?' + target.query(QUrl::FullyEncoded).toUtf8() +
                 " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    socket.flush();
    require(until([&] { return socket.bytesAvailable() > 0; }), "loopback callback replied");
    require(socket.readAll().startsWith(expectSuccess ? "HTTP/1.1 200" : "HTTP/1.1 400"),
            "callback state validated");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        require(pkceChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") ==
                    "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
                "RFC 7636 S256 vector");
        require(randomUrlToken().size() == 43 && randomUrlToken() != randomUrlToken(),
                "random 256-bit authorization tokens");
        QUrlQuery form;
        form.addQueryItem("refresh_token", "a+b&c=d% e");
        require(encodeForm(form) == "refresh_token=a%2Bb%26c%3Dd%25%20e",
                "form encoding preserves token punctuation");
        OAuthHttp real(nullptr);
        bool blocked = false;
        real.send(QUrl("http://127.0.0.1:1/token"), {}, "secret", {}, true,
                  [&](HttpResult r) { blocked = r.transportError; });
        require(until([&] { return blocked; }), "HTTP and non-provider token destinations refused");
        QTemporaryDir temp;
        require(temp.isValid(), "isolated OAuth credential directory");
        SecretStore vault(temp.path());
        QString error;
        FakeHttp twitchHttp;
        OAuthAccount twitch(Provider::Twitch, vault, nullptr, &twitchHttp);
        QString codeShown;
        QObject::connect(&twitch, &OAuthAccount::authorizationRequired, &app,
                         [&](const QUrl &url, const QString &code) {
                             if (url.host() == "www.twitch.tv")
                                 codeShown = code;
                         });
        QByteArray key;
        QObject::connect(&twitch, &OAuthAccount::ingestReady, &app,
                         [&](const QString &server, const QByteArray &value) {
                             if (server.startsWith("rtmps://"))
                                 key = value;
                         });
        twitch.link("public-client");
        auto device = twitchHttp.take();
        require(
            device.post && device.url.path() == "/oauth2/device" &&
                device.form.queryItemValue("scopes").split(' ').contains("channel:read:stream_key") &&
                device.form.queryItemValue("scopes").split(' ').contains("channel:manage:broadcast") &&
                device.form.queryItemValue("scopes").split(' ').contains("moderator:manage:chat_messages") &&
                device.form.queryItemValue("scopes").split(' ').contains("moderator:manage:banned_users") &&
                device.form.queryItemValue("scopes").split(' ').contains("chat:read") &&
                !device.form.hasQueryItem("client_secret"),
            "Twitch documented public device flow and scope");
        device.callback({200,
                         {{"device_code", "fixture-device"},
                          {"expires_in", 120},
                          {"interval", 1},
                          {"user_code", "TESTCODE"},
                          {"verification_uri", "https://www.twitch.tv/activate"}},
                         false});
        require(codeShown == "TESTCODE" && twitchHttp.requests.empty(),
                "device code displayed before interval polling");
        require(until([&] { return !twitchHttp.requests.empty(); }), "device poll occurs after interval");
        twitchHttp.answer(400, {{"message", "slow_down"}});
        const unsigned count = twitchHttp.sent;
        until([] { return false; }, 1200);
        require(twitchHttp.sent == count, "slow_down prevents rapid polling");
        require(until([&] { return !twitchHttp.requests.empty(); }, 7000),
                "slow_down increases next poll delay");
        twitchHttp.answer(200, token());
        require(twitch.linked(), "tokens stored after device grant");
        auto validate = twitchHttp.take();
        require(validate.url.path() == "/oauth2/validate" && validate.token == "fixture-access",
                "Twitch token validated before Helix");
        validate.callback({200, validTwitch(), false});
        auto helix = twitchHttp.take();
        require(helix.url.path() == "/helix/streams/key" && helix.client == "public-client" &&
                    QUrlQuery(helix.url).queryItemValue("broadcaster_id") == "123",
                "stream key uses authorized broadcaster and Client-Id");
        helix.callback({200, {{"data", QJsonArray{QJsonObject{{"stream_key", "fixture-key"}}}}}, false});
        require(key == "fixture-key" && !twitch.busy(), "Twitch ingest delivered without UI blocking");
        QFile file(temp.path() + "/vault.dpapi");
        require(file.open(QIODevice::ReadOnly), "encrypted account vault exists");
        const auto disk = file.readAll();
        file.close();
        require(!disk.contains("fixture-access") && !disk.contains("fixture-refresh"),
                "OAuth credentials absent from plaintext disk content");
        twitch.disconnectAccount();
        require(!twitch.linked() && vault.get("Twitch/oauth", error).isEmpty(),
                "disconnect erases local tokens before network revocation");
        twitchHttp.answer(503, {});
        require(twitch.statusKey() == "OAuthRevokeFailed",
                "revocation failure reported while remaining disconnected");
        twitch.link("public-client");
        auto cancelled = twitchHttp.take();
        twitch.cancel();
        cancelled.callback({200, token(), false});
        require(!twitch.linked() && twitchHttp.requests.empty(),
                "late cancelled authorization response ignored");

        auto saved = token("old-access", "old-refresh");
        saved.insert("client_id", "public-client");
        saved.insert("expires_at", QDateTime::currentSecsSinceEpoch() - 1);
        require(vault.put("Twitch/oauth", QJsonDocument(saved).toJson(), error),
                "expired session fixture saved");
        twitch.resume();
        auto refresh = twitchHttp.take();
        require(refresh.form.queryItemValue("grant_type") == "refresh_token" &&
                    refresh.form.queryItemValue("refresh_token") == "old-refresh" &&
                    !refresh.form.hasQueryItem("client_secret"),
                "public client refresh is secretless");
        refresh.callback({200, token("new-access", "rotated-refresh"), false});
        require(QJsonDocument::fromJson(vault.get("Twitch/oauth", error)).object().value("refresh_token") ==
                    "rotated-refresh",
                "rotated refresh token persisted atomically");
        twitchHttp.answer(401, {});
        require(!twitch.linked() && vault.get("Twitch/oauth", error).isEmpty(),
                "revoked Twitch token ends stored session");

        FakeHttp googleHttp;
        OAuthAccount google(Provider::YouTube, vault, nullptr, &googleHttp);
        QUrl authorize;
        QObject::connect(&google, &OAuthAccount::authorizationRequired, &app,
                         [&](const QUrl &url, const QString &) { authorize = url; });
        QObject::connect(&google, &OAuthAccount::ingestReady, &app,
                         [&](const QString &, const QByteArray &value) { key = value; });
        google.link("desktop-client");
        const QUrlQuery auth(authorize);
        const QUrl redirect(auth.queryItemValue("redirect_uri"));
        require(authorize.host() == "accounts.google.com" &&
                    auth.queryItemValue("code_challenge_method") == "S256" && redirect.host() == "127.0.0.1",
                "Google external-browser S256 and random loopback port");
        callback(redirect, "incorrect-state", "injected-code", false);
        require(googleHttp.requests.empty(), "CSRF response cannot trigger token exchange");
        callback(redirect, auth.queryItemValue("state"), "fixture-code", true);
        auto exchange = googleHttp.take();
        require(exchange.form.queryItemValue("code") == "fixture-code" &&
                    pkceChallenge(exchange.form.queryItemValue("code_verifier").toLatin1()) ==
                        auth.queryItemValue("code_challenge") &&
                    !exchange.form.hasQueryItem("client_secret"),
                "Google code exchange uses matching PKCE verifier without private secret");
        exchange.callback({200, token(), false});
        require(googleHttp.requests.front().url.path().endsWith("liveBroadcasts"),
                "existing YouTube broadcasts requested");
        googleHttp.answer(
            200,
            {{"items",
              QJsonArray{QJsonObject{{"id", "bound"},
                                     {"snippet", QJsonObject{{"title", "Fixture broadcast"}}},
                                     {"contentDetails", QJsonObject{{"boundStreamId", "stream1"}}}},
                         QJsonObject{{"id", "unbound"}, {"snippet", QJsonObject{{"title", "Unbound"}}}}}}});
        require(google.broadcasts().size() == 2 && !google.busy(),
                "broadcast list delivered for explicit selection");
        google.selectBroadcast("unbound");
        require(google.statusKey() == "OAuthUnboundBroadcast" && googleHttp.requests.empty(),
                "unbound broadcast does not create or publish a stream");
        google.selectBroadcast("bound");
        auto stream = googleHttp.take();
        require(stream.url.path().endsWith("liveStreams") &&
                    QUrlQuery(stream.url).queryItemValue("id") == "stream1",
                "selected broadcast resolves its bound stream");
        stream.callback(
            {200,
             {{"items", QJsonArray{QJsonObject{
                            {"cdn", QJsonObject{{"ingestionInfo",
                                                 QJsonObject{{"rtmpsIngestionAddress",
                                                              "rtmps://a.rtmps.youtube.com:443/live2"},
                                                             {"streamName", "youtube-fixture-key"}}}}}}}}},
             false});
        require(key == "youtube-fixture-key", "YouTube RTMPS address and key returned");
        google.disconnectAccount();
        googleHttp.answer(200, {});
        require(!google.linked() && google.statusKey() == "OAuthDisconnected",
                "Google session revoked and cleared");
        const auto credentials = parseGoogleDesktopClient(
            R"({"installed":{"client_id":"123-desktop.apps.googleusercontent.com","client_secret":"desktop-fixture-secret","token_uri":"https://invalid.example/token"}})",
            error);
        require(error.isEmpty() && !credentials.id.isEmpty(), "Google Desktop credential JSON accepted");
        require(
            parseGoogleDesktopClient(
                R"({"web":{"client_id":"123.apps.googleusercontent.com","client_secret":"private"}})", error)
                    .id.isEmpty() &&
                error == "OAuthGoogleDesktopRequired",
            "confidential web credentials rejected");
        require(parseGoogleDesktopClient(QByteArray(65537, 'x'), error).id.isEmpty(),
                "oversized credential import rejected");
        google.link(credentials.id, credentials.secret);
        const QUrlQuery importedAuth(authorize);
        require(!importedAuth.hasQueryItem("client_secret") &&
                    !authorize.toString().contains("desktop-fixture-secret"),
                "desktop credential never appears in browser URL");
        callback(QUrl(importedAuth.queryItemValue("redirect_uri")), importedAuth.queryItemValue("state"),
                 "desktop-code", true);
        auto importedExchange = googleHttp.take();
        require(importedExchange.url == QUrl("https://oauth2.googleapis.com/token") &&
                    importedExchange.form.queryItemValue("client_secret") == "desktop-fixture-secret",
                "desktop exchange uses credential and fixed Google endpoint");
        importedExchange.callback({200, token(), false});
        googleHttp.answer(200, {{"items", QJsonArray{}}});
        auto googleSaved = QJsonDocument::fromJson(vault.get("YouTube/oauth", error)).object();
        require(googleSaved.value("client_secret") == "desktop-fixture-secret",
                "desktop credential persists with encrypted refresh token");
        require(file.open(QIODevice::ReadOnly) && !file.readAll().contains("desktop-fixture-secret"),
                "desktop credential absent from plaintext vault");
        file.close();
        googleSaved["expires_at"] = QDateTime::currentSecsSinceEpoch() - 1;
        require(vault.put("YouTube/oauth", QJsonDocument(googleSaved).toJson(), error),
                "expired Google fixture saved");
        FakeHttp resumeHttp;
        {
            OAuthAccount resumed(Provider::YouTube, vault, nullptr, &resumeHttp);
            resumed.resume();
            auto renewed = resumeHttp.take();
            require(renewed.form.queryItemValue("client_secret") == "desktop-fixture-secret" &&
                        renewed.form.queryItemValue("grant_type") == "refresh_token",
                    "Google refresh after restart includes imported credential");
            renewed.callback({200, token("renewed-access", "renewed-refresh"), false});
            require(
                QJsonDocument::fromJson(vault.get("YouTube/oauth", error)).object().value("client_secret") ==
                    "desktop-fixture-secret",
                "refresh rotation retains desktop credential");
        }
        google.disconnectAccount();
        googleHttp.answer(200, {});
        require(vault.get("YouTube/oauth", error).isEmpty(), "disconnect removes desktop credential");
        for (const auto &failure : std::vector<std::pair<QJsonObject, QString>>{
                 {{{"error", "invalid_request"},
                   {"error_description", "client_secret is missing sensitive-fixture"}},
                  "OAuthGoogleSecretRequired"},
                 {{{"error", "invalid_client"}}, "OAuthInvalidClient"},
                 {{{"error", "invalid_grant"}}, "OAuthGrantExpired"},
                 {{{"error", "unauthorized_client"}}, "OAuthClientType"}}) {
            google.link(credentials.id);
            const QUrlQuery request(authorize);
            callback(QUrl(request.queryItemValue("redirect_uri")), request.queryItemValue("state"), "code",
                     true);
            googleHttp.answer(400, failure.first);
            require(google.statusKey() == failure.second && !google.busy(),
                    "provider failure mapped to actionable safe message");
        }
        std::cout << "PASS: OAuth protocol and encrypted persistence tests complete (mock provider APIs)\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << std::endl;
        return 1;
    }
}
