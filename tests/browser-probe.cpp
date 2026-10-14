// SPDX-License-Identifier: GPL-2.0-or-later
// CEF routing experiment with real TLS to the public, static example.com page.
#include "browser/host.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QNetworkProxy>
#include <QPointer>
#include <QTcpSocket>
#include <QThread>
#include <iostream>
#include <stdexcept>

static void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
    std::cout << "PASS: " << message << std::endl;
}
static bool until(const std::function<bool()> &condition, int ms = 25000) {
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
class Proxy : public QTcpServer {
  public:
    unsigned connects = 0;
    qint64 forwarded = 0;
    QList<QPointer<QTcpSocket>> sockets;
    ~Proxy() override { cut(); }
    Proxy() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *client = nextPendingConnection();
                sockets.append(client);
                auto *remote = new QTcpSocket(client);
                remote->setProxy(QNetworkProxy::NoProxy);
                auto header = std::make_shared<QByteArray>();
                auto ready = std::make_shared<bool>(false);
                connect(client, &QTcpSocket::readyRead, this, [this, client, remote, header, ready] {
                    const auto data = client->readAll();
                    if (*ready) {
                        forwarded += data.size();
                        remote->write(data);
                        return;
                    }
                    header->append(data);
                    if (header->size() > 8192) {
                        client->abort();
                        return;
                    }
                    if (!header->contains("\r\n\r\n"))
                        return;
                    if (!header->startsWith("CONNECT example.com:443 HTTP/1.1\r\n")) {
                        client->abort();
                        return;
                    }
                    ++connects;
                    *ready = true;
                    remote->connectToHost("example.com", 443);
                });
                connect(remote, &QTcpSocket::connected, client,
                        [client] { client->write("HTTP/1.1 200 Connection Established\r\n\r\n"); });
                connect(remote, &QTcpSocket::readyRead, client, [this, client, remote] {
                    auto data = remote->readAll();
                    forwarded += data.size();
                    client->write(data);
                });
                connect(remote, &QTcpSocket::disconnected, client, &QTcpSocket::disconnectFromHost);
                connect(remote, &QTcpSocket::errorOccurred, client, [client](QAbstractSocket::SocketError e) {
                    std::cerr << "PROXY remote error: " << static_cast<int>(e) << std::endl;
                    client->abort();
                });
                connect(client, &QTcpSocket::disconnected, remote, &QTcpSocket::abort);
            }
        });
    }
    void cut() {
        close();
        for (auto socket : sockets)
            if (socket)
                socket->abort();
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2)
        return 2;
    try {
        Proxy a, b;
        require(a.listen(QHostAddress::LocalHost, 0) && b.listen(QHostAddress::LocalHost, 0),
                "two isolated loopback test proxies");
        unsigned framesA = 0, framesB = 0, loadedA = 0, loadedB = 0, loadedDirect = 0, failuresA = 0;
        QString failure;
        sb::BrowserHost first, second, direct;
        first.setDiagnosticFile("build/helper-a.log");
        second.setDiagnosticFile("build/helper-b.log");
        direct.setDiagnosticFile("build/helper-direct.log");
        for (auto *host : {&first, &second, &direct})
            QObject::connect(host, &sb::BrowserHost::failed, &app, [&](const QString &e) {
                failure = e;
                std::cerr << "HELPER: " << e.toStdString() << std::endl;
            });
        QObject::connect(&first, &sb::BrowserHost::video, &app,
                         [&](const QByteArray &, int, int, quint64) { ++framesA; });
        QObject::connect(&second, &sb::BrowserHost::video, &app,
                         [&](const QByteArray &, int, int, quint64) { ++framesB; });
        QObject::connect(&first, &sb::BrowserHost::status, &app, [&](unsigned code) {
            if (code == 2)
                ++loadedA;
            if (code == 3)
                ++failuresA;
        });
        QObject::connect(&first, &sb::BrowserHost::status, &app,
                         [](unsigned code) { std::cout << "STATUS A: " << code << std::endl; });
        QObject::connect(&second, &sb::BrowserHost::status, &app, [&](unsigned code) {
            if (code == 2)
                ++loadedB;
        });
        QObject::connect(&direct, &sb::BrowserHost::status, &app, [&](unsigned code) {
            if (code == 2)
                ++loadedDirect;
        });
        sb::BrowserConfig config;
        config.url = "https://example.com/";
        config.proxy.protocol = sb::ProxyProtocol::HttpConnect;
        config.proxy.host = "127.0.0.1";
        config.proxy.port = a.serverPort();
        const auto helper = QString::fromLocal8Bit(argv[1]);
        QString error;
        require(first.start(helper, config, error), "first sandboxed helper starts");
        require(until([&] { return loadedA > 0 || failuresA > 0; }), "first helper produces a result");
        std::cout << "Proxy A: connects=" << a.connects << " bytes=" << a.forwarded << " frames=" << framesA
                  << std::endl;
        require(loadedA && framesA && a.connects && a.forwarded > 1000,
                "first helper renders HTTPS through proxy A");
        config.proxy.port = b.serverPort();
        require(second.start(helper, config, error), "second sandboxed helper starts");
        require(until([&] { return loadedB > 0 && framesB > 0; }), "second helper produces a result");
        require(loadedB && framesB && b.connects && b.forwarded > 1000 &&
                    first.processId() != second.processId(),
                "second helper uses independent process and proxy B");
        config.proxyEnabled = false;
        const auto aCount = a.connects, bCount = b.connects;
        require(direct.start(helper, config, error), "explicit direct control helper starts");
        require(until([&] { return loadedDirect > 0; }), "direct control renders independently");
        require(a.connects == aCount && b.connects == bCount,
                "direct control does not use either source proxy");
        a.cut();
        const auto original = loadedA;
        first.refresh();
        require(until([&] { return failuresA > 0; }), "dead proxy causes CEF load error");
        require(loadedA == original, "failed source did not render via a direct fallback");
        const auto priorB = loadedB;
        second.refresh();
        require(until([&] { return loadedB > priorB; }), "other proxied source survives proxy A failure");
        first.stop();
        second.stop();
        direct.stop();
        require(!first.processId() && !second.processId() && !direct.processId(),
                "all helper process trees stopped");
        std::cout << "PASS: CEF basic routing probe complete; UDP/DNS capture and widget audio still need "
                     "separate tests\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << std::endl;
        return 1;
    }
}
