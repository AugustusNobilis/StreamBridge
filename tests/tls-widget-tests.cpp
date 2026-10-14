// SPDX-License-Identifier: GPL-2.0-or-later
// Real localhost HTTPS and WSS with normal CEF/Windows certificate validation.
#include "browser/host.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QNetworkProxy>
#include <QPointer>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QThread>
#include <QUdpSocket>
#include <iostream>
#include <stdexcept>
static void require(bool value, const char *message) {
    if (!value) {
        std::cerr << "FAIL: " << message << std::endl;
        throw std::runtime_error(message);
    }
    std::cout << "PASS: " << message << std::endl;
}
static bool until(const std::function<bool()> &done, int ms = 20000) {
    QElapsedTimer t;
    t.start();
    do {
        QCoreApplication::processEvents();
        if (done())
            return true;
        QThread::msleep(5);
    } while (t.elapsed() < ms);
    return false;
}
static QByteArray read(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error("fixture missing");
    return f.readAll();
}
class Server : public QTcpServer {
  public:
    QSslCertificate certificate;
    QSslKey key;
    bool tls = false;
    QList<QPointer<QTcpSocket>> sockets;
    std::function<void(QTcpSocket *)> accept;
    ~Server() override { cut(); }
    void cut() {
        close();
        const auto copy = sockets;
        for (auto s : copy)
            if (s) {
                QObject::disconnect(s, nullptr, nullptr, nullptr);
                s->abort();
            }
    }

