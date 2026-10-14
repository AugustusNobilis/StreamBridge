// SPDX-License-Identifier: GPL-2.0-or-later
#include "adapter.hpp"
#include <QNetworkProxy>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <functional>

namespace sb {
QString validateProxy(const ProxyConfig &c) {
    const QUrl authority("http://" + c.host + ":" + QString::number(c.port), QUrl::StrictMode);
    if (static_cast<unsigned>(c.protocol) > static_cast<unsigned>(ProxyProtocol::Socks5) ||
        c.host.isEmpty() || c.host.size() > 253 || c.port == 0 || !authority.isValid() ||
        authority.host().isEmpty() || authority.host() != c.host || !authority.userInfo().isEmpty() ||
        !authority.path().isEmpty() || authority.hasQuery() || authority.hasFragment() ||
        c.username.size() > 255 || c.password.size() > 255 || c.username.contains(':') ||
        c.username.contains('\r') || c.username.contains('\n') || c.password.contains('\r') ||
        c.password.contains('\n') || (c.username.isEmpty() && !c.password.isEmpty()))
        return "ProxyInvalid";
    return {};
}
namespace {
// One authenticated loopback TCP tunnel. There is deliberately no destination connectToHost path.
class Tunnel final : public QObject {
  public:
    Tunnel(QTcpSocket *client, ProxyConfig config, QByteArray capability, QObject *parent,
           std::function<void(QString)> error)
        : QObject(parent), client_(client), config_(std::move(config)), capability_(std::move(capability)),
          error_(std::move(error)), deadline_(this) {
        client_->setParent(this);
        client_->setReadBufferSize(1024 * 1024);
        deadline_.setSingleShot(true);
        deadline_.start(15000);
        connect(&deadline_, &QTimer::timeout, this, [this] { fail("ProxyTimeout"); });
        connect(client_, &QTcpSocket::readyRead, this, [this] { readClient(); });
        connect(client_, &QTcpSocket::disconnected, this, [this] { close(); });
        readClient();
    }
    ~Tunnel() override {
        // QTcpSocket destruction can emit disconnected before QObject tears down
        // connections. Never let callbacks touch partially destroyed tunnel state.
        state_ = State::Closed;
        deadline_.stop();
        QObject::disconnect(client_, nullptr, this, nullptr);
        if (upstream_) {
            QObject::disconnect(upstream_, nullptr, this, nullptr);
            upstream_->abort();
        }
        client_->abort();
        config_.password.fill('\0');
        capability_.fill('\0');
    }

