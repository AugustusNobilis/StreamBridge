// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat-view.hpp"
#include "locale.hpp"
#include "platform-docks.hpp"
#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QMovie>
#include <QNetworkReply>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QTimer>
#include <algorithm>
#include <cmath>
namespace sb {
bool allowedChatImage(const QUrl &url) {
    if (!url.isValid() || url.scheme() != "https" || !url.userInfo().isEmpty() ||
        (url.port() != -1 && url.port() != 443) || !url.fragment().isEmpty() || url.toString().size() > 2048)
        return false;
    const auto host = url.host().toLower();
    return host == "static-cdn.jtvnw.net" || host == "cdn.betterttv.net" || host == "media.giphy.com" ||
           host == "i.giphy.com" || host == "media.tenor.com" ||
           QRegularExpression("^media[0-4]\\.giphy\\.com$").match(host).hasMatch();
}
static QString imageHtml(const QString &url, const QString &alt, int size = 28) {
    return QString("<img src=\"%1\" alt=\"%2\" width=\"%3\" height=\"%3\" />")
        .arg(url.toHtmlEscaped(), alt.toHtmlEscaped())
        .arg(size);
}
QString chatBodyHtml(const ChatMessage &m, const QHash<QString, QString> &bttv) {
    const auto unicode = m.text.toStdU32String();
    const int unicodeLength = static_cast<int>(unicode.size());
    struct Span {
        int start, end;
        QString id;
    };
    QList<Span> spans;
    if (m.platform == 1)
        for (const auto &group : m.emotes.split('/')) {
            const auto id = group.section(':', 0, 0);
            if (!QRegularExpression("^[A-Za-z0-9_-]{1,128}$").match(id).hasMatch())
                continue;
            for (const auto &range : group.section(':', 1).split(',')) {
                const auto match = QRegularExpression("^(\\d{1,5})-(\\d{1,5})$").match(range);
                if (!match.hasMatch())
                    continue;
                const int start = match.captured(1).toInt(), end = match.captured(2).toInt();
                if (start <= end && end < unicodeLength && spans.size() < 128)
                    spans.append({start, end, id});
            }
        }
    std::sort(spans.begin(), spans.end(), [](const Span &a, const Span &b) { return a.start < b.start; });
    const auto plain = [&bttv, &m](const QString &value) {
        QString result;
        auto tokens = QRegularExpression("\\s+|\\S+").globalMatch(value);
        while (tokens.hasNext()) {
            const auto token = tokens.next().captured();
            const QUrl url(token);
            if (m.platform == 1 && bttv.contains(token))
                result += imageHtml(bttv.value(token), token);
            else if (token.startsWith("https://") && url.path().endsWith(".gif", Qt::CaseInsensitive) &&
                     allowedChatImage(url))
                result += imageHtml(url.toString(), "GIF", 64);
            else
                result += token.toHtmlEscaped().replace('\n', "<br/>");
        }
        return result;
    };
    QString result;
    int position = 0;
    for (const auto &span : spans) {
        if (span.start < position)
            continue;
        result += plain(QString::fromUcs4(unicode.data() + position, span.start - position));
        const auto code = QString::fromUcs4(unicode.data() + span.start, span.end - span.start + 1);
        result +=
            imageHtml("https://static-cdn.jtvnw.net/emoticons/v2/" + span.id + "/default/dark/1.0", code);
        position = span.end + 1;
    }
    result += plain(QString::fromUcs4(unicode.data() + position, unicodeLength - position));
    return result;
}
QString chatRoleName(const QString &badge) {
    const auto role = badge.section('/', 0, 0);
    if (role == "owner" || role == "broadcaster")
        return text("RoleOwner");
    if (role == "moderator")
        return text("RoleModerator");
    if (role == "vip")
        return "VIP";
    if (role == "member" || role == "subscriber" || role == "founder")
        return text("RoleMember");
    if (role == "verified" || role == "partner")
        return text("RoleVerified");
    return role;
}
struct ChatMedia::Asset {
    QBuffer buffer;
    QMovie movie;
    QImage still;
    Asset() : movie(&buffer) {}
};
ChatMedia::ChatMedia(QObject *parent) : QObject(parent), network_(this) {
}
ChatMedia::~ChatMedia() {
    for (auto *reply : network_.findChildren<QNetworkReply *>()) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
    }
}
QImage ChatMedia::image(const QUrl &url) {
    const auto key = url.toString();
    if (auto *asset = cache_.object(key)) {
        const auto frame = asset->movie.currentImage();
        return frame.isNull() ? asset->still : frame;
    }
    if (allowedChatImage(url) && !failed_.contains(key))
        fetch(url);
    QImage placeholder(28, 28, QImage::Format_ARGB32_Premultiplied);
    placeholder.fill(Qt::transparent);
    return placeholder;
}
void ChatMedia::fetch(const QUrl &url, bool catalog, unsigned generation) {
    const auto key = url.toString() + (catalog ? "#" + QString::number(generation) : QString());
    if (pending_.contains(key) || queue_.size() >= 128)
        return;
    pending_.insert(key);
    queue_.enqueue({url, catalog, generation});
    next();
}
void ChatMedia::next() {
    while (active_ < 4 && !queue_.isEmpty()) {
        const auto job = queue_.dequeue();
        ++active_;
        QNetworkRequest request(job.url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(10000);
        request.setRawHeader("User-Agent", "OBS-StreamBridge/0.2.5");
        auto *reply = network_.get(request);
        const qint64 limit = job.catalog ? 1024 * 1024 : 2 * 1024 * 1024;
        reply->setReadBufferSize(limit + 1);
        connect(reply, &QNetworkReply::readyRead, reply, [reply, limit] {
            if (reply->bytesAvailable() > limit)
                reply->abort();
        });
        connect(reply, &QNetworkReply::metaDataChanged, reply, [reply, limit] {
            if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > limit)
                reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, job, limit] {
            --active_;
            const auto key = job.url.toString();
            pending_.remove(key + (job.catalog ? "#" + QString::number(job.generation) : QString()));
            const auto bytes = reply->readAll();
            const bool ok = reply->error() == QNetworkReply::NoError &&
                            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
                            bytes.size() <= limit;
            reply->deleteLater();
            if (ok && job.catalog && job.generation == generation_) {
                const auto json = QJsonDocument::fromJson(bytes);
                auto data = json.isArray() ? json.array() : json.object()["channelEmotes"].toArray();
                for (const auto &value : json.object()["sharedEmotes"].toArray())
                    data.append(value);
                for (const auto &value : data) {
                    const auto item = value.toObject();
                    const auto id = item["id"].toString(), code = item["code"].toString();
                    if (emotes_.size() < 4000 && code.size() <= 128 && !code.isEmpty() &&
                        QRegularExpression("^[a-fA-F0-9]{24}$").match(id).hasMatch())
                        emotes_[code] = "https://cdn.betterttv.net/emote/" + id + "/2x";
                }
                emit catalogChanged();
            } else if (ok && !job.catalog) {
                if (!cacheImage(job.url, bytes))
                    failed_.insert(key);
            } else if (!job.catalog)
                failed_.insert(key);
            if (failed_.size() > 1000)
                failed_.clear();
            next();
        });
    }
}
bool ChatMedia::cacheImage(const QUrl &url, const QByteArray &bytes) {
    if (!allowedChatImage(url) || bytes.size() > 2 * 1024 * 1024)
        return false;
    auto asset = std::make_unique<Asset>();
    asset->buffer.setData(bytes);
    asset->buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&asset->buffer);
    const auto size = reader.size();
    const auto format = reader.format();
    if (!size.isValid() || size.width() > 512 || size.height() > 512 || (format != "png" && format != "gif"))
        return false;
    const bool animated = reader.supportsAnimation();
    reader.setScaledSize(size.scaled(64, 64, Qt::KeepAspectRatio));
    asset->still = reader.read();
    if (asset->still.isNull())
        return false;
    asset->buffer.seek(0);
    asset->movie.setFormat(format);
    asset->movie.setScaledSize(size.scaled(64, 64, Qt::KeepAspectRatio));
    asset->movie.setCacheMode(QMovie::CacheNone);
    if (animated) {
        connect(&asset->movie, &QMovie::frameChanged, this, [this] { emit changed(); });
        asset->movie.start();
    }
    cache_.insert(url.toString(), asset.release(), static_cast<int>(bytes.size() / 1024) + 192);
    emit catalogChanged();
    return true;
}
void ChatMedia::reloadCatalogs() {
    const auto channel = channel_;
    channel_.clear();
    failed_.clear();
    setChannel(channel);
}
void ChatMedia::setChannel(const QString &channel) {
    if (channel_ == channel)
        return;
    channel_ = channel;
    ++generation_;
    emotes_.clear();
    badges_.clear();
    emit catalogChanged();
    if (!QRegularExpression("^[0-9]{1,32}$").match(channel).hasMatch())
        return;
    fetch(QUrl("https://api.betterttv.net/3/cached/emotes/global"), true, generation_);
    fetch(QUrl("https://api.betterttv.net/3/cached/users/twitch/" + channel), true, generation_);
}
void ChatMedia::setBadges(const QJsonObject &catalog) {
    badges_.clear();
    for (const auto &set : catalog["data"].toArray()) {
        const auto data = set.toObject();
        for (const auto &version : data["versions"].toArray()) {
            const auto v = version.toObject();
            const auto url = v["image_url_2x"].toString();
            if (badges_.size() < 2000 && allowedChatImage(QUrl(url)))
                badges_[data["set_id"].toString() + "/" + v["id"].toString()] = url;
        }
    }
    emit catalogChanged();
}
QString ChatMedia::badgeUrl(const QString &badge) const {
    return badges_.value(badge);
}
// YouTube exposes role flags, but no badge artwork. Match the platform's wrench,
// verification check and membership-star vocabulary; Twitch uses its actual CDN badges.
static QImage roleImage(const QString &role) {
    QImage image(20, 20, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(role == "owner"       ? QColor("#e3b95f")
                  : role == "moderator" ? QColor("#7aa9e2")
                                        : QColor("#78bda0"),
                  2.5, Qt::SolidLine, Qt::RoundCap));
    if (role == "moderator") {
        p.drawLine(4, 16, 12, 8);
        p.drawArc(QRectF(10, 1, 8, 8), 70 * 16, 260 * 16);
    } else if (role == "verified" || role == "partner") {
        p.drawEllipse(QRectF(2, 2, 16, 16));
        p.drawPolyline(QPolygonF{{5, 10}, {9, 14}, {15, 6}});
    } else if (role == "owner" || role == "broadcaster") {
        p.drawPolygon(QPolygonF{{3, 6}, {7, 10}, {10, 3}, {13, 10}, {17, 6}, {15, 16}, {5, 16}});
    } else {
        p.drawPolygon(QPolygonF{
            {10, 2}, {12, 7}, {18, 8}, {14, 12}, {15, 18}, {10, 15}, {5, 18}, {6, 12}, {2, 8}, {8, 7}});
    }
    return image;
}
class ChatDocument : public QTextDocument {
  public:
    ChatDocument(ChatMedia &media, const ChatMessage &m, const QFont &font, const QColor &color, int width)
        : media_(media) {
        setDefaultFont(font);
        setDocumentMargin(0);
        auto option = defaultTextOption();
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        setDefaultTextOption(option);
        addResource(QTextDocument::ImageResource, QUrl("platform:icon"),
                    platformIcon(m.platform).pixmap(18, 18).toImage());
        QString header = imageHtml("platform:icon", m.platform == 0 ? "YouTube" : "Twitch", 16) + " ";
        for (const auto &badge : m.badges.mid(0, 6)) {
            auto url = m.platform == 1 ? media.badgeUrl(badge) : QString();
            if (!url.isEmpty() && !media.ready(QUrl(url))) {
                media.image(QUrl(url));
                url.clear();
            }
            const auto local = "role:" + badge;
            if (url.isEmpty())
                addResource(QTextDocument::ImageResource, QUrl(local), roleImage(badge.section('/', 0, 0)));
            header += imageHtml(url.isEmpty() ? local : url, chatRoleName(badge), 16) + " ";
        }
        header += QString("<a href=\"author\" style=\"color:%1;text-decoration:none\"><b>%2</b></a><span "
                          "style=\"color:#92969e;font-size:small\">  %3</span><br/>")
                      .arg(m.platform == 0 ? "#e28a91" : "#ba98ec", m.author.left(100).toHtmlEscaped(),
                           m.time.toLocalTime().toString("HH:mm"));
        auto body = chatBodyHtml(m, media.emotes());
        const auto matches =
            QRegularExpression("<img src=\"([^\"]+)\" alt=\"([^\"]*)\" width=\"[0-9]+\" height=\"[0-9]+\" />")
                .globalMatch(body);
        // Keep emote names readable while CDN images are unavailable. Only generated
        // image tags are processed; user HTML has already been escaped.
        QList<QRegularExpressionMatch> images;
        auto it = matches;
        while (it.hasNext())
            images.append(it.next());
        for (auto i = images.crbegin(); i != images.crend(); ++i) {
            const QUrl url(i->captured(1).replace("&amp;", "&"));
            if (!media.ready(url)) {
                media.image(url);
                body.replace(i->capturedStart(), i->capturedLength(), i->captured(2));
            }
        }
        setHtml("<div style=\"color:" + color.name() + "\">" + header + body + "</div>");
        setTextWidth(std::max(40, width));
    }

