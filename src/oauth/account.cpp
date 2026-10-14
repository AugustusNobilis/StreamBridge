// SPDX-License-Identifier: GPL-2.0-or-later
#include "account.hpp"
#include "config/config.hpp"
#include <QJsonDocument>
#include <algorithm>

namespace sb {
static const QString twitchScope = "channel:read:stream_key";
static const QString youtubeScope = "https://www.googleapis.com/auth/youtube.readonly";
OAuthAccount::OAuthAccount(Provider provider, SecretStore &store, QObject *parent, HttpClient *transport)
    : QObject(parent), provider_(provider), store_(store), http_(transport ? transport : new OAuthHttp(this)),
      loopback_(this), poll_(this), maintenance_(this) {
    poll_.setSingleShot(true);
    maintenance_.setSingleShot(true);
    connect(&poll_, &QTimer::timeout, this, &OAuthAccount::pollDevice);
    connect(&maintenance_, &QTimer::timeout, this, [this] {
        if (busy_) {
            maintenance_.start(1000);
            return;
        }
        withAccess([this] {
            if (provider_ == Provider::Twitch)
                validateTwitch(false);
            else {
                setStatus("OAuthConnected");
                armMaintenance();
            }
        });
    });
    connect(&loopback_, &OAuthLoopback::expired, this, [this] { fail("OAuthExpired"); });
    connect(&loopback_, &OAuthLoopback::completed, this, [this](const QString &code, bool denied) {
        if (denied) {
            fail("OAuthDenied");
            return;
        }
        setStatus("OAuthConnecting", true);
        QUrlQuery form;
        form.addQueryItem("client_id", clientId_);
        form.addQueryItem("grant_type", "authorization_code");
        form.addQueryItem("code", code);
        form.addQueryItem("redirect_uri", redirect_.toString());
        form.addQueryItem("code_verifier", QString::fromLatin1(verifier_));
        request(tokenUrl(), form, true, false, [this](HttpResult result) {
            verifier_.fill('\0');
            verifier_.clear();
            if (!result.ok()) {
                fail("OAuthTokenFailed");
                return;
            }
            if (saveTokens(result.json)) {
                setStatus("OAuthConnected");
                armMaintenance();
                fetchIngest();
            }
        });
    });
}
OAuthAccount::~OAuthAccount() {
    cancel();
}
QString OAuthAccount::vaultKey() const {
    return (provider_ == Provider::Twitch ? "Twitch" : "YouTube") + QString("/oauth");
}
QUrl OAuthAccount::tokenUrl() const {
    return QUrl(provider_ == Provider::Twitch ? "https://id.twitch.tv/oauth2/token"
                                              : "https://oauth2.googleapis.com/token");
}
void OAuthAccount::setStatus(const QString &key, bool busy) {
    status_ = key;
    busy_ = busy;
    emit changed();
}
void OAuthAccount::request(const QUrl &url, const QUrlQuery &form, bool post, bool authenticated,
                           HttpClient::Callback callback) {
    const auto generation = generation_;
    http_->send(url, form, authenticated ? tokens_.value("access_token").toString().toUtf8() : QByteArray(),
                authenticated && provider_ == Provider::Twitch ? clientId().toUtf8() : QByteArray(), post,
                [this, generation, callback](HttpResult result) {
                    if (generation == generation_)
                        callback(std::move(result));
                });
}
void OAuthAccount::cancel() {
    ++generation_;
    http_->cancel();
    poll_.stop();
    maintenance_.stop();
    loopback_.stop();
    verifier_.fill('\0');
    verifier_.clear();
    deviceCode_.fill('\0');
    deviceCode_.clear();
    busy_ = false;
}
void OAuthAccount::fail(const QString &key) {
    cancel();
    setStatus(key);
    if (linked() && (key == "OAuthNetworkFailed" || key == "OAuthApiFailed"))
        maintenance_.start(60000);
}
void OAuthAccount::resume() {
    if (busy_ || linked())
        return;
    QString error;
    auto saved = store_.get(vaultKey(), error);
    if (!error.isEmpty()) {
        fail("OAuthVaultFailed");
        return;
    }
    if (saved.isEmpty())
        return;
    QJsonParseError parse;
    tokens_ = QJsonDocument::fromJson(saved, &parse).object();
    saved.fill('\0');
    if (parse.error != QJsonParseError::NoError || !linked() || clientId().isEmpty()) {
        tokens_ = {};
        fail("OAuthVaultFailed");
        return;
    }
    clientId_ = clientId();
    label_ = tokens_.value("label").toString();
    withAccess([this] {
        if (provider_ == Provider::Twitch)
            validateTwitch(false);
        else {
            setStatus("OAuthConnected");
            armMaintenance();
        }
    });
}
void OAuthAccount::link(const QString &clientId) {
    if (busy_ || linked())
        return;
    const auto id = clientId.trimmed();
    if (id.isEmpty() || id.size() > 256 || id.contains(QChar::Space) || id.contains('\n') ||
        id.contains('\r')) {
        fail("OAuthClientRequired");
        return;
    }
    cancel();
    clientId_ = id;
    label_.clear();
    broadcasts_.clear();
    setStatus("OAuthConnecting", true);
    if (provider_ == Provider::YouTube) {
        verifier_ = randomUrlToken();
        const auto state = randomUrlToken();
        redirect_ = loopback_.start(state);
        if (redirect_.isEmpty()) {
            fail("OAuthLoopbackFailed");
            return;
        }
        QUrl url("https://accounts.google.com/o/oauth2/v2/auth");
        QUrlQuery query;
        query.addQueryItem("client_id", clientId_);
        query.addQueryItem("response_type", "code");
        query.addQueryItem("redirect_uri", redirect_.toString());
        query.addQueryItem("scope", youtubeScope);
        query.addQueryItem("code_challenge", QString::fromLatin1(pkceChallenge(verifier_)));
        query.addQueryItem("code_challenge_method", "S256");
        query.addQueryItem("state", QString::fromLatin1(state));
        query.addQueryItem("access_type", "offline");
        query.addQueryItem("prompt", "consent");
        url.setQuery(query);
        setStatus("OAuthAuthorize", true);
        emit authorizationRequired(url, {});
        return;
    }
    QUrlQuery form;
    form.addQueryItem("client_id", clientId_);
    form.addQueryItem("scopes", twitchScope);
    request(QUrl("https://id.twitch.tv/oauth2/device"), form, true, false, [this](HttpResult result) {
        if (!result.ok()) {
            fail("OAuthTokenFailed");
            return;
        }
        const auto &json = result.json;
        const QUrl verify(json.value("verification_uri").toString(), QUrl::StrictMode);
        deviceCode_ = json.value("device_code").toString().toUtf8();
        const auto code = json.value("user_code").toString();
        const int expires = json.value("expires_in").toInt();
        const int interval = json.value("interval").toInt();
        if (verify.scheme() != "https" || verify.host() != "www.twitch.tv" || verify.path() != "/activate" ||
            !verify.userInfo().isEmpty() || verify.port(443) != 443 || deviceCode_.isEmpty() ||
            deviceCode_.size() > 4096 || code.isEmpty() || code.size() > 128 || expires <= 0 ||
            expires > 3600 || interval <= 0 || interval > 60) {
            fail("OAuthInvalidResponse");
            return;
        }
        deviceExpiry_ = QDateTime::currentDateTimeUtc().addSecs(expires);
        interval_ = interval;
        setStatus("OAuthAuthorize", true);
        emit authorizationRequired(verify, code);
        poll_.start(interval_ * 1000);
    });
}
void OAuthAccount::pollDevice() {
    if (QDateTime::currentDateTimeUtc() >= deviceExpiry_) {
        fail("OAuthExpired");
        return;
    }
    QUrlQuery form;
    form.addQueryItem("client_id", clientId_);
    form.addQueryItem("scopes", twitchScope);
    form.addQueryItem("device_code", QString::fromUtf8(deviceCode_));
    form.addQueryItem("grant_type", "urn:ietf:params:oauth:grant-type:device_code");
    request(tokenUrl(), form, true, false, [this](HttpResult result) {
        if (result.ok()) {
            deviceCode_.fill('\0');
            deviceCode_.clear();
            if (saveTokens(result.json))
                validateTwitch(true);
            return;
        }
        const auto reason = result.json.value("error").toString(result.json.value("message").toString());
        if (result.status == 400 && (reason == "authorization_pending" || reason == "slow_down")) {
            if (reason == "slow_down")
                interval_ += 5;
            poll_.start(interval_ * 1000);
            return;
        }
        fail(reason == "access_denied" ? "OAuthDenied" : "OAuthTokenFailed");
    });
}
bool OAuthAccount::saveTokens(const QJsonObject &json) {
    const auto access = json.value("access_token").toString();
    const auto refresh = json.value("refresh_token").toString(tokens_.value("refresh_token").toString());
    const int expires = json.value("expires_in").toInt();
    if (access.isEmpty() || access.size() > 8192 || refresh.isEmpty() || refresh.size() > 8192 ||
        expires <= 0 || expires > 365 * 86400 ||
        json.value("token_type").toString().compare("bearer", Qt::CaseInsensitive) != 0) {
        fail("OAuthInvalidResponse");
        return false;
    }
    // A single encrypted JSON entry makes refresh-token rotation atomic.
    QJsonObject next{{"client_id", clientId_},
                     {"access_token", access},
                     {"refresh_token", refresh},
                     {"expires_at", QDateTime::currentSecsSinceEpoch() + expires},
                     {"label", label_}};
    QString error;
    auto bytes = QJsonDocument(next).toJson(QJsonDocument::Compact);
    const bool saved = store_.put(vaultKey(), bytes, error);
    bytes.fill('\0');
    tokens_ = next; // Never reuse a rotated refresh token, even if the disk write failed.
    if (!saved) {
        fail("OAuthVaultFailed");
        return false;
    }
    return true;
}
void OAuthAccount::expireSession() {
    QString error;
    const bool removed = store_.erase(vaultKey(), error);
    tokens_ = {};
    label_.clear();
    broadcasts_.clear();
    fail(removed ? "OAuthReconnectRequired" : "OAuthVaultFailed");
}
void OAuthAccount::refresh(std::function<void()> continuation) {
    setStatus("OAuthRefreshing", true);
    QUrlQuery form;
    form.addQueryItem("client_id", clientId_);
    form.addQueryItem("grant_type", "refresh_token");
    form.addQueryItem("refresh_token", tokens_.value("refresh_token").toString());
    request(tokenUrl(), form, true, false, [this, continuation](HttpResult result) {
        if (!result.ok()) {
            if (result.status == 400 || result.status == 401)
                expireSession();
            else
                fail("OAuthNetworkFailed");
            return;
        }
        if (saveTokens(result.json))
            continuation();
    });
}
void OAuthAccount::withAccess(std::function<void()> continuation) {
    if (!linked()) {
        fail("OAuthReconnectRequired");
        return;
    }
    setStatus("OAuthConnecting", true);
    if (tokens_.value("expires_at").toInteger() <= QDateTime::currentSecsSinceEpoch() + 120)
        refresh(std::move(continuation));
    else
        continuation();
}
void OAuthAccount::armMaintenance() {
    const auto remaining = tokens_.value("expires_at").toInteger() - QDateTime::currentSecsSinceEpoch() - 120;
    // Never poll faster than 30 seconds, and validate Twitch at least hourly.
    maintenance_.start(static_cast<int>(std::clamp<qint64>(remaining, 30, 3500) * 1000));
}
void OAuthAccount::validateTwitch(bool fetchKey) {
    setStatus("OAuthConnecting", true);
    request(
        QUrl("https://id.twitch.tv/oauth2/validate"), {}, false, true, [this, fetchKey](HttpResult result) {
            if (result.status == 401) {
                expireSession();
                return;
            }
            if (!result.ok()) {
                fail("OAuthNetworkFailed");
                return;
            }
            const auto user = result.json.value("user_id").toString();
            if (result.json.value("client_id").toString() != clientId_ || user.isEmpty() ||
                !result.json.value("scopes").toArray().contains(twitchScope)) {
                expireSession();
                return;
            }
            label_ = result.json.value("login").toString();
            setStatus("OAuthConnected");
            armMaintenance();
            if (!fetchKey)
                return;
            QUrl url("https://api.twitch.tv/helix/streams/key");
            QUrlQuery query;
            query.addQueryItem("broadcaster_id", user);
            url.setQuery(query);
            setStatus("OAuthLoading", true);
            request(url, {}, false, true, [this](HttpResult keyResult) {
                if (keyResult.status == 401) {
                    expireSession();
                    return;
                }
                if (!keyResult.ok()) {
                    fail("OAuthApiFailed");
                    return;
                }
                const auto data = keyResult.json.value("data").toArray();
                const auto key = data.isEmpty() ? QByteArray()
                                                : data[0].toObject().value("stream_key").toString().toUtf8();
                if (key.isEmpty() || key.size() > 8192) {
                    fail("OAuthInvalidResponse");
                    return;
                }
                setStatus("OAuthConnected");
                armMaintenance();
                emit ingestReady("rtmps://live.twitch.tv:443/app", key);
            });
        });
}
void OAuthAccount::fetchIngest() {
    if (busy_)
        return;
    withAccess([this] {
        if (provider_ == Provider::Twitch)
            validateTwitch(true);
        else {
            broadcasts_.clear();
            pages_ = 0;
            listBroadcasts();
        }
    });
}
void OAuthAccount::listBroadcasts(QString page) {
    setStatus("OAuthLoading", true);
    QUrl url("https://www.googleapis.com/youtube/v3/liveBroadcasts");
    QUrlQuery query;
    query.addQueryItem("part", "id,snippet,contentDetails,status");
    query.addQueryItem("mine", "true");
    query.addQueryItem("broadcastType", "all");
    query.addQueryItem("maxResults", "50");
    if (!page.isEmpty())
        query.addQueryItem("pageToken", page);
    url.setQuery(query);
    ++pages_;
    request(url, {}, false, true, [this](HttpResult result) {
        if (result.status == 401) {
            expireSession();
            return;
        }
        if (!result.ok()) {
            fail("OAuthApiFailed");
            return;
        }
        for (const auto &item : result.json.value("items").toArray()) {
            const auto o = item.toObject();
            if (o.value("status").toObject().value("lifeCycleStatus").toString() == "complete")
                continue;
            const auto id = o.value("id").toString();
            const auto title = o.value("snippet").toObject().value("title").toString();
            const auto bound = o.value("contentDetails").toObject().value("boundStreamId").toString();
            if (!id.isEmpty() && id.size() <= 256 && title.size() <= 1024 && bound.size() <= 256)
                broadcasts_.append({id, title, bound});
        }
        const auto next = result.json.value("nextPageToken").toString();
        if (!next.isEmpty() && pages_ < 20 && next.size() <= 4096) {
            listBroadcasts(next);
            return;
        }
        setStatus(broadcasts_.isEmpty() ? "OAuthNoBroadcasts" : "OAuthChooseBroadcast");
        armMaintenance();
    });
}
void OAuthAccount::selectBroadcast(const QString &id) {
    if (busy_)
        return;
    const auto it = std::find_if(broadcasts_.cbegin(), broadcasts_.cend(),
                                 [&](const Broadcast &b) { return b.id == id; });
    if (it == broadcasts_.cend()) {
        fail("OAuthChooseBroadcast");
        return;
    }
    if (it->streamId.isEmpty()) {
        fail("OAuthUnboundBroadcast");
        return;
    }
    const auto streamId = it->streamId;
    withAccess([this, streamId] {
        QUrl url("https://www.googleapis.com/youtube/v3/liveStreams");
        QUrlQuery query;
        query.addQueryItem("part", "cdn");
        query.addQueryItem("id", streamId);
        url.setQuery(query);
        request(url, {}, false, true, [this](HttpResult result) {
            if (result.status == 401) {
                expireSession();
                return;
            }
            if (!result.ok()) {
                fail("OAuthApiFailed");
                return;
            }
            const auto items = result.json.value("items").toArray();
            const auto ingest =
                items.isEmpty()
                    ? QJsonObject()
                    : items[0].toObject().value("cdn").toObject().value("ingestionInfo").toObject();
            const auto server = ingest.value("rtmpsIngestionAddress").toString();
            const auto key = ingest.value("streamName").toString().toUtf8();
            if (!validateServer(server).isEmpty() || key.isEmpty() || key.size() > 8192) {
                fail("OAuthInvalidResponse");
                return;
            }
            setStatus("OAuthConnected");
            armMaintenance();
            emit ingestReady(server, key);
        });
    });
}
void OAuthAccount::disconnectAccount() {
    cancel();
    const auto old = tokens_;
    QString error;
    if (!store_.erase(vaultKey(), error)) {
        fail("OAuthVaultFailed");
        return;
    }
    tokens_ = {};
    label_.clear();
    broadcasts_.clear();
    setStatus("OAuthDisconnecting", true);
    const auto token =
        old.value(provider_ == Provider::YouTube ? "refresh_token" : "access_token").toString();
    if (token.isEmpty()) {
        setStatus("OAuthDisconnected");
        return;
    }
    QUrlQuery form;
    form.addQueryItem("token", token);
    if (provider_ == Provider::Twitch)
        form.addQueryItem("client_id", old.value("client_id").toString());
    const QUrl url(provider_ == Provider::Twitch ? "https://id.twitch.tv/oauth2/revoke"
                                                 : "https://oauth2.googleapis.com/revoke");
    request(url, form, true, false, [this](HttpResult result) {
        setStatus(result.ok() ? "OAuthDisconnected" : "OAuthRevokeFailed");
    });
}
} // namespace sb