  private:
    enum class State {
        ClientHeader,
        Connecting,
        HttpReply,
        SocksMethod,
        SocksAuth,
        SocksReply,
        Relay,
        Draining,
        Closed
    };
    void close() {
        if (state_ == State::Closed)
            return;
        state_ = State::Closed;
        deadline_.stop();
        if (upstream_)
            upstream_->abort();
        client_->abort();
        deleteLater();
    }
    void fail(const QString &code) {
        if (state_ != State::Closed) {
            error_(code);
            close();
        }
    }
    void upstreamClosed() {
        if (state_ == State::Closed || state_ == State::Draining)
            return;
        if (state_ != State::Relay) {
            fail("ProxyConnectionFailed");
            return;
        }
        const auto tail = upstream_->readAll();
        if (!tail.isEmpty() && !write(client_, tail))
            return;
        // A normal upstream EOF can arrive while encrypted response bytes are
        // queued for the browser. abort() here truncates HTTPS responses.
        state_ = State::Draining;
        deadline_.start(5000);
        client_->disconnectFromHost();
        if (client_->state() == QAbstractSocket::UnconnectedState)
            close();
    }
    bool write(QAbstractSocket *socket, const QByteArray &data) {
        if (socket->bytesToWrite() + data.size() > 4 * 1024 * 1024) {
            fail("ProxyBufferLimit");
            return false;
        }
        if (socket->write(data) != data.size()) {
            fail("ProxyWriteFailed");
            return false;
        }
        deadline_.start(300000);
        return true;
    }
    void readClient() {
        if (state_ == State::Closed)
            return;
        auto bytes = client_->readAll();
        if (state_ == State::Relay) {
            write(upstream_, bytes);
            return;
        }
        pending_.append(bytes);
        if (pending_.size() > 65536) {
            fail("ProxyHeaderLimit");
            return;
        }
        if (state_ != State::ClientHeader || !pending_.contains("\r\n\r\n"))
            return;
        const auto end = pending_.indexOf("\r\n\r\n") + 4;
        const auto lines = pending_.left(end - 2).split('\n');
        const auto first = lines[0].trimmed().split(' ');
        if (first.size() != 3 || first[2] != "HTTP/1.1") {
            fail("ProxyBadRequest");
            return;
        }
        QByteArray auth;
        unsigned authHeaders = 0;
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const auto line = lines[i].trimmed();
            if (line.isEmpty())
                continue;
            const auto colon = line.indexOf(':');
            if (colon < 1) {
                fail("ProxyBadRequest");
                return;
            }
            const auto name = line.left(colon).toLower();
            if (name == "proxy-authorization") {
                auth = line.mid(colon + 1).trimmed();
                ++authHeaders;
            }
        }
        const auto expected = "Basic " + (QByteArray("streambridge:") + capability_).toBase64();
        unsigned different = static_cast<unsigned>(auth.size() ^ expected.size());
        for (qsizetype i = 0; i < std::min(auth.size(), expected.size()); ++i)
            different |= static_cast<unsigned char>(auth[i] ^ expected[i]);
        if (different || authHeaders != 1) {
            state_ = State::Closed;
            client_->write("HTTP/1.1 407 Proxy Authentication Required\r\nProxy-Authenticate: Basic "
                           "realm=\"StreamBridge\"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            client_->disconnectFromHost();
            deadline_.stop();
            QTimer::singleShot(1000, this, [this] { deleteLater(); });
            return;
        }
        capability_.fill('\0');
        capability_.clear();
        // The source accepts HTTPS/WSS only. Refuse plaintext proxy requests rather than
        // forwarding later keep-alive proxy credentials to an origin server.
        if (first[0] != "CONNECT") {
            fail("ProxyHttpsOnly");
            return;
        }
        const QUrl target("https://" + QString::fromLatin1(first[1]), QUrl::StrictMode);
        if (!target.isValid() || target.host().isEmpty() || !target.userInfo().isEmpty() ||
            target.hasFragment() || !target.path().isEmpty() || target.hasQuery()) {
            fail("ProxyBadTarget");
            return;
        }
        host_ = QUrl::toAce(target.host());
        port_ = target.port(443);
        if (host_.isEmpty() || host_.size() > 255 || port_ <= 0 || port_ > 65535) {
            fail("ProxyBadTarget");
            return;
        }
        pending_.remove(0, end);
        state_ = State::Connecting;
        if (config_.protocol == ProxyProtocol::HttpsConnect) {
            auto *tls = new QSslSocket(this);
            tls->setSslConfiguration(config_.tls);
            tls->setPeerVerifyMode(QSslSocket::VerifyPeer);
            tls->setPeerVerifyName(config_.host);
            upstream_ = tls;
            connect(tls, &QSslSocket::encrypted, this, [this] { handshake(); });
            connect(tls, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
                fail(QString("ProxyTlsRejected (%1)")
                         .arg(errors.isEmpty() ? 0 : static_cast<int>(errors.first().error())));
            });
        } else {
            upstream_ = new QTcpSocket(this);
            connect(upstream_, &QTcpSocket::connected, this, [this] { handshake(); });
        }
        upstream_->setProxy(QNetworkProxy::NoProxy);
        upstream_->setReadBufferSize(1024 * 1024);
        connect(upstream_, &QTcpSocket::readyRead, this, [this] { readUpstream(); });
        connect(upstream_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError code) {
            if (code == QAbstractSocket::RemoteHostClosedError && state_ == State::Relay)
                return;
            else
                fail("ProxyConnectionFailed");
        });
        connect(upstream_, &QTcpSocket::disconnected, this, [this] { upstreamClosed(); });
        if (auto *tls = qobject_cast<QSslSocket *>(upstream_))
            tls->connectToHostEncrypted(config_.host, config_.port);
        else
            upstream_->connectToHost(config_.host, config_.port);
    }
    void handshake() {
        deadline_.start(15000);
        if (config_.protocol == ProxyProtocol::Socks5) {
            state_ = State::SocksMethod;
            // Do not silently downgrade configured authentication to anonymous SOCKS.
            upstream_->write(config_.username.isEmpty() ? QByteArray::fromHex("050100")
                                                        : QByteArray::fromHex("050102"));
        } else {
            state_ = State::HttpReply;
            const auto authority =
                (host_.contains(':') ? '[' + host_ + ']' : host_) + ':' + QByteArray::number(port_);
            QByteArray request = "CONNECT " + authority + " HTTP/1.1\r\nHost: " + authority + "\r\n";
            if (!config_.username.isEmpty())
                request += "Proxy-Authorization: Basic " +
                           (config_.username + ':' + config_.password).toBase64() + "\r\n";
            upstream_->write(request + "\r\n");
            request.fill('\0');
        }
    }
    void socksConnect() {
        state_ = State::SocksReply;
        // RFC 1928 domain form keeps destination resolution at the upstream proxy.
        QByteArray request = QByteArray::fromHex("05010003");
        request += static_cast<char>(host_.size());
        request += host_;
        request += static_cast<char>((port_ >> 8) & 255);
        request += static_cast<char>(port_ & 255);
        upstream_->write(request);
    }
    void established() {
        state_ = State::Relay;
        config_.password.fill('\0');
        config_.password.clear();
        if (!write(client_, "HTTP/1.1 200 Connection Established\r\n\r\n"))
            return;
        if (!pending_.isEmpty()) {
            if (!write(upstream_, pending_))
                return;
            pending_.clear();
        }
        if (!response_.isEmpty()) {
            write(client_, response_);
            response_.clear();
        }
        deadline_.start(300000);
    }
    void readUpstream() {
        if (state_ == State::Closed)
            return;
        const auto bytes = upstream_->readAll();
        if (state_ == State::Relay) {
            write(client_, bytes);
            return;
        }
        response_.append(bytes);
        if (response_.size() > 65536) {
            fail("ProxyHeaderLimit");
            return;
        }
        if (state_ == State::HttpReply) {
            const auto end = response_.indexOf("\r\n\r\n");
            if (end < 0)
                return;
            const auto first = response_.left(response_.indexOf("\r\n")).split(' ');
            if (first.size() < 2 || (first[0] != "HTTP/1.1" && first[0] != "HTTP/1.0") || first[1] != "200") {
                fail("ProxyConnectRejected");
                return;
            }
            response_.remove(0, end + 4);
            established();
            return;
        }
        if (state_ == State::SocksMethod && response_.size() >= 2) {
            const unsigned char method = config_.username.isEmpty() ? 0 : 2;
            if (response_[0] != 5 || static_cast<unsigned char>(response_[1]) != method) {
                fail("ProxyAuthRejected");
                return;
            }
            response_.remove(0, 2);
            if (method == 0)
                socksConnect();
            else {
                state_ = State::SocksAuth;
                QByteArray auth(1, 1);
                auth += static_cast<char>(config_.username.size());
                auth += config_.username;
                auth += static_cast<char>(config_.password.size());
                auth += config_.password;
                upstream_->write(auth);
                auth.fill('\0');
            }
        }
        if (state_ == State::SocksAuth && response_.size() >= 2) {
            if (response_[0] != 1 || response_[1] != 0) {
                fail("ProxyAuthRejected");
                return;
            }
            response_.remove(0, 2);
            socksConnect();
        }
        if (state_ == State::SocksReply && response_.size() >= 5) {
            if (response_[0] != 5 || response_[1] != 0 || response_[2] != 0) {
                fail("ProxyConnectRejected");
                return;
            }
            const unsigned char addressType = static_cast<unsigned char>(response_[3]);
            const int size = addressType == 1   ? 10
                             : addressType == 4 ? 22
                             : addressType == 3 ? 7 + static_cast<unsigned char>(response_[4])
                                                : 0;
            if (size == 0) {
                fail("ProxyInvalidResponse");
                return;
            }
            if (response_.size() < size)
                return;
            response_.remove(0, size);
            established();
        }
    }
    QTcpSocket *client_, *upstream_ = nullptr;
    ProxyConfig config_;
    QByteArray capability_, pending_, response_, host_;
    std::function<void(QString)> error_;
    QTimer deadline_;
    State state_ = State::ClientHeader;
    int port_ = 0;
};
} // namespace
ProxyAdapter::ProxyAdapter(QObject *parent) : QObject(parent), server_(this) {
    server_.setProxy(QNetworkProxy::NoProxy);
    server_.setMaxPendingConnections(32);
    connect(&server_, &QTcpServer::newConnection, this, [this] {
        while (server_.hasPendingConnections()) {
            auto *socket = server_.nextPendingConnection();
            if (!socket->peerAddress().isLoopback() || children().size() > 128) {
                socket->abort();
                socket->deleteLater();
                continue;
            }
            new Tunnel(socket, config_, capability_, this,
                       [this](const QString &error) { emit failed(error); });
        }
    });
}
ProxyAdapter::~ProxyAdapter() {
    stop();
}
bool ProxyAdapter::start(const ProxyConfig &config, const QByteArray &capability, QString &error) {
    stop();
    error = validateProxy(config);
    if (!error.isEmpty() || capability.size() < 32 || capability.size() > 128) {
        error = "ProxyInvalid";
        return false;
    }
    config_ = config;
    capability_ = capability;
    if (!server_.listen(QHostAddress::LocalHost, 0)) {
        error = "ProxyListenFailed";
        return false;
    }
    return true;
}
void ProxyAdapter::stop() {
    server_.close();
    while (true) {
        QObject *victim = nullptr;
        for (auto *child : children())
            if (child != &server_) {
                victim = child;
                break;
            }
        if (!victim)
            break;
        // children() returns a reference. Deletion changes the list and invalidates
        // range-for iterators; select a fresh child on each iteration.
        delete victim;
    }
    config_.password.fill('\0');
    config_.password.clear();
    capability_.fill('\0');
    capability_.clear();
}
} // namespace sb
