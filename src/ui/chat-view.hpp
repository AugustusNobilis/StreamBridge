// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "platform/hub.hpp"
#include <QCache>
#include <QImage>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QQueue>
#include <QSet>
namespace sb {
bool allowedChatImage(const QUrl &url);
QString chatBodyHtml(const ChatMessage &message, const QHash<QString, QString> &bttv);
QString chatRoleName(const QString &badge);
class ChatMedia : public QObject {
    Q_OBJECT
  public:
    explicit ChatMedia(QObject *parent = nullptr);
    ~ChatMedia() override;
    QImage image(const QUrl &url);
    bool cacheImage(const QUrl &url, const QByteArray &bytes);
    bool ready(const QUrl &url) const { return cache_.contains(url.toString()); }
    void reloadCatalogs();
    void setChannel(const QString &channel);
    void setBadges(const QJsonObject &catalog);
    QString badgeUrl(const QString &badge) const;
    const QHash<QString, QString> &emotes() const { return emotes_; }
  signals:
    void changed();
    void catalogChanged();

  private:
    struct Asset;
    void fetch(const QUrl &url, bool catalog = false, unsigned generation = 0);
    void next();
    QNetworkAccessManager network_;
    QCache<QString, Asset> cache_{16384};
    QSet<QString> pending_, failed_;
    struct Request {
        QUrl url;
        bool catalog;
        unsigned generation;
    };
    QQueue<Request> queue_;
    QHash<QString, QString> emotes_, badges_;
    QString channel_;
    unsigned generation_ = 0;
    int active_ = 0;
};
class ChatView : public QListWidget {
    Q_OBJECT
  public:
    explicit ChatView(ChatMedia &media, QWidget *parent);
    void append(const ChatMessage &message);
  signals:
    void authorClicked(sb::ChatMessage message);

  protected:
    void resizeEvent(QResizeEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

  private:
    class Delegate;
    Delegate *delegate_;
};
} // namespace sb
Q_DECLARE_METATYPE(sb::ChatMessage)
