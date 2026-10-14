// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>

#include <shellapi.h>
#include <string>
#include <tlhelp32.h>

// Runs with the original OBS token, independent of the elevated installer.
static std::wstring ini(const std::wstring &file, const wchar_t *section, const wchar_t *key) {
    wchar_t value[256]{};
    GetPrivateProfileStringW(section, key, L"", value, 256, file.c_str());
    return value;
}
static bool running(const std::wstring &exe) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return true; // Cannot rule out a duplicate: leave launching to the user.
    PROCESSENTRY32W entry{sizeof(entry)};
    bool found = false;
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        if (_wcsicmp(entry.szExeFile, L"obs64.exe") != 0)
            continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        wchar_t path[32768]{};
        DWORD length = 32768;
        if (!process || !QueryFullProcessImageNameW(process, 0, path, &length) ||
            _wcsicmp(path, exe.c_str()) == 0)
            found = true;
        if (process)
            CloseHandle(process);
    }
    CloseHandle(snapshot);
    return found;
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc != 8)
        return 2;
    const HANDLE installer = reinterpret_cast<HANDLE>(_wcstoui64(argv[1], nullptr, 10));
    const HANDLE obs = reinterpret_cast<HANDLE>(_wcstoui64(argv[2], nullptr, 10));
    const HANDLE cancel = reinterpret_cast<HANDLE>(_wcstoui64(argv[3], nullptr, 10));
    const std::wstring root = argv[4], cache = argv[5], version = argv[6];
    const bool portable = std::wstring(argv[7]) == L"1";
    LocalFree(argv);
    const auto result = cache + L"\\restart-result.ini";
    auto finish = [&](const wchar_t *status, int code) {
        WritePrivateProfileStringW(L"Restart", L"Status", status, result.c_str());
        CloseHandle(installer);
        CloseHandle(obs);
        CloseHandle(cancel);
        return code;
    };
    HANDLE waits[]{cancel, installer};
    const auto wait = WaitForMultipleObjects(2, waits, FALSE, 30 * 60 * 1000);
    if (wait == WAIT_OBJECT_0)
        return finish(L"cancelled", 0);
    DWORD code = 1;
    if (wait != WAIT_OBJECT_0 + 1 || !GetExitCodeProcess(installer, &code) || code != 0 ||
        WaitForSingleObject(obs, 0) != WAIT_OBJECT_0 ||
        ini(cache + L"\\update-result.ini", L"Update", L"Status") != L"success" ||
        ini(cache + L"\\update-result.ini", L"Update", L"Version") != version ||
        ini(root + L"\\data\\obs-plugins\\obs-streambridge\\package.ini", L"StreamBridge", L"Version") !=
            version)
        return finish(L"failed", 3);
    const auto directory = root + L"\\bin\\64bit";
    const auto exe = directory + L"\\obs64.exe";
    if (running(exe))
        return finish(L"already-running", 0);
    if (WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0)
        return finish(L"cancelled", 0);
    auto command = L"\"" + exe + L"\"" + (portable ? L" --portable" : L"");
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, directory.c_str(),
                        &startup, &process))
        return finish(L"failed", 4);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return finish(L"started", 0);
}
