// SPDX-License-Identifier: GPL-2.0-or-later
#include "launch.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryFile>
#include <qt_windows.h>
#include <shellapi.h>

namespace sb {
bool updateInstallerRunning(const QString &file) {
    QSettings result(QFileInfo(file).path() + "/update-result.ini", QSettings::IniFormat);
    const auto pid = result.value("Update/Pid").toUInt();
    const auto stamp = result.value("Update/Created").toULongLong();
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;
    FILETIME created{}, exited{}, kernel{}, user{};
    wchar_t path[32768];
    DWORD length = 32768;
    const bool running = GetProcessTimes(process, &created, &exited, &kernel, &user) &&
                         ((quint64(created.dwHighDateTime) << 32) | created.dwLowDateTime) == stamp &&
                         QueryFullProcessImageNameW(process, 0, path, &length) &&
                         QFileInfo(QString::fromWCharArray(path, int(length)))
                                 .canonicalFilePath()
                                 .compare(QFileInfo(file).canonicalFilePath(), Qt::CaseInsensitive) == 0 &&
                         WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return running;
}
bool launchUpdate(const QString &file, const Release &release, QString &error) {
    const QFileInfo obs(QCoreApplication::applicationFilePath());
    const QString root = QDir(obs.absolutePath() + "/../..").canonicalPath();
    if (obs.fileName().compare("obs64.exe", Qt::CaseInsensitive) != 0 ||
        QFileInfo(root + "/bin/64bit/obs64.exe").canonicalFilePath() != obs.canonicalFilePath()) {
        error = "UpdateWrongHost";
        return false;
    }
    const auto nativeFile = QDir::toNativeSeparators(file).toStdWString();
    HANDLE lock = CreateFileW(nativeFile.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock == INVALID_HANDLE_VALUE) {
        error = "UpdateVerifyFailed";
        return false;
    }
    if (!verifyUpdateFile(file, release)) {
        CloseHandle(lock);
        error = "UpdateVerifyFailed";
        return false;
    }
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        CloseHandle(lock);
        error = "UpdateLaunchFailed";
        return false;
    }
    const quint64 stamp = (quint64(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    const auto args = QString("/S /UPDATEPID=%1 /UPDATECREATED=%2 /D=%3")
                          .arg(GetCurrentProcessId())
                          .arg(stamp)
                          .arg(QDir::toNativeSeparators(root))
                          .toStdWString();
    bool elevate = false;
    for (const auto &dir : {root, root + "/obs-plugins", root + "/obs-plugins/64bit", root + "/data",
                            root + "/data/obs-plugins", root + "/data/obs-plugins/obs-streambridge"}) {
        if (!QFileInfo::exists(dir))
            continue;
        QTemporaryFile probe(dir + "/.streambridge-write-XXXXXX");
        if (!probe.open()) {
            elevate = true;
            break;
        }
    }
    const auto working = QFileInfo(file).absolutePath().toStdWString();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = elevate ? L"runas" : L"open";
    info.lpFile = nativeFile.c_str();
    info.lpParameters = args.c_str();
    info.lpDirectory = working.c_str();
    info.nShow = SW_HIDE;
    const bool ok = ShellExecuteExW(&info) != FALSE;
    const DWORD code = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(lock);
    if (info.hProcess)
        CloseHandle(info.hProcess);
    error = code == ERROR_CANCELLED ? "UpdateElevationCancelled" : "UpdateLaunchFailed";
    return ok;
}
} // namespace sb
