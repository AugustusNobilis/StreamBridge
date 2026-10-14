// SPDX-License-Identifier: GPL-2.0-or-later
#include "updates/launch.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QThread>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc == 1 || (argc == 2 && app.arguments()[1] == "--portable")) {
        QFile marker(QCoreApplication::applicationDirPath() + "/restarted.txt");
        if (marker.open(QIODevice::WriteOnly))
            marker.write((QDir::currentPath() + "\n" + app.arguments().join(' ')).toUtf8());
        return 0;
    }
    if (argc != 4)
        return 2;
    const QString installer = app.arguments()[1];
    const QString control = app.arguments()[2];
    const QString result = app.arguments()[3];
    QFile file(installer);
    if (!file.open(QIODevice::ReadOnly))
        return 3;
    sb::Release release;
    release.version = "99.0.0";
    release.asset = QUrl("https://github.com/example/streambridge/releases/download/v99.0.0/fixture.exe");
    release.size = file.size();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return 4;
    release.sha256 = hash.result().toHex();
    file.close();
    QElapsedTimer timer;
    timer.start();
    bool launched = false;
    QString cancellation;
    while (timer.elapsed() < 600000) {
        QFile command(control);
        QByteArray action;
        if (command.open(QIODevice::ReadOnly))
            action = command.readAll().trimmed();
        command.close();
        if (action == "exit")
            return 0;
        if ((action == "launch" || action.startsWith("restart:")) && !launched) {
            const bool restart = action.startsWith("restart:");
            if (restart)
                release.version = QString::fromUtf8(action.mid(8));
            QString error;
            const bool ok = sb::launchUpdate(installer, release, error, restart ? &cancellation : nullptr);
            QFile response(result);
            if (response.open(QIODevice::WriteOnly))
                response.write(ok ? QByteArray("launched") : error.toUtf8());
            if (!ok)
                return 5;
            launched = true;
        }
        if (action == "cancel" && !cancellation.isEmpty()) {
            sb::cancelUpdateRestart(cancellation);
            cancellation.clear();
        }
        if (launched && sb::updateInstallerRunning(installer)) {
            QFile observed(result + ".running");
            if (observed.open(QIODevice::WriteOnly))
                observed.write("running");
        }
        QThread::msleep(20);
    }
    return 6;
}
