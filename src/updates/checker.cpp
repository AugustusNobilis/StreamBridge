// SPDX-License-Identifier: GPL-2.0-or-later
#include "checker.hpp"
#include "launch.hpp"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QVersionNumber>
#ifndef STREAMBRIDGE_GITHUB_REPOSITORY
#define STREAMBRIDGE_GITHUB_REPOSITORY ""
#endif
#ifndef STREAMBRIDGE_VERSION
#define STREAMBRIDGE_VERSION "0.1.2"
#endif
namespace sb {
namespace {
constexpr qint64 maxMetadata = 2 * 1024 * 1024, maxInstaller = 1024LL * 1024 * 1024;
bool validRepository(const QString &r) {
    return QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.-]{0,99}/[A-Za-z0-9][A-Za-z0-9_.-]{0,99}$")
        .match(r)
        .hasMatch();
}
bool validVersion(const QString &v) {
    return QRegularExpression("^[0-9]{1,4}\\.[0-9]{1,4}\\.[0-9]{1,4}$").match(v).hasMatch();
}
QString assetName(const QString &version) {
    return "obs-streambridge-" + version + "-windows-x64-setup.exe";
}
bool writeJson(const QString &path, const QJsonObject &object) {
    QSaveFile file(path);
    const auto bytes = QJsonDocument(object).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QJsonObject readJson(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.size() <= 16384
               ? QJsonDocument::fromJson(file.readAll()).object()
               : QJsonObject();
}
QNetworkRequest requestFor(const QUrl &url) {
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    request.setRawHeader("User-Agent", "OBS-StreamBridge/" STREAMBRIDGE_VERSION);
    return request;
}
} // namespace
QString normalizeRepository(QString repository) {
    repository = repository.trimmed();
    if (repository.startsWith("https://github.com/"))
        repository.remove(0, 19);
    if (repository.endsWith('/'))
        repository.chop(1);
    if (repository.endsWith(".git"))
        repository.chop(4);
    return validRepository(repository) ? repository : QString();
}
Release parseRelease(const QByteArray &json, const QString &repository, bool previews) {
    if (!validRepository(repository) || json.size() > 512 * 1024)
        return {};
    const auto o = QJsonDocument::fromJson(json).object();
    const auto tag = o.value("tag_name").toString();
    const QString version = tag.startsWith('v') ? tag.mid(1) : tag;
    if (o.value("draft").toBool(true) || !o.value("prerelease").isBool() ||
        (o.value("prerelease").toBool() && !previews) || !validVersion(version))
        return {};
    const QUrl expected("https://github.com/" + repository + "/releases/tag/" + tag);
    if (QUrl(o.value("html_url").toString(), QUrl::StrictMode) != expected)
        return {};
    Release result;
    result.version = version;
    result.url = expected;
    const QString name = assetName(version);
    int found = 0;
    for (const auto &entry : o.value("assets").toArray()) {
        const auto asset = entry.toObject();
        if (asset.value("name").toString() != name)
            continue;
        ++found;
        const QUrl download("https://github.com/" + repository + "/releases/download/" + tag + "/" + name);
        const auto digest = asset.value("digest").toString();
        const auto size = asset.value("size").toInteger();
        if (asset.value("state").toString() != "uploaded" || size <= 0 || size > maxInstaller ||
            QUrl(asset.value("browser_download_url").toString(), QUrl::StrictMode) != download ||
            !QRegularExpression("^sha256:[a-fA-F0-9]{64}$").match(digest).hasMatch())
            continue;
        result.asset = download;
        result.sha256 = digest.mid(7).toLatin1().toLower();
        result.size = size;
    }
    if (found != 1) {
        result.asset = {};
        result.sha256.clear();
        result.size = 0;
    }
    return result;
}
Release selectRelease(const QByteArray &json, const QString &repository, bool previews) {
    if (json.size() > maxMetadata)
        return {};
    const auto doc = QJsonDocument::fromJson(json);
    if (doc.isObject())
        return parseRelease(json, repository, previews);
    Release result;
    for (const auto &entry : doc.array()) {
        auto candidate = parseRelease(QJsonDocument(entry.toObject()).toJson(), repository, previews);
        if (QVersionNumber::fromString(candidate.version) > QVersionNumber::fromString(result.version))
            result = std::move(candidate);
    }
    return result;
}
bool allowedAssetRedirect(const QUrl &url, const QUrl &original) {
    if (!url.isValid() || url.scheme() != "https" || !url.userInfo().isEmpty() || url.hasFragment() ||
        (url.port() != -1 && url.port() != 443))
        return false;
    if (url.host() == "github.com")
        return url == original;
    return url.host() == "release-assets.githubusercontent.com" ||
           url.host() == "objects.githubusercontent.com";
}
bool verifyUpdateFile(const QString &path, const Release &release) {
    QFile file(path);
    if (!release.installable() || !file.open(QIODevice::ReadOnly) || file.size() != release.size)
        return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) && hash.result().toHex() == release.sha256;
}
struct UpdateChecker::Download {
    QTemporaryDir directory;
    QSaveFile file;
    QCryptographicHash hash{QCryptographicHash::Sha256};
    qint64 received = 0;
    bool failed = false;
    Download(const QString &base, const QString &name)
        : directory(base + "/update-XXXXXX"), file(directory.path() + "/" + name) {}
};
UpdateChecker::UpdateChecker(QString directory, QObject *parent, QNetworkAccessManager *network)
    : QObject(parent), directory_(std::move(directory)),
      repository_(QStringLiteral(STREAMBRIDGE_GITHUB_REPOSITORY)),
      network_(network ? network : new QNetworkAccessManager(this)), timer_(this) {
    const auto o = readJson(directory_ + "/updates.json");
    if (o.contains("repository"))
        repository_ = normalizeRepository(o.value("repository").toString());
    automatic_ = o.value("automatic").toBool(false);
    autoDownload_ = o.value("autoDownload").toBool(false);
    previews_ = o.value("previews").toBool(false);
    lastAttempt_ = static_cast<qint64>(o.value("lastAttempt").toDouble());
    if (!available())
        state_ = "UpdateUnconfigured";
    restoreReady();
    connect(&timer_, &QTimer::timeout, this, [this] {
        if (state_ == "UpdateScheduled") {
            QSettings result(QFileInfo(readyFile_).path() + "/update-result.ini", QSettings::IniFormat);
            if (result.value("Update/Status").toString() == "failed" || !updateInstallerRunning(readyFile_)) {
                state_ = "UpdateInstallFailed";
                emit changed();
            }
        }
        if (automatic_ && !busy() && readyFile_.isEmpty() &&
            QDateTime::currentSecsSinceEpoch() - lastAttempt_ >= 86400)
            check();
    });
    timer_.start(60000);
    QTimer::singleShot(5000, this, [this] {
        if (automatic_ && readyFile_.isEmpty() && QDateTime::currentSecsSinceEpoch() - lastAttempt_ >= 86400)
            check();
    });
}
UpdateChecker::~UpdateChecker() {
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
        reply_->deleteLater();
    }
}
bool UpdateChecker::available() const {
    return validRepository(repository_);
}
bool UpdateChecker::save() {
    QDir().mkpath(directory_);
    return writeJson(directory_ + "/updates.json", {{"repository", repository_},
                                                    {"automatic", automatic_},
                                                    {"autoDownload", autoDownload_},
                                                    {"previews", previews_},
                                                    {"lastAttempt", lastAttempt_}});
}
bool UpdateChecker::configure(QString repository, bool automatic, bool autoDownload, bool previews) {
    if (busy() || state_ == "UpdateScheduled")
        return false;
    repository = normalizeRepository(repository);
    if (repository.isEmpty())
        return false;
    const auto oldRepository = repository_;
    const bool oldAutomatic = automatic_, oldDownload = autoDownload_, oldPreviews = previews_;
    repository_ = repository;
    automatic_ = automatic;
    autoDownload_ = autoDownload;
    previews_ = previews;
    if (!save()) {
        repository_ = oldRepository;
        automatic_ = oldAutomatic;
        autoDownload_ = oldDownload;
        previews_ = oldPreviews;
        return false;
    }
    if (repository_ != oldRepository || previews_ != oldPreviews) {
        discardReady();
        release_ = {};
        state_ = "UpdateIdle";
        lastAttempt_ = 0;
        save();
    } else if (state_ == "UpdateUnconfigured") {
        state_ = "UpdateIdle";
    }
    emit changed();
    return true;
}
bool UpdateChecker::setAutomatic(bool value) {
    return configure(repository_, value, autoDownload_, previews_);
}
bool UpdateChecker::canDownload() const {
    return !busy() && readyFile_.isEmpty() && release_.installable() &&
           QVersionNumber::fromString(release_.version) > QVersionNumber::fromString(STREAMBRIDGE_VERSION);
}
void UpdateChecker::check() {
    if (!available() || busy() || !readyFile_.isEmpty())
        return;
    cancelled_ = false;
    state_ = "UpdateChecking";
    release_ = {};
    lastAttempt_ = QDateTime::currentSecsSinceEpoch();
    save();
    const QString suffix = previews_ ? "/releases?per_page=25" : "/releases/latest";
    auto request = requestFor(QUrl("https://api.github.com/repos/" + repository_ + suffix));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2026-03-10");
    auto *reply = network_->get(request);
    reply_ = reply;
    reply->setReadBufferSize(64 * 1024);
    auto data = std::make_shared<QByteArray>();
    const auto read = [reply, data] {
        data->append(reply->readAll());
        if (data->size() > maxMetadata)
            reply->abort();
    };
    connect(reply, &QNetworkReply::readyRead, this, read);
    connect(reply, &QNetworkReply::finished, this, [this, reply, data, read] {
        read();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto document = QJsonDocument::fromJson(*data);
        const bool emptyList = status == 200 && reply->error() == QNetworkReply::NoError &&
                               document.isArray() && document.array().isEmpty();
        if (reply->error() == QNetworkReply::NoError && status == 200)
            release_ = selectRelease(*data, repository_, previews_);
        state_ =
            cancelled_                   ? "UpdateCancelled"
            : status == 404 || emptyList ? "UpdateNoRelease"
            : release_.url.isEmpty()     ? "UpdateFailed"
            : QVersionNumber::fromString(release_.version) <= QVersionNumber::fromString(STREAMBRIDGE_VERSION)
                ? "UpdateCurrent"
            : release_.installable() ? "UpdateAvailable"
                                     : "UpdateNoPackage";
        reply_ = nullptr;
        reply->deleteLater();
        emit changed();
        if (autoDownload_ && canDownload())
            download();
    });
    emit changed();
}
void UpdateChecker::download() {
    if (!canDownload())
        return;
    QDir().mkpath(directory_ + "/updates");
    download_ = std::make_unique<Download>(directory_ + "/updates", assetName(release_.version));
    if (!download_->directory.isValid() || !download_->file.open(QIODevice::WriteOnly)) {
        failDownload("UpdateDownloadFailed");
        return;
    }
    state_ = "UpdateDownloading";
    progress_ = 0;
    cancelled_ = false;
    fetchAsset(release_.asset, 0);
}
void UpdateChecker::fetchAsset(QUrl url, int redirects) {
    auto *reply = network_->get(requestFor(url));
    reply_ = reply;
    reply->setReadBufferSize(64 * 1024);
    const auto read = [this, reply] {
        const auto bytes = reply->readAll();
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
            return;
        auto &d = *download_;
        d.received += bytes.size();
        if (d.received > release_.size || d.file.write(bytes) != bytes.size()) {
            d.failed = true;
            reply->abort();
            return;
        }
        d.hash.addData(bytes);
        progress_ = static_cast<int>(100 * d.received / release_.size);
        emit changed();
    };
    connect(reply, &QNetworkReply::readyRead, this, read);
    connect(reply, &QNetworkReply::finished, this, [this, reply, redirects, read] {
        read();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QUrl redirect =
            reply->url().resolved(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply_ = nullptr;
        reply->deleteLater();
        if (cancelled_) {
            failDownload("UpdateCancelled");
            return;
        }
        if (ok && status >= 300 && status < 400 && redirects < 3 &&
            allowedAssetRedirect(redirect, release_.asset)) {
            fetchAsset(redirect, redirects + 1);
            return;
        }
        auto &d = *download_;
        if (!ok || status != 200 || d.failed || d.received != release_.size) {
            failDownload("UpdateDownloadFailed");
            return;
        }
        if (d.hash.result().toHex() != release_.sha256) {
            failDownload("UpdateVerifyFailed");
            return;
        }
        if (!d.file.commit()) {
            failDownload("UpdateDownloadFailed");
            return;
        }
        readyFile_ = d.file.fileName();
        if (!saveReady()) {
            readyFile_.clear();
            failDownload("SaveFailed");
            return;
        }
        d.directory.setAutoRemove(false);
        download_.reset();
        state_ = "UpdateReady";
        emit changed();
    });
    emit changed();
}
void UpdateChecker::failDownload(const char *state) {
    download_.reset();
    state_ = state;
    emit changed();
}
void UpdateChecker::cancel() {
    if (reply_) {
        cancelled_ = true;
        reply_->abort();
    }
}
bool UpdateChecker::saveReady() {
    return writeJson(directory_ + "/pending-update.json",
                     {{"repository", repository_},
                      {"file", QDir(directory_ + "/updates").relativeFilePath(readyFile_)},
                      {"version", release_.version},
                      {"url", release_.url.toString()},
                      {"asset", release_.asset.toString()},
                      {"sha256", QString::fromLatin1(release_.sha256)},
                      {"size", release_.size}});
}
void UpdateChecker::restoreReady() {
    const auto o = readJson(directory_ + "/pending-update.json");
    const auto relative = o.value("file").toString();
    const auto version = o.value("version").toString();
    if (o.value("repository").toString() != repository_ || !validVersion(version) ||
        !QRegularExpression("^update-[A-Za-z0-9]{6}/obs-streambridge-[0-9.]+-windows-x64-setup\\.exe$")
             .match(relative)
             .hasMatch() ||
        QFileInfo(relative).fileName() != assetName(version))
        return;
    const auto base = QFileInfo(directory_ + "/updates").canonicalFilePath();
    const auto path = QFileInfo(directory_ + "/updates/" + relative).canonicalFilePath();
    if (base.isEmpty() || !path.startsWith(base + "/", Qt::CaseInsensitive))
        return;
    Release restored;
    restored.version = version;
    restored.url = QUrl(o.value("url").toString());
    restored.asset = QUrl(o.value("asset").toString());
    restored.sha256 = o.value("sha256").toString().toLatin1();
    restored.size = o.value("size").toInteger();
    readyFile_ = path;
    if (QVersionNumber::fromString(version) <= QVersionNumber::fromString(STREAMBRIDGE_VERSION)) {
        discardReady();
        state_ = "UpdateInstalled";
        return;
    }
    // Require cached URLs to still refer to this exact repository, version and asset.
    const QUrl tagUrl("https://github.com/" + repository_ + "/releases/tag/v" + version);
    const QUrl plainTagUrl("https://github.com/" + repository_ + "/releases/tag/" + version);
    const QString tag = restored.url == tagUrl ? "v" + version : version;
    if ((restored.url != tagUrl && restored.url != plainTagUrl) ||
        restored.asset != QUrl("https://github.com/" + repository_ + "/releases/download/" + tag + "/" +
                               assetName(version)) ||
        !verifyUpdateFile(path, restored)) {
        discardReady();
        state_ = "UpdateVerifyFailed";
        return;
    }
    release_ = restored;
    QSettings result(QFileInfo(path).path() + "/update-result.ini", QSettings::IniFormat);
    const auto status = result.value("Update/Status").toString();
    state_ = updateInstallerRunning(path) ? "UpdateScheduled"
             : status == "failed"         ? "UpdateInstallFailed"
                                          : "UpdateReady";
}
void UpdateChecker::discardReady() {
    if (!readyFile_.isEmpty()) {
        // Only a validated cache entry is ever assigned to readyFile_.
        QFile::remove(readyFile_);
        const auto dir = QFileInfo(readyFile_).path();
        QFile::remove(dir + "/update-result.ini");
        QFile::remove(dir + "/restart-result.ini");
        for (const auto &helper : QDir(dir).entryList({"restart-*.exe"}, QDir::Files))
            QFile::remove(dir + "/" + helper);
        QDir().rmdir(dir);
    }
    readyFile_.clear();
    QFile::remove(directory_ + "/pending-update.json");
}
bool UpdateChecker::schedule() {
    if (readyFile_.isEmpty() || busy() || state_ == "UpdateScheduled")
        return false;
    QString error;
    if (!launchUpdate(readyFile_, release_, error)) {
        state_ = error;
        emit changed();
        return false;
    }
    state_ = "UpdateScheduled";
    emit changed();
    return true;
}
bool UpdateChecker::scheduleRestart() {
    if (!ready())
        return false;
    if (!restartGuard_ || !restartGuard_()) {
        state_ = "UpdateStopOutputs";
        emit changed();
        return false;
    }
    QString error;
    if (!launchUpdate(readyFile_, release_, error, &restartCancellation_)) {
        state_ = error;
        emit changed();
        return false;
    }
    // Elevation can leave the application responsive long enough to start an output.
    if (!restartGuard_()) {
        cancelRestart();
        return false;
    }
    state_ = "UpdateScheduled";
    emit changed();
    emit restartRequested();
    return true;
}
void UpdateChecker::cancelRestart() {
    cancelUpdateRestart(restartCancellation_);
    restartCancellation_.clear();
    state_ = "UpdateRestartCancelled";
    emit changed();
}
static std::unique_ptr<UpdateChecker> checker;
UpdateChecker *updateChecker() {
    return checker.get();
}
void initializeUpdates(const QString &directory) {
    checker = std::make_unique<UpdateChecker>(directory);
}
void shutdownUpdates() {
    checker.reset();
}
} // namespace sb
