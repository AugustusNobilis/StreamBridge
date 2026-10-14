// SPDX-License-Identifier: GPL-2.0-or-later
#include "loopback.hpp"
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QUrlQuery>
#include <cstring>
#include <memory>

namespace sb {
QByteArray randomUrlToken() {
    QByteArray bytes(32, '\0');
    for (int i = 0; i < bytes.size(); i += 4) {
        const auto value = QRandomGenerator::system()->generate();
        std::memcpy(bytes.data() + i, &value, 4);
    }
    return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
QByteArray pkceChallenge(const QByteArray &verifier) {
    return QCryptographicHash::hash(verifier, QCryptographicHash::Sha256)
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
OAuthLoopback::OAuthLoopback(QObject *parent) : QObject(parent), listener_(this), expiry_(this) {
    listener_.setMaxPendingConnections(4);
    expiry_.setSingleShot(true);
    connect(&expiry_, &QTimer::timeout, this, [this] {
        stop();
        emit expired();
    });
    connect(&listener_, &QTcpServer::newConnection, this, [this] {
        while (listener_.hasPendingConnections()) {
            auto *socket = listener_.nextPendingConnection();
            if (listener_.findChildren<QTcpSocket *>().size() > 4) {
                socket->abort();
                socket->deleteLater();
                continue;
            }
            socket->setReadBufferSize(8193);
            auto buffer = std::make_shared<QByteArray>();
            QTimer::singleShot(5000, socket, [socket] {
                socket->abort();
                socket->deleteLater();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                buffer->append(socket->readAll());
                if (buffer->size() > 8192) {
                    socket->abort();
                    return;
                }
                if (!buffer->contains("\r\n\r\n"))
                    return;
                const auto line = buffer->left(buffer->indexOf("\r\n")).split(' ');
                const QUrl url = line.size() == 3 ? QUrl::fromEncoded(line[1], QUrl::StrictMode) : QUrl();
                const QUrlQuery query(url);
                const auto returned = query.queryItemValue("state", QUrl::FullyDecoded).toLatin1();
                unsigned mismatch = static_cast<unsigned>(returned.size() ^ state_.size());
                for (qsizetype i = 0; i < std::min(returned.size(), state_.size()); ++i)
                    mismatch |= static_cast<unsigned char>(returned[i] ^ state_[i]);
                int states = 0, codes = 0;
                for (const auto &item : query.queryItems()) {
                    if (item.first == "state")
                        ++states;
                    if (item.first == "code")
                        ++codes;
                }
                const bool valid = listener_.isListening() && !state_.isEmpty() && line.size() == 3 &&
                                   line[0] == "GET" && url.isRelative() && url.path() == "/oauth/callback" &&
                                   !url.hasFragment() && mismatch == 0 && states == 1 && codes <= 1;
                const auto code = query.queryItemValue("code", QUrl::FullyDecoded);
                const bool denied = query.hasQueryItem("error");
                const bool accepted = valid && (denied || (!code.isEmpty() && code.size() <= 4096));
                socket->write(accepted ? "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nCache-Control: "
                                         "no-store\r\nConnection: close\r\n\r\nReturn to OBS StreamBridge."
                                       : "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\nInvalid "
                                         "authorization response.");
                socket->disconnectFromHost();
                buffer->fill('\0');
                if (accepted) {
                    stop();
                    emit completed(code, denied);
                }
            });
        }
    });
}
QUrl OAuthLoopback::start(const QByteArray &state) {
    stop();
    if (state.size() < 32 || !listener_.listen(QHostAddress::LocalHost, 0))
        return {};
    state_ = state;
    expiry_.start(300000);
    return QUrl(QString("http://127.0.0.1:%1/oauth/callback").arg(listener_.serverPort()));
}
void OAuthLoopback::stop() {
    listener_.close();
    expiry_.stop();
    state_.fill('\0');
    state_.clear();
}
} // namespace sb
