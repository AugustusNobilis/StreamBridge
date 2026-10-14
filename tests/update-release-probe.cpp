// SPDX-License-Identifier: GPL-2.0-or-later
#include "updates/checker.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QNetworkReply>
#include <QSslSocket>
#include <QTextStream>
#include <QTimer>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2)
        return 2;
    const int tlsOption = app.arguments().indexOf("--tls-path");
    if (tlsOption >= 0 && tlsOption + 1 < app.arguments().size())
        QCoreApplication::addLibraryPath(QDir(app.arguments()[tlsOption + 1]).absolutePath());
    QTextStream(stdout) << "TLS backends: " << QSslSocket::availableBackends().join(", ") << Qt::endl;
    QNetworkAccessManager network;
    QObject::connect(&network, &QNetworkAccessManager::finished, &app, [](QNetworkReply *reply) {
        QTextStream(stdout) << "HTTP " << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                            << "; network error " << int(reply->error()) << Qt::endl;
    });
    sb::UpdateChecker checker(QDir(app.arguments()[1]).absolutePath(), nullptr, &network);
    const bool checkOnly = app.arguments().contains("--check-only");
    if (!checker.configure("AugustusNobilis/StreamBridge", false, false, true))
        return 3;
    QObject::connect(&checker, &sb::UpdateChecker::changed, &app, [&] {
        if (checker.state() != "UpdateDownloading")
            QTextStream(stdout) << checker.state() << " " << checker.version() << Qt::endl;
        if (checker.canDownload()) {
            if (checkOnly)
                app.exit(0);
            else
                checker.download();
        } else if (checker.ready()) {
            QTextStream(stdout) << "PASS: public GitHub package downloaded and SHA-256 verified: "
                                << checker.readyFile() << Qt::endl;
            app.exit(0);
        } else if (!checker.busy()) {
            app.exit(checkOnly && checker.state() == "UpdateNoRelease" ? 0 : 1);
        }
    });
    QTimer::singleShot(300000, &app, [&] { app.exit(4); });
    QTimer::singleShot(0, &checker, &sb::UpdateChecker::check);
    return app.exec();
}
