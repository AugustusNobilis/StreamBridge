// SPDX-License-Identifier: GPL-2.0-or-later
#include "updates/checker.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QThread>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
static void check(bool value, const char *message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
struct Response {
    int status = 200;
    QByteArray data;
    QUrl redirect;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
};
class Reply final : public QNetworkReply {
  public:
    Reply(const QNetworkRequest &request, Response response, QObject *parent)
        : QNetworkReply(parent), data_(std::move(response.data)) {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
        if (!response.redirect.isEmpty())
            setAttribute(QNetworkRequest::RedirectionTargetAttribute, response.redirect);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this, error = response.error] {
            if (isFinished())
                return;
            if (error != NoError)
                setError(error, "fixture network error");
            emit readyRead();
            if (isFinished())
                return;
            setFinished(true);
            emit finished();
        });
    }
    void abort() override {
        if (isFinished())
            return;
        setError(OperationCanceledError, "cancelled");
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override { return data_.size() - offset_ + QIODevice::bytesAvailable(); }

  protected:
    qint64 readData(char *target, qint64 maximum) override {
        const qint64 count = std::min(maximum, qint64(data_.size()) - offset_);
        if (!count)
            return -1;
        std::memcpy(target, data_.constData() + offset_, static_cast<size_t>(count));
        offset_ += count;
        return count;
    }

  private:
    QByteArray data_;
    qint64 offset_ = 0;
};
class Network final : public QNetworkAccessManager {
  public:
    std::deque<Response> responses;
    QList<QUrl> requests;

  protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *) override {
        check(operation == GetOperation, "updater only makes GET requests");
        check(!responses.empty(), "unexpected request");
        requests.append(request.url());
        auto response = std::move(responses.front());
        responses.pop_front();
        return new Reply(request, std::move(response), this);
    }
};
static void finish(sb::UpdateChecker &checker) {
    QElapsedTimer time;
    time.start();
    while (checker.busy() && time.elapsed() < 3000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    check(!checker.busy(), "asynchronous operation completed");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString repository = "example/streambridge";
    const QByteArray payload("MZ synthetic update fixture, never executable");
    const auto digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    const QString assetUrl = "https://github.com/example/streambridge/releases/download/v1.2.3/"
                             "obs-streambridge-1.2.3-windows-x64-setup.exe";
    QJsonObject asset{{"name", "obs-streambridge-1.2.3-windows-x64-setup.exe"},
                      {"state", "uploaded"},
                      {"size", payload.size()},
                      {"digest", "sha256:" + QString::fromLatin1(digest)},
                      {"browser_download_url", assetUrl}};
    QJsonObject release{{"tag_name", "v1.2.3"},
                        {"draft", false},
                        {"prerelease", false},
                        {"html_url", "https://github.com/example/streambridge/releases/tag/v1.2.3"},
                        {"assets", QJsonArray{asset}}};
    auto json = [&] { return QJsonDocument(release).toJson(); };
    auto parse = [&] { return sb::parseRelease(json(), repository); };
    check(parse().installable() && parse().version == "1.2.3", "stable package accepted");
    check(sb::normalizeRepository("https://github.com/example/streambridge.git") == repository,
          "repository URL normalized");
    check(sb::normalizeRepository("https://github.com.evil.invalid/example/streambridge").isEmpty(),
          "foreign repository rejected");
    for (const auto &url : {"http://github.com/example/streambridge/releases/tag/v1.2.3",
                            "https://github.com.evil.invalid/example/streambridge/releases/tag/v1.2.3",
                            "https://github.com/example/other/releases/tag/v1.2.3",
                            "https://user@github.com/example/streambridge/releases/tag/v1.2.3",
                            "https://github.com/example/streambridge/releases/tag/v1.2.3?redirect=evil"}) {
        release["html_url"] = url;
        check(parse().url.isEmpty(), "foreign release rejected");
    }
    release["html_url"] = "https://github.com/example/streambridge/releases/tag/v1.2.3";
    release["draft"] = true;
    check(parse().url.isEmpty(), "draft rejected");
    release["draft"] = false;
    release["prerelease"] = true;
    check(parse().url.isEmpty(), "preview opt-in required");
    check(sb::parseRelease(json(), repository, true).installable(), "preview explicitly accepted");
    release["prerelease"] = false;
    release["assets"] = QJsonArray{asset, asset};
    check(!parse().installable(), "duplicate assets rejected");
    release["assets"] = QJsonArray{QJsonObject{{"name", asset["name"]},
                                               {"state", "uploaded"},
                                               {"size", payload.size()},
                                               {"browser_download_url", assetUrl}}};
    check(!parse().installable(), "missing digest rejected");
    release["assets"] = QJsonArray{asset};
    check(sb::parseRelease("{}", "../evil").url.isEmpty(), "invalid repository rejected");
    check(sb::parseRelease(QByteArray(512 * 1024 + 1, 'x'), repository).url.isEmpty(),
          "oversized metadata rejected");
    check(sb::selectRelease(QJsonDocument(QJsonArray{release}).toJson(), repository, false).installable(),
          "release list accepted");
    const QUrl original(assetUrl);
    check(sb::allowedAssetRedirect(QUrl("https://release-assets.githubusercontent.com/test?signature=x"),
                                   original),
          "GitHub signed CDN accepted");
    for (const auto &url :
         {"http://release-assets.githubusercontent.com/test", "https://evil.invalid/test",
          "https://release-assets.githubusercontent.com.evil.invalid/test",
          "https://user@objects.githubusercontent.com/test", "https://github.com/foreign/repo",
          "https://objects.githubusercontent.com:444/test"}) {
        check(!sb::allowedAssetRedirect(QUrl(url), original), "unsafe redirect rejected");
    }
    QTemporaryDir directory;
    Network network;
    {
        sb::UpdateChecker checker(directory.path(), nullptr, &network);
        check(!checker.automatic() && !checker.autoDownload() && !checker.previews(),
              "network automation defaults off");
        check(checker.configure(repository, true, false, false), "preferences saved");
        network.responses.push_back({200, json()});
        checker.check();
        finish(checker);
        check(checker.canDownload(), "new version downloadable");
        network.responses.push_back(
            {302, {}, QUrl("https://release-assets.githubusercontent.com/package?sig=fixture")});
        network.responses.push_back({200, payload});
        checker.download();
        finish(checker);
        check(checker.ready() && checker.progress() == 100, "redirected package downloaded and verified");
        check(sb::verifyUpdateFile(checker.readyFile(), parse()), "cached package exact hash");
        check(!checker.schedule() && checker.state() == "UpdateWrongHost",
              "installation cannot run outside OBS");
    }
    {
        sb::UpdateChecker checker(directory.path(), nullptr, &network);
        check(checker.automatic() && checker.ready(), "opt-in and verified download survive restart");
        QFile corrupt(checker.readyFile());
        check(corrupt.open(QIODevice::WriteOnly), "open synthetic cache");
        corrupt.write("corrupt");
        corrupt.close();
    }
    {
        sb::UpdateChecker checker(directory.path(), nullptr, &network);
        check(!checker.ready() && checker.state() == "UpdateVerifyFailed",
              "tampered cache rejected on restart");
    }
    auto downloadCase = [&](Response response, const QString &expected) {
        QTemporaryDir temp;
        Network net;
        sb::UpdateChecker checker(temp.path(), nullptr, &net);
        check(checker.configure(repository, false, false, false), "fixture configured");
        net.responses.push_back({200, json()});
        checker.check();
        finish(checker);
        net.responses.push_back(std::move(response));
        checker.download();
        finish(checker);
        check(!checker.ready() && checker.state() == expected, "bad download never ready");
        check(checker.canDownload(), "failed download can be retried");
    };
    downloadCase({200, QByteArray(payload.size(), 'x')}, "UpdateVerifyFailed");
    downloadCase({200, payload.left(4)}, "UpdateDownloadFailed");
    downloadCase({200, payload + "oversize"}, "UpdateDownloadFailed");
    downloadCase({200, {}, {}, QNetworkReply::RemoteHostClosedError}, "UpdateDownloadFailed");
    downloadCase({302, {}, QUrl("https://evil.invalid/file")}, "UpdateDownloadFailed");
    {
        QTemporaryDir temp;
        Network net;
        sb::UpdateChecker checker(temp.path(), nullptr, &net);
        checker.configure(repository, true, true, true);
        net.responses.push_back({200, QJsonDocument(QJsonArray{release}).toJson()});
        net.responses.push_back({200, payload});
        checker.check();
        finish(checker);
        check(checker.ready(), "opted-in automatic download completes");
        check(net.requests.front().query() == "per_page=25", "preview uses published release list");
    }
    {
        QTemporaryDir temp;
        Network net;
        sb::UpdateChecker checker(temp.path(), nullptr, &net);
        checker.configure(repository, false, false, false);
        net.responses.push_back({200, json()});
        checker.check();
        checker.cancel();
        finish(checker);
        check(checker.state() == "UpdateCancelled", "check cancellation");
        net.responses.push_back({404, "{}"});
        checker.check();
        finish(checker);
        check(checker.state() == "UpdateNoRelease", "empty repository handled");
        net.responses.push_back({200, "[]"});
        checker.check();
        finish(checker);
        check(checker.state() == "UpdateNoRelease", "empty preview channel handled");
    }
    std::cout << "PASS: release boundaries, redirects, streaming integrity, interruption, opt-in and restart "
                 "recovery\n";
}