  protected:
    void incomingConnection(qintptr descriptor) override {
        QTcpSocket *s = tls ? new QSslSocket(this) : new QTcpSocket(this);
        s->setSocketDescriptor(descriptor);
        sockets.append(s);
        if (tls) {
            auto *ssl = static_cast<QSslSocket *>(s);
            ssl->setLocalCertificate(certificate);
            ssl->setPrivateKey(key);
            ssl->setPeerVerifyMode(QSslSocket::VerifyNone);
            QObject::connect(ssl, &QSslSocket::errorOccurred, ssl, [ssl] {
                std::cerr << "TLS fixture: " << ssl->errorString().toStdString() << std::endl;
            });
            accept(s);
            ssl->startServerEncryption();
        } else
            accept(s);
    }
};
class Widget final : public Server {
  public:
    unsigned websocketMessages = 0, workerRequests = 0, images = 0;
    Widget() {
        tls = true;
        accept = [this](QTcpSocket *s) {
            auto buffer = std::make_shared<QByteArray>();
            auto websocket = std::make_shared<bool>(false);
            QObject::connect(s, &QTcpSocket::readyRead, s, [this, s, buffer, websocket] {
                buffer->append(s->readAll());
                if (buffer->size() > 65536) {
                    s->abort();
                    return;
                }
                if (*websocket) {
                    if (buffer->size() < 6)
                        return;
                    const auto length = static_cast<unsigned char>((*buffer)[1]) & 127;
                    if (length > 125 || buffer->size() < 6 + length)
                        return;
                    QByteArray payload = buffer->mid(6, length);
                    for (int i = 0; i < length; ++i)
                        payload[i] = payload[i] ^ (*buffer)[2 + i % 4];
                    buffer->remove(0, 6 + length);
                    if (payload == "ping") {
                        ++websocketMessages;
                        s->write(QByteArray::fromHex("8109") + "widget-ok");
                    }
                    return;
                }
                if (!buffer->contains("\r\n\r\n"))
                    return;
                const auto request = *buffer;
                buffer->clear();
                const auto line = request.left(request.indexOf("\r\n"));
                if (line.startsWith("GET /ws ")) {
                    QByteArray nonce;
                    for (auto h : request.split('\n'))
                        if (h.toLower().startsWith("sec-websocket-key:"))
                            nonce = h.mid(h.indexOf(':') + 1).trimmed();
                    if (nonce.isEmpty()) {
                        s->abort();
                        return;
                    }
                    const auto digest =
                        QCryptographicHash::hash(nonce + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
                                                 QCryptographicHash::Sha1)
                            .toBase64();
                    s->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: "
                             "Upgrade\r\nSec-WebSocket-Accept: " +
                             digest + "\r\n\r\n");
                    *websocket = true;
                    return;
                }
                QByteArray body, mime = "text/html";
                if (line.startsWith("GET /worker.js ")) {
                    ++workerRequests;
                    mime = "text/javascript";
                    body = "postMessage('worker-ok')";
                } else if (line.startsWith("GET /pixel.svg ")) {
                    ++images;
                    mime = "image/svg+xml";
                    body = "<svg xmlns='http://www.w3.org/2000/svg' width='8' height='8'><rect width='8' "
                           "height='8' fill='lime'/></svg>";
                } else if (line.startsWith("GET /redirect ")) {
                    s->write("HTTP/1.1 302 Found\r\nLocation: /widget\r\nContent-Length: 0\r\nConnection: "
                             "close\r\n\r\n");
                    s->disconnectFromHost();
                    return;
                } else {
                    body =
                        R"HTML(<!doctype html><style>body{margin:0;background:transparent}#ws{position:absolute;left:32px;top:32px;width:64px;height:64px;background:rgba(255,0,0,.5)}</style>
<div id="ws"></div><img src="/pixel.svg"><script>
const ws=new WebSocket('wss://'+location.host+'/ws');ws.onopen=()=>ws.send('ping');ws.onmessage=e=>{if(e.data==='widget-ok')document.getElementById('ws').style.background='rgba(0,0,255,.5)'};
new Worker('/worker.js').onmessage=()=>document.body.dataset.worker='ok';
let n=0;setInterval(()=>document.getElementById('ws').style.left=(32+(n++%16))+'px',33);
const ac=new AudioContext({sampleRate:48000});const o=ac.createOscillator(),g=ac.createGain();g.gain.value=.1;o.connect(g).connect(ac.destination);o.start();ac.resume();
const pc=new RTCPeerConnection({iceServers:[{urls:'stun:127.0.0.1:38173'}]});pc.createDataChannel('test');pc.createOffer().then(o=>pc.setLocalDescription(o)).catch(()=>{});
</script>)HTML";
                }
                s->write("HTTP/1.1 200 OK\r\nContent-Type: " + mime +
                         "\r\nContent-Length: " + QByteArray::number(body.size()) +
                         "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" + body);
                s->disconnectFromHost();
            });
        };
    }
};
class ForwardProxy final : public Server {
  public:
    bool socks = false;
    quint16 targetPort = 0;
    unsigned tunnels = 0, authenticated = 0;
    bool badTarget = false;
    ForwardProxy() {
        accept = [this](QTcpSocket *client) {
            auto *remote = new QTcpSocket(client);
            remote->setProxy(QNetworkProxy::NoProxy);
            auto buffer = std::make_shared<QByteArray>();
            auto phase = std::make_shared<int>(socks ? 0 : 3);
            QObject::connect(client, &QTcpSocket::readyRead, client, [this, client, remote, buffer, phase] {
                auto data = client->readAll();
                if (*phase == 4) {
                    remote->write(data);
                    return;
                }
                buffer->append(data);
                if (buffer->size() > 16384) {
                    client->abort();
                    return;
                }
                if (*phase == 0) {
                    if (buffer->size() < 3)
                        return;
                    if (buffer->left(3) != QByteArray::fromHex("050102")) {
                        client->abort();
                        return;
                    }
                    buffer->remove(0, 3);
                    client->write(QByteArray::fromHex("0502"));
                    *phase = 1;
                }
                if (*phase == 1) {
                    if (buffer->size() < 2)
                        return;
                    const auto user = static_cast<unsigned char>((*buffer)[1]);
                    if (buffer->size() < 3 + user)
                        return;
                    const auto pass = static_cast<unsigned char>((*buffer)[2 + user]);
                    if (buffer->size() < 3 + user + pass)
                        return;
                    if (buffer->mid(2, user) != "fixture" || buffer->mid(3 + user, pass) != "local-test") {
                        client->abort();
                        return;
                    }
                    ++authenticated;
                    buffer->remove(0, 3 + user + pass);
                    client->write(QByteArray::fromHex("0100"));
                    *phase = 2;
                }
                if (*phase == 2) {
                    if (buffer->size() < 5)
                        return;
                    const auto size = static_cast<unsigned char>((*buffer)[4]);
                    if (buffer->size() < 7 + size)
                        return;
                    const auto port = (static_cast<unsigned char>((*buffer)[5 + size]) << 8) |
                                      static_cast<unsigned char>((*buffer)[6 + size]);
                    if (buffer->left(4) != QByteArray::fromHex("05010003") ||
                        buffer->mid(5, size) != "localhost" || port != targetPort) {
                        badTarget = true;
                        client->abort();
                        return;
                    }
                    buffer->remove(0, 7 + size);
                    *phase = 4;
                } else if (*phase == 3) {
                    if (!buffer->contains("\r\n\r\n"))
                        return;
                    if (!buffer->startsWith("CONNECT localhost:" + QByteArray::number(targetPort) +
                                            " HTTP/1.1\r\n")) {
                        badTarget = true;
                        client->abort();
                        return;
                    }
                    if (!buffer->contains("Proxy-Authorization: Basic " +
                                          QByteArray("fixture:local-test").toBase64())) {
                        client->write(
                            "HTTP/1.1 407 Proxy Authentication Required\r\nContent-Length: 0\r\n\r\n");
                        client->disconnectFromHost();
                        return;
                    }
                    ++authenticated;
                    buffer->clear();
                    *phase = 4;
                } else
                    return;
                ++tunnels;
                remote->connectToHost(QHostAddress::LocalHost, targetPort);
            });
            QObject::connect(remote, &QTcpSocket::connected, client, [this, client] {
                client->write(socks ? QByteArray::fromHex("050000017f0000010000")
                                    : QByteArray("HTTP/1.1 200 Connection Established\r\n\r\n"));
            });
            QObject::connect(remote, &QTcpSocket::readyRead, client,
                             [client, remote] { client->write(remote->readAll()); });
            QObject::connect(remote, &QTcpSocket::disconnected, client, &QTcpSocket::disconnectFromHost);
            QObject::connect(client, &QTcpSocket::disconnected, remote, &QTcpSocket::abort);
            QObject::connect(remote, &QTcpSocket::errorOccurred, client,
                             [client] { client->disconnectFromHost(); });
        };
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 3)
        return 2;
    try {
        const QString helper = QString::fromLocal8Bit(argv[1]), directory = QString::fromLocal8Bit(argv[2]);
        Widget widget;
        widget.certificate = QSslCertificate(read(directory + "/trusted.crt"));
        widget.key = QSslKey(read(directory + "/trusted.key"), QSsl::Rsa);
        require(widget.listen(QHostAddress::LocalHost, 0), "real local TLS widget server");
        QUdpSocket stun;
        require(stun.bind(QHostAddress::LocalHost, 38173), "STUN canary");
        ForwardProxy http, socks, https;
        https.tls = true;
        https.certificate = QSslCertificate(read(directory + "/proxy/trusted.crt"));
        https.key = QSslKey(read(directory + "/proxy/trusted.key"), QSsl::Rsa);
        socks.socks = true;
        for (auto *p : {&http, &socks, &https}) {
            p->targetPort = widget.serverPort();
            require(p->listen(QHostAddress::LocalHost, 0), "isolated authenticated proxy listener");
        }
        unsigned loaded[3]{}, failed[3]{}, audio[3]{}, frames[3]{};
        bool blue[3]{};
        sb::BrowserHost a, b, c;
        sb::BrowserHost *hosts[]{&a, &b, &c};
        ForwardProxy *proxies[]{&http, &socks, &https};
        for (int i = 0; i < 3; ++i) {
            hosts[i]->setDiagnosticFile(QString("build/tls-helper-%1.log").arg(i));
            QObject::connect(hosts[i], &sb::BrowserHost::failed, &app, [i](const QString &code) {
                std::cerr << "HELPER " << i << ": " << code.toStdString() << std::endl;
            });
            QObject::connect(hosts[i], &sb::BrowserHost::networkWarning, &app, [i](const QString &code) {
                std::cerr << "NETWORK " << i << ": " << code.toStdString() << std::endl;
            });
            QObject::connect(hosts[i], &sb::BrowserHost::status, &app, [&, i](unsigned code) {
                if (code == 2)
                    ++loaded[i];
                if (code == 3)
                    ++failed[i];
            });
            QObject::connect(hosts[i], &sb::BrowserHost::audio, &app,
                             [&, i](QByteArray, int, quint64) { ++audio[i]; });
            QObject::connect(hosts[i], &sb::BrowserHost::video, &app,
                             [&, i](QByteArray bytes, int, int, quint64) {
                                 ++frames[i];
                                 for (qsizetype k = 0; k < bytes.size(); k += 4)
                                     if (static_cast<unsigned char>(bytes[k]) > 100 && bytes[k + 2] == 0 &&
                                         static_cast<unsigned char>(bytes[k + 3]) > 100)
                                         blue[i] = true;
                             });
            sb::BrowserConfig config;
            config.url = QString("https://localhost:%1/redirect").arg(widget.serverPort());
            config.width = 256;
            config.height = 128;
            config.proxy.protocol = i == 0   ? sb::ProxyProtocol::HttpConnect
                                    : i == 1 ? sb::ProxyProtocol::Socks5
                                             : sb::ProxyProtocol::HttpsConnect;
            config.proxy.host = "localhost";
            config.proxy.port = proxies[i]->serverPort();
            config.proxy.username = "fixture";
            config.proxy.password = "local-test";
            if (i == 2) {
                auto cas = config.proxy.tls.caCertificates();
                cas.append(QSslCertificate(read(directory + "/proxy/ca.crt")));
                config.proxy.tls.setCaCertificates(cas);
            }
            QString error;
            require(hosts[i]->start(helper, config, error), "real production helper launched");
        }
        const bool complete = until([&] {
            for (int i = 0; i < 3; ++i)
                if (!loaded[i] || audio[i] < 10 || frames[i] < 10 || !blue[i])
                    return false;
            return true;
        });
        for (int i = 0; i < 3; ++i)
            std::cout << "PROXY " << i << ": loaded=" << loaded[i] << " failed=" << failed[i]
                      << " audio=" << audio[i] << " frames=" << frames[i] << " blue=" << blue[i]
                      << " tunnels=" << proxies[i]->tunnels << " auth=" << proxies[i]->authenticated
                      << std::endl;
        require(complete,
                "HTTPS redirect, JS animation, WSS echo and audio through all three proxy protocols");
        require(widget.websocketMessages >= 3 && widget.workerRequests >= 3 && widget.images >= 3,
                "real WSS messages, workers and images received");
        for (auto *p : proxies)
            require(p->tunnels > 0 && p->authenticated > 0, "upstream credentials and tunneled TLS verified");
        require(!stun.hasPendingDatagrams(), "no direct STUN UDP from real HTTPS widget");
        const auto savedPort = http.serverPort();
        http.cut();
        const auto before = loaded[0];
        a.refresh();
        require(until([&] { return failed[0] > 0; }), "failed HTTP proxy blocks page");
        require(loaded[0] == before, "no direct fallback to reachable local origin");
        const auto peer = loaded[1];
        b.refresh();
        require(until([&] { return loaded[1] > peer; }), "SOCKS source survives HTTP proxy failure");
        require(http.listen(QHostAddress::LocalHost, savedPort), "HTTP proxy restored");
        a.refresh();
        require(until([&] { return loaded[0] > before; }), "source reload recovers after proxy restoration");
        a.stop();
        b.stop();
        c.stop();
        require(!a.processId() && !b.processId() && !c.processId(), "active WSS/audio helper trees stopped");
        std::cout
            << "PASS: TLS widget tests complete; synthetic widget, not DonationAlerts account acceptance\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << std::endl;
        return 1;
    }
}
