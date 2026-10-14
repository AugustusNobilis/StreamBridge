// SPDX-License-Identifier: GPL-2.0-or-later
#include "launch.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryFile>
#include <QUuid>
#include <qt_windows.h>
#include <shellapi.h>
#include <vector>

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
void cancelUpdateRestart(const QString &event) {
    if (event.isEmpty())
        return;
    HANDLE handle = OpenEventW(EVENT_MODIFY_STATE, FALSE, event.toStdWString().c_str());
    if (handle) {
        SetEvent(handle);
        CloseHandle(handle);
    }
}
static bool startRestartHelper(const QString &helper, HANDLE installer, HANDLE cancel, const QString &root,
                               const QString &cache, const QString &version) {
    HANDLE handles[3]{};
    const HANDLE sources[]{installer, GetCurrentProcess(), cancel};
    bool ok = true;
    for (int i = 0; i < 3; ++i)
        ok = DuplicateHandle(GetCurrentProcess(), sources[i], GetCurrentProcess(), &handles[i], 0, TRUE,
                             DUPLICATE_SAME_ACCESS) &&
             ok;
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<char> storage(size);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    const bool initialized = InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size) != FALSE;
    ok = initialized && ok;
    if (ok)
        ok = UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                                       sizeof(handles), nullptr, nullptr) != FALSE;
    if (ok) {
        const bool portable = QCoreApplication::arguments().contains("--portable") ||
                              QCoreApplication::arguments().contains("-p");
        auto args = QString("\"%1\" %2 %3 %4 \"%5\" \"%6\" \"%7\" %8")
                        .arg(QDir::toNativeSeparators(helper))
                        .arg(quintptr(handles[0]))
                        .arg(quintptr(handles[1]))
                        .arg(quintptr(handles[2]))
                        .arg(QDir::toNativeSeparators(root), QDir::toNativeSeparators(cache), version)
                        .arg(portable ? 1 : 0)
                        .toStdWString();
        PROCESS_INFORMATION process{};
        ok = CreateProcessW(QDir::toNativeSeparators(helper).toStdWString().c_str(), args.data(), nullptr,
                            nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr,
                            &startup.StartupInfo, &process) != FALSE;
        if (ok) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
    }
    if (initialized)
        DeleteProcThreadAttributeList(startup.lpAttributeList);
    for (HANDLE handle : handles)
        if (handle)
            CloseHandle(handle);
    return ok;
}
bool launchUpdate(const QString &file, const Release &release, QString &error, QString *restartCancellation) {
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
    QString cancelName, helper;
    HANDLE cancel = nullptr;
    if (restartCancellation) {
        cancelName = "Local\\StreamBridgeUpdateCancel-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        cancel = CreateEventW(nullptr, TRUE, FALSE, cancelName.toStdWString().c_str());
        helper = QFileInfo(file).path() + "/restart-" + QUuid::createUuid().toString(QUuid::WithoutBraces) +
                 ".exe";
        if (!cancel ||
            !QFile::copy(root + "/data/obs-plugins/obs-streambridge/streambridge-update-helper.exe",
                         helper)) {
            if (cancel)
                CloseHandle(cancel);
            CloseHandle(lock);
            error = "UpdateLaunchFailed";
            return false;
        }
    }
    const auto args = QString("/S /UPDATEPID=%1 /UPDATECREATED=%2 %3 /D=%4")
                          .arg(GetCurrentProcessId())
                          .arg(stamp)
                          .arg(cancel ? "/UPDATECANCEL=" + cancelName : QString())
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
    bool ok = ShellExecuteExW(&info) != FALSE;
    const DWORD code = ok ? ERROR_SUCCESS : GetLastError();
    if (ok && cancel)
        ok = info.hProcess &&
             startRestartHelper(helper, info.hProcess, cancel, root, QFileInfo(file).path(), release.version);
    if (cancel) {
        if (ok)
            *restartCancellation = cancelName;
        else
            SetEvent(cancel);
        CloseHandle(cancel);
    }
    CloseHandle(lock);
    if (info.hProcess)
        CloseHandle(info.hProcess);
    error = code == ERROR_CANCELLED ? "UpdateElevationCancelled" : "UpdateLaunchFailed";
    return ok;
}
} // namespace sb
