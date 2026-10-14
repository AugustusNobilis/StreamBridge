// SPDX-License-Identifier: GPL-2.0-or-later
#include "proxy/adapter.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <iostream>
#include <stdexcept>

using namespace sb;
static void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
    std::cout << "PASS: " << message << std::endl;
}
static bool until(const std::function<bool()> &condition, int ms = 4000) {
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
class FixtureProxy : public QTcpServer {
  public:
    ProxyProtocol protocol;
    bool auth = false, reject = false;
    unsigned connections = 0, authenticated = 0;
    QByteArray target, payload;
    QSslCertificate certificate;
    QSslKey key;
    QList<QPointer<QTcpSocket>> sockets;
    explicit FixtureProxy(ProxyProtocol p) : protocol(p) {}
    void cut() {
        close();
        for (auto socket : sockets)
            if (socket)
                socket->abort();
    }

  protected:
    void incomingConnection(qintptr descriptor) override {
        ++connections;
        QTcpSocket *socket;
        if (protocol == ProxyProtocol::HttpsConnect) {
            auto *ssl = new QSslSocket(this);
            ssl->setSocketDescriptor(descriptor);
            ssl->setLocalCertificate(certificate);
            ssl->setPrivateKey(key);
            ssl->setPeerVerifyMode(QSslSocket::VerifyNone); // Server does not require a client certificate.
            connect(ssl, &QSslSocket::errorOccurred, this,
                    [ssl] { std::cerr << "FIXTURE TLS: " << ssl->errorString().toStdString() << '\n'; });
            socket = ssl;
        } else {
            socket = new QTcpSocket(this);
            socket->setSocketDescriptor(descriptor);
        }
        sockets.append(socket);
        auto bytes = std::make_shared<QByteArray>();
        auto state = std::make_shared<int>(0);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, bytes, state] {
            bytes->append(socket->readAll());
            if (*state == 9) {
                payload += *bytes;
                socket->write(*bytes);
                bytes->clear();
                return;
            }
            if (protocol != ProxyProtocol::Socks5) {
                if (!bytes->contains("\r\n\r\n"))
                    return;
                const auto lines = bytes->split('\n');
                const auto request = lines[0].split(' ');
                if (request.size() >= 2)
                    target = request[1];
                if (bytes->contains("Proxy-Authorization: Basic " +
                                    QByteArray("proxyuser:proxypass").toBase64()))
                    ++authenticated;
                socket->write(reject ? "HTTP/1.1 407 Proxy Authentication Required\r\n\r\n"
                                     : "HTTP/1.1 200 Connection Established\r\n\r\n");
                bytes->clear();
                *state = 9;
                return;
            }
            if (*state == 0 && bytes->size() >= 3) {
                if ((*bytes)[0] != 5 || (*bytes)[1] != 1 || (*bytes)[2] != (auth ? 2 : 0)) {
                    socket->abort();
                    return;
                }
                bytes->remove(0, 3);
                *state = auth ? 1 : 2;
                // Fragment the method reply to exercise incremental parsing.
                socket->write(QByteArray(1, 5));
                QTimer::singleShot(10, socket, [socket, method = static_cast<char>(auth ? 2 : 0)] {
                    socket->write(QByteArray(1, method));
                });
                return;
            }
            if (*state == 1 && bytes->size() >= 2) {
                const auto userLength = static_cast<unsigned char>((*bytes)[1]);
                if (bytes->size() < 3 + userLength)
                    return;
                const auto passLength = static_cast<unsigned char>((*bytes)[2 + userLength]);
                if (bytes->size() < 3 + userLength + passLength)
                    return;
                if (bytes->mid(2, userLength) == "proxyuser" &&
                    bytes->mid(3 + userLength, passLength) == "proxypass")
                    ++authenticated;
                bytes->remove(0, 3 + userLength + passLength);
                *state = 2;
                socket->write(reject ? QByteArray::fromHex("0101") : QByteArray::fromHex("0100"));
                return;
            }
            if (*state == 2 && bytes->size() >= 5) {
                if (bytes->left(4) != QByteArray::fromHex("05010003")) {
                    socket->abort();
                    return;
                }
                const auto length = static_cast<unsigned char>((*bytes)[4]);
                if (bytes->size() < 7 + length)
                    return;
                target = bytes->mid(5, length);
                bytes->remove(0, 7 + length);
                *state = 9;
                socket->write(QByteArray::fromHex("05000001000000000000"));
            }
        });
        if (auto *ssl = qobject_cast<QSslSocket *>(socket))
            ssl->startServerEncryption();
    }
};
static QByteArray read(QTcpSocket &client) {
    QByteArray bytes;
    until([&] {
        bytes += client.readAll();
        return bytes.contains("\r\n\r\n") || client.state() == QAbstractSocket::UnconnectedState;
    });
    return bytes;
}
static void request(QTcpSocket &client, quint16 port, const QByteArray &capability, bool plaintext = false) {
    client.connectToHost("127.0.0.1", port);
    require(client.waitForConnected(1000), "adapter accepts loopback TCP");
    const QByteArray first =
        plaintext ? "GET http://widget.invalid/ HTTP/1.1\r\n" : "CONNECT widget.invalid:443 HTTP/1.1\r\n";
    client.write(first + "Host: widget.invalid:443\r\nProxy-Authorization: Basic " +
                 (QByteArray("streambridge:") + capability).toBase64() + "\r\n\r\n");
    client.flush();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const QByteArray capability(43, 'x');
        QString error;
        for (const auto protocol : {ProxyProtocol::HttpConnect, ProxyProtocol::Socks5}) {
            for (const bool auth : {false, true}) {
                FixtureProxy upstream(protocol);
                upstream.auth = auth;
                require(upstream.listen(QHostAddress::LocalHost, 0), "local proxy fixture listens");
                ProxyConfig config;
                config.host = "127.0.0.1";
                config.port = upstream.serverPort();
                config.protocol = protocol;
                if (auth) {
                    config.username = "proxyuser";
                    config.password = "proxypass";
                }
                ProxyAdapter adapter;
                require(adapter.start(config, capability, error), "adapter starts with selected proxy");
                QTcpSocket unauthorized;
                request(unauthorized, adapter.port(), "wrong-token");
                require(read(unauthorized).startsWith("HTTP/1.1 407") && upstream.connections == 0,
                        "unauthorized loopback client cannot reach upstream");
                QTcpSocket client;
                request(client, adapter.port(), capability);
                require(read(client).startsWith("HTTP/1.1 200"), "CONNECT tunnel established");
                require(upstream.target ==
                            (protocol == ProxyProtocol::Socks5 ? "widget.invalid" : "widget.invalid:443"),
                        "unresolvable destination hostname is passed to proxy");
                if (auth)
                    require(upstream.authenticated == 1, "upstream username/password handshake verified");
                client.write("opaque-end-to-end-TLS-bytes");
                client.flush();
                require(until([&] { return client.bytesAvailable() > 0; }), "tunnel returns data");
                require(client.readAll() == "opaque-end-to-end-TLS-bytes" &&
                            upstream.payload == "opaque-end-to-end-TLS-bytes",
                        "tunnel preserves opaque payload bytes");
                upstream.cut();
                require(until([&] { return client.state() == QAbstractSocket::UnconnectedState; }),
                        "proxy loss closes the browser tunnel");
                QTcpSocket offline;
                request(offline, adapter.port(), capability);
                require(!read(offline).startsWith("HTTP/1.1 200"),
                        "dead proxy fails closed without resolving destination");
                adapter.stop();
            }
        }
        FixtureProxy upstream(ProxyProtocol::Socks5);
        upstream.auth = true;
        upstream.reject = true;
        require(upstream.listen(QHostAddress::LocalHost, 0), "authentication rejection fixture");
        ProxyConfig config;
        config.host = "127.0.0.1";
        config.port = upstream.serverPort();
        config.username = "proxyuser";
        config.password = "proxypass";
        ProxyAdapter adapter;
        require(adapter.start(config, capability, error), "authenticated SOCKS adapter starts");
        QObject::connect(&adapter, &ProxyAdapter::failed, &app,
                         [](const QString &code) { std::cerr << "ADAPTER: " << code.toStdString() << '\n'; });
        QTcpSocket denied;
        request(denied, adapter.port(), capability);
        require(!read(denied).startsWith("HTTP/1.1 200") && upstream.target.isEmpty(),
                "SOCKS authentication failure never sends CONNECT");
        const auto count = upstream.connections;
        QTcpSocket plaintext;
        request(plaintext, adapter.port(), capability, true);
        require(!read(plaintext).startsWith("HTTP/1.1 200") && upstream.connections == count,
                "plaintext proxy requests rejected before upstream access");
        adapter.stop();
        upstream.cut();
        if (argc == 2) {
            require(QSslSocket::supportsSsl(), "Qt TLS backend available");
            QFile cert(QString::fromLocal8Bit(argv[1]) + "/trusted.crt"),
                keyFile(QString::fromLocal8Bit(argv[1]) + "/trusted.key");
            require(cert.open(QIODevice::ReadOnly) && keyFile.open(QIODevice::ReadOnly),
                    "private HTTPS fixture loaded");
            FixtureProxy tls(ProxyProtocol::HttpsConnect);
            tls.certificate = QSslCertificate(cert.readAll());
            tls.key = QSslKey(keyFile.readAll(), QSsl::Rsa);
            require(tls.listen(QHostAddress::LocalHost, 0), "HTTPS proxy fixture listens");
            config = {};
            config.host = "localhost";
            config.port = tls.serverPort();
            config.protocol = ProxyProtocol::HttpsConnect;
            // This trust addition exists only on this test socket, never in Windows or production
            // configuration.
            QFile ca(QString::fromLocal8Bit(argv[1]) + "/ca.crt");
            require(ca.open(QIODevice::ReadOnly), "private test CA loaded");
            auto cas = config.tls.caCertificates();
            cas.append(QSslCertificate(ca.readAll()));
            config.tls.setCaCertificates(cas);
            require(adapter.start(config, capability, error), "HTTPS adapter starts with fixture CA");
            QTcpSocket client;
            request(client, adapter.port(), capability);
            require(read(client).startsWith("HTTP/1.1 200"),
                    "HTTPS proxy tunnel verifies certificate and connects");
            client.write("TLS-proxy-payload");
            client.flush();
            require(until([&] { return client.bytesAvailable() > 0; }) &&
                        client.readAll() == "TLS-proxy-payload",
                    "HTTPS proxy carries opaque application data");
            adapter.stop();
            config.tls = QSslConfiguration::defaultConfiguration();
            require(adapter.start(config, capability, error), "HTTPS negative trust test starts");
            QTcpSocket untrusted;
            request(untrusted, adapter.port(), capability);
            require(!read(untrusted).startsWith("HTTP/1.1 200"),
                    "untrusted HTTPS proxy certificate rejected");
            adapter.stop();
            tls.cut();
        }
        std::cout << "PASS: proxy adapter socket tests complete\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << std::endl;
        return 1;
    }
}