  protected:
    QVariant loadResource(int type, const QUrl &url) override {
        if (type == QTextDocument::ImageResource && allowedChatImage(url))
            return media_.image(url);
        return {}; // Never resolve file:, data:, or arbitrary remote HTML resources.
    }

  private:
    ChatMedia &media_;
};
class ChatView::Delegate : public QStyledItemDelegate {
  public:
    Delegate(ChatMedia &media, ChatView *view) : QStyledItemDelegate(view), media_(media), view_(view) {}
    void invalidate() { heights_.clear(); }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const auto m = index.data(Qt::UserRole).value<ChatMessage>();
        p->save();
        p->setClipRect(option.rect);
        p->setRenderHint(QPainter::Antialiasing);
        const auto rect = option.rect.adjusted(1, 2, -1, -2);
        p->setPen(Qt::NoPen);
        p->setBrush(m.platform == 0 ? QColor(214, 91, 100, 38) : QColor(164, 122, 221, 40));
        p->drawRoundedRect(rect, 5, 5);
        if (option.state & QStyle::State_Selected) {
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(option.palette.highlight().color(), 1));
            p->drawRoundedRect(rect, 5, 5);
        }
        ChatDocument doc(media_, m, option.font, option.palette.color(QPalette::Text),
                         option.rect.width() - 20);
        p->translate(option.rect.topLeft() + QPoint(10, 8));
        doc.drawContents(p);
        p->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const auto message = index.data(Qt::UserRole).value<ChatMessage>();
        const auto key = QString::number(message.platform) + "/" + message.id + "/" +
                         QString::number(view_->viewport()->width()) + "/" + view_->font().toString();
        if (const auto *size = heights_.object(key))
            return *size;
        ChatDocument doc(media_, index.data(Qt::UserRole).value<ChatMessage>(), view_->font(),
                         option.palette.color(QPalette::Text), view_->viewport()->width() - 20);
        auto *size =
            new QSize(view_->viewport()->width(), static_cast<int>(std::ceil(doc.size().height())) + 16);
        const auto result = *size;
        heights_.insert(key, size);
        return result;
    }
    bool authorAt(const QModelIndex &index, const QPoint &point) const {
        if (!index.isValid())
            return false;
        const auto rect = view_->visualRect(index);
        ChatDocument doc(media_, index.data(Qt::UserRole).value<ChatMessage>(), view_->font(),
                         view_->palette().color(QPalette::Text), rect.width() - 20);
        return doc.documentLayout()->anchorAt(point - rect.topLeft() - QPoint(10, 8)) == "author";
    }

  private:
    ChatMedia &media_;
    ChatView *view_;
    mutable QCache<QString, QSize> heights_{600};
};
ChatView::ChatView(ChatMedia &media, QWidget *parent)
    : QListWidget(parent), delegate_(new Delegate(media, this)) {
    setItemDelegate(delegate_);
    setWordWrap(true);
    setMouseTracking(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setSpacing(1);
    // Coalesce GIF frame updates; layout is only recalculated when content/width changes.
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(50);
    connect(&media, &ChatMedia::changed, this, [timer] {
        if (!timer->isActive())
            timer->start();
    });
    connect(timer, &QTimer::timeout, viewport(), qOverload<>(&QWidget::update));
    connect(&media, &ChatMedia::catalogChanged, this, [this] {
        delegate_->invalidate();
        scheduleDelayedItemsLayout();
        viewport()->update();
    });
}
void ChatView::append(const ChatMessage &m) {
    const bool bottom = verticalScrollBar()->value() >= verticalScrollBar()->maximum() - 4;
    auto *item = new QListWidgetItem(platformIcon(m.platform), m.author + ": " + m.text, this);
    item->setData(Qt::UserRole, QVariant::fromValue(m));
    QStringList roles;
    for (const auto &badge : m.badges)
        roles << chatRoleName(badge);
    item->setToolTip((m.platform == 0 ? "YouTube" : "Twitch") +
                     (roles.isEmpty() ? QString() : " · " + roles.join(", ")));
    while (count() > 500)
        delete takeItem(0);
    if (bottom)
        scrollToBottom();
}
void ChatView::resizeEvent(QResizeEvent *event) {
    QListWidget::resizeEvent(event);
    scheduleDelayedItemsLayout();
}
void ChatView::mouseReleaseEvent(QMouseEvent *event) {
    const auto index = indexAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && delegate_->authorAt(index, event->position().toPoint()))
        emit authorClicked(index.data(Qt::UserRole).value<ChatMessage>());
    QListWidget::mouseReleaseEvent(event);
}
void ChatView::mouseMoveEvent(QMouseEvent *event) {
    viewport()->setCursor(
        delegate_->authorAt(indexAt(event->position().toPoint()), event->position().toPoint())
            ? Qt::PointingHandCursor
            : Qt::ArrowCursor);
    QListWidget::mouseMoveEvent(event);
}
} // namespace sb
