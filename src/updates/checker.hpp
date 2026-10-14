// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <memory>
namespace sb {
struct Release {
    QString version;
    QUrl url, asset;
    QByteArray sha256;
    qint64 size = 0;
    bool installable() const { return !asset.isEmpty() && sha256.size() == 64 && size > 0; }
};
QString normalizeRepository(QString repository);
Release parseRelease(const QByteArray &json, const QString &repository, bool previews = false);
Release selectRelease(const QByteArray &json, const QString &repository, bool previews);
bool allowedAssetRedirect(const QUrl &url, const QUrl &original);
bool verifyUpdateFile(const QString &path, const Release &release);
class UpdateChecker final : public QObject {
    Q_OBJECT
  public:
    explicit UpdateChecker(QString directory, QObject *parent = nullptr,
                           QNetworkAccessManager *network = nullptr);
    ~UpdateChecker() override;
    bool available() const;
    bool automatic() const { return automatic_; }
    bool autoDownload() const { return autoDownload_; }
    bool previews() const { return previews_; }
    QString repository() const { return repository_; }
    bool configure(QString repository, bool automatic, bool autoDownload, bool previews);
    bool setAutomatic(bool value);
    void check();
    void download();
    void cancel();
    bool schedule();
    bool busy() const { return !reply_.isNull(); }
    bool canDownload() const;
    bool ready() const { return !readyFile_.isEmpty() && !busy() && state_ != "UpdateScheduled"; }
    int progress() const { return progress_; }
    QString version() const { return release_.version; }
    QString readyFile() const { return readyFile_; }
    QString state() const { return state_; }
    QUrl releaseUrl() const { return release_.url; }
  signals:
    void changed();

  private:
    struct Download;
    bool save();
    bool saveReady();
    void restoreReady();
    void discardReady();
    void fetchAsset(QUrl url, int redirects);
    void failDownload(const char *state);
    QString directory_, repository_, state_ = "UpdateIdle", readyFile_;
    QNetworkAccessManager *network_;
    QPointer<QNetworkReply> reply_;
    QTimer timer_;
    Release release_;
    std::unique_ptr<Download> download_;
    bool automatic_ = false, autoDownload_ = false, previews_ = false, cancelled_ = false;
    int progress_ = 0;
    qint64 lastAttempt_ = 0;
};
UpdateChecker *updateChecker();
void initializeUpdates(const QString &directory);
void shutdownUpdates();
} // namespace sb
