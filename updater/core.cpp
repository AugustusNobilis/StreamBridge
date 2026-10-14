// SPDX-License-Identifier: GPL-2.0-or-later
#include "core.hpp"
#include <array>
#include <bcrypt.h>
#include <chrono>
#include <cmath>
#include <memory>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <vector>
#include <winhttp.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace bootstrap {
namespace fs = std::filesystem;
namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
struct Internet {
    HINTERNET value;
    explicit Internet(HINTERNET handle) : value(handle) {
        if (!handle)
            throw Failure{L"Не удалось подключиться к GitHub. Проверьте интернет и повторите запуск."};
    }
    ~Internet() { WinHttpCloseHandle(value); }
    Internet(const Internet &) = delete;
    Internet &operator=(const Internet &) = delete;
};
void require(bool condition, const wchar_t *message) {
    if (!condition)
        throw Failure{message};
}
std::wstring guid() {
    GUID id{};
    winrt::check_hresult(CoCreateGuid(&id));
    wchar_t buffer[40]{};
    StringFromGUID2(id, buffer, 40);
    return buffer;
}
struct Url {
    std::wstring host, path;
};
Url split(const std::wstring &url) {
    URL_COMPONENTS parts{sizeof(URL_COMPONENTS)};
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = parts.dwUserNameLength =
        parts.dwPasswordLength = DWORD(-1);
    require(url.size() < 16384 && url.find_first_of(L"#\\\r\n") == std::wstring::npos &&
                WinHttpCrackUrl(url.c_str(), 0, 0, &parts) && parts.nScheme == INTERNET_SCHEME_HTTPS &&
                parts.nPort == 443 && !parts.dwUserNameLength && !parts.dwPasswordLength,
            L"GitHub вернул недопустимый адрес загрузки.");
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    return {std::wstring(parts.lpszHostName, parts.dwHostNameLength), path};
}
std::wstring header(HINTERNET request, DWORD field) {
    DWORD bytes = 0;
    WinHttpQueryHeaders(request, field, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &bytes,
                        WINHTTP_NO_HEADER_INDEX);
    require(bytes > 0 && bytes < 32768, L"GitHub вернул недопустимый ответ.");
    std::vector<wchar_t> data(bytes / sizeof(wchar_t) + 1);
    require(WinHttpQueryHeaders(request, field, WINHTTP_HEADER_NAME_BY_INDEX, data.data(), &bytes,
                                WINHTTP_NO_HEADER_INDEX),
            L"Не удалось прочитать ответ GitHub.");
    return data.data();
}
using Sink = std::function<void(const char *, DWORD)>;
void get(const std::wstring &initial, bool asset, std::uint64_t limit, std::atomic_bool &cancel,
         const Sink &sink) {
    Internet session(WinHttpOpen(L"StreamBridge-Updater/0.2.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    require(WinHttpSetTimeouts(session.value, 30000, 30000, 30000, 30000), L"Ошибка сетевых настроек.");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(15);
    auto url = initial;
    for (int redirects = 0; redirects <= 3; ++redirects) {
        if (cancel)
            throw Failure{L"Загрузка отменена."};
        require(!asset || allowedDownloadUrl(url), L"Перенаправление загрузки отклонено.");
        const auto address = split(url);
        Internet connection(WinHttpConnect(session.value, address.host.c_str(), 443, 0));
        Internet request(WinHttpOpenRequest(connection.value, L"GET", address.path.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE));
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        DWORD disable = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
        require(
            WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)) &&
                WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)),
            L"Не удалось настроить безопасную загрузку.");
        const wchar_t *headers =
            asset ? L"Accept: application/octet-stream\r\n"
                  : L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
        require(WinHttpSendRequest(request.value, headers, DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                    WinHttpReceiveResponse(request.value, nullptr),
                L"Не удалось загрузить обновление. Проверьте интернет и повторите запуск.");
        DWORD status = 0, bytes = sizeof(status);
        require(WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes, WINHTTP_NO_HEADER_INDEX),
                L"Не удалось прочитать ответ GitHub.");
        if (status >= 300 && status < 400 && asset) {
            require(redirects < 3, L"Слишком много перенаправлений.");
            url = header(request.value, WINHTTP_QUERY_LOCATION);
            continue;
        }
        if (status == 404)
            throw Failure{L"Версия 0.2.1 ещё не опубликована на GitHub. Повторите позже."};
        if (status == 403 || status == 429)
            throw Failure{L"GitHub временно ограничил запросы. Повторите позже."};
        require(status == 200, L"GitHub не смог выдать пакет. Повторите загрузку позже.");
        std::uint64_t received = 0;
        std::array<char, 65536> buffer{};
        for (;;) {
            if (cancel)
                throw Failure{L"Загрузка отменена."};
            require(std::chrono::steady_clock::now() < deadline,
                    L"Истекло время загрузки. Повторите запуск.");
            DWORD count = 0;
            require(WinHttpReadData(request.value, buffer.data(), DWORD(buffer.size()), &count),
                    L"Соединение прервано. Повторите запуск для новой загрузки.");
            if (!count)
                return;
            received += count;
            require(received <= limit, L"Размер ответа GitHub превышает ожидаемый.");
            sink(buffer.data(), count);
        }
    }
}
std::wstring registry(HKEY hive, const wchar_t *key, const wchar_t *name) {
    wchar_t buffer[32768]{};
    DWORD bytes = sizeof(buffer);
    if (RegGetValueW(hive, key, name, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buffer, &bytes) !=
        ERROR_SUCCESS)
        return {};
    return buffer;
}
std::wstring imagePath(HANDLE process) {
    wchar_t buffer[32768]{};
    DWORD size = 32768;
    if (!QueryFullProcessImageNameW(process, 0, buffer, &size))
        return {};
    return std::wstring(buffer, size);
}
bool samePath(const fs::path &a, const fs::path &b) {
    std::error_code error;
    return fs::equivalent(a, b, error) && !error;
}
bool writable(const fs::path &directory) {
    if (!fs::exists(directory))
        return true;
    Handle probe(CreateFileW((directory / (L".streambridge-" + guid())).c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
    return bool(probe);
}
} // namespace

bool allowedDownloadUrl(const std::wstring &url) {
    try {
        const auto parts = split(url);
        return url == assetUrl || parts.host == L"release-assets.githubusercontent.com" ||
               parts.host == L"objects.githubusercontent.com";
    } catch (...) {
        return false;
    }
}
Release parseRelease(const std::string &json) {
    require(json.size() <= 2 * 1024 * 1024, L"Метаданные релиза слишком велики.");
    try {
        using namespace winrt::Windows::Data::Json;
        auto document = JsonObject::Parse(winrt::to_hstring(json));
        require(document.GetNamedString(L"tag_name") == L"v0.2.1" && !document.GetNamedBoolean(L"draft"),
                L"GitHub вернул другую версию.");
        Release release{};
        for (const auto &entry : document.GetNamedArray(L"assets")) {
            const auto asset = entry.GetObject();
            if (asset.GetNamedString(L"name", L"") != assetName)
                continue;
            const auto size = asset.GetNamedNumber(L"size");
            const std::wstring digest(asset.GetNamedString(L"digest"));
            require(!release.size && asset.GetNamedString(L"state") == L"uploaded" &&
                        asset.GetNamedString(L"browser_download_url") == assetUrl && std::isfinite(size) &&
                        size >= 1 && size <= 1024.0 * 1024 * 1024 && std::floor(size) == size &&
                        digest.size() == 71 && digest.starts_with(L"sha256:") &&
                        digest.find_first_not_of(L"0123456789abcdef", 7) == std::wstring::npos,
                    L"Пакет релиза не прошёл проверку метаданных.");
            release = {static_cast<std::uint64_t>(size), digest.substr(7)};
        }
        require(release.size != 0, L"В релизе нет проверяемого установщика Windows x64.");
        return release;
    } catch (const winrt::hresult_error &) {
        throw Failure{L"Некорректные метаданные релиза GitHub."};
    }
}
std::wstring sha256(HANDLE file) {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0,
            L"Не удалось проверить SHA-256.");
    struct HashCleanup {
        BCRYPT_ALG_HANDLE algorithm;
        BCRYPT_HASH_HANDLE &hash;
        ~HashCleanup() {
            if (hash)
                BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } cleanup{algorithm, hash};
    require(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0, L"Ошибка SHA-256.");
    LARGE_INTEGER start{};
    require(SetFilePointerEx(file, start, nullptr, FILE_BEGIN), L"Не удалось прочитать загруженный пакет.");
    std::array<UCHAR, 65536> buffer{};
    for (;;) {
        DWORD read{};
        require(ReadFile(file, buffer.data(), DWORD(buffer.size()), &read, nullptr),
                L"Ошибка чтения пакета.");
        if (!read)
            break;
        require(BCryptHashData(hash, buffer.data(), read, 0) >= 0, L"Ошибка SHA-256.");
    }
    UCHAR digest[32]{};
    require(BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0, L"Ошибка SHA-256.");
    std::wstring result;
    for (auto byte : digest) {
        result += L"0123456789abcdef"[byte >> 4];
        result += L"0123456789abcdef"[byte & 15];
    }
    return result;
}
bool verifyFile(const fs::path &file, const Release &release) {
    Handle handle(CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr));
    LARGE_INTEGER size{};
    return handle && GetFileSizeEx(handle.value, &size) && std::uint64_t(size.QuadPart) == release.size &&
           sha256(handle.value) == release.digest;
}
Download download(const fs::path &cache, std::atomic_bool &cancel, const Progress &progress) {
    progress(L"Проверка релиза 0.2.1 на GitHub…", 0);
    std::string json;
    get(L"https://api.github.com/repos/AugustusNobilis/StreamBridge/releases/tags/v0.2.1", false,
        2 * 1024 * 1024, cancel, [&](const char *data, DWORD size) { json.append(data, size); });
    Download result;
    result.release = parseRelease(json);
    fs::create_directories(cache);
    const auto directory = cache / (L"StreamBridge-" + guid());
    require(fs::create_directory(directory), L"Не удалось создать папку загрузки.");
    result.file = directory / assetName;
    try {
        {
            Handle file(CreateFileW(result.file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr));
            require(bool(file), L"Не удалось сохранить установщик.");
            std::uint64_t received = 0;
            int last = -1;
            get(assetUrl, true, result.release.size, cancel, [&](const char *data, DWORD count) {
                DWORD written{};
                require(WriteFile(file.value, data, count, &written, nullptr) && written == count,
                        L"Не удалось записать пакет. Проверьте свободное место.");
                received += count;
                const int percent = int(received * 100 / result.release.size);
                if (percent != last) {
                    last = percent;
                    progress(L"Загрузка обновления: " + std::to_wstring(percent) + L"%", percent);
                }
            });
            require(received == result.release.size && FlushFileBuffers(file.value),
                    L"Пакет загружен не полностью.");
        }
        progress(L"Проверка SHA-256…", 100);
        require(verifyFile(result.file, result.release),
                L"Контрольная сумма не совпала. Пакет не будет запущен.");
        if (cancel)
            throw Failure{L"Загрузка отменена."};
        return result;
    } catch (...) {
        cleanup(result);
        throw;
    }
}
void cleanup(const Download &download) {
    if (download.file.empty())
        return;
    // Delete only this generated file and empty private directory; never recursive.
    std::error_code error;
    fs::remove(download.file, error);
    fs::remove(download.file.parent_path() / L"update-result.ini", error);
    fs::remove(download.file.parent_path() / L"update-result.tmp", error);
    fs::remove(download.file.parent_path(), error);
}
bool isObsRoot(const fs::path &root) {
    return !root.empty() && root.is_absolute() && fs::is_regular_file(root / L"bin/64bit/obs64.exe");
}
fs::path findObs() {
    std::vector<fs::path> paths = {
        registry(HKEY_CURRENT_USER,
                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OBSStreamBridge",
                 L"InstallLocation"),
        registry(HKEY_LOCAL_MACHINE, L"Software\\OBS Studio", nullptr)};
    PWSTR programFiles{};
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &programFiles))) {
        paths.emplace_back(fs::path(programFiles) / L"obs-studio");
        CoTaskMemFree(programFiles);
    }
    for (const auto &path : paths)
        if (isObsRoot(path))
            return fs::canonical(path);
    return {};
}
void install(const Download &download, const fs::path &selectedRoot, const Progress &progress, HWND owner) {
    require(isObsRoot(selectedRoot), L"Выберите папку OBS с файлом bin\\64bit\\obs64.exe.");
    const auto root = fs::canonical(selectedRoot);
    auto mutexName = L"Local\\StreamBridgeBootstrap-" + root.wstring();
    for (auto &letter : mutexName) {
        if (letter == L'\\' && &letter >= mutexName.data() + 6)
            letter = L'/';
        else if (&letter >= mutexName.data() + 6)
            letter = wchar_t(towupper(letter));
    }
    Handle mutex(CreateMutexW(nullptr, FALSE, mutexName.c_str()));
    const DWORD mutexError = GetLastError();
    require(bool(mutex) && mutexError != ERROR_ALREADY_EXISTS,
            L"Обновление этой папки OBS уже выполняется. Дождитесь его завершения.");
    Handle lock(CreateFileW(download.file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr));
    require(bool(lock) && verifyFile(download.file, download.release), L"Пакет изменился после проверки.");
    std::wstring arguments = L"/S";
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    require(bool(snapshot), L"Не удалось проверить процессы OBS.");
    PROCESSENTRY32W entry{sizeof(PROCESSENTRY32W)};
    bool waiting = false;
    if (Process32FirstW(snapshot.value, &entry))
        do {
            if (_wcsicmp(entry.szExeFile, L"obs64.exe") != 0)
                continue;
            Handle process(
                OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, entry.th32ProcessID));
            if (!process || !samePath(imagePath(process.value), root / L"bin/64bit/obs64.exe"))
                continue;
            FILETIME created{}, exited{}, kernel{}, user{};
            if (WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT ||
                !GetProcessTimes(process.value, &created, &exited, &kernel, &user))
                continue;
            const auto stamp = (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
            arguments += L" /UPDATEPID=" + std::to_wstring(entry.th32ProcessID) + L" /UPDATECREATED=" +
                         std::to_wstring(stamp);
            waiting = true;
            break;
        } while (Process32NextW(snapshot.value, &entry));
    // NSIS requires /D last, without quotes, even when the Unicode path contains spaces.
    arguments += L" /D=" + root.wstring();
    bool elevate = !writable(root) || !writable(root / L"obs-plugins") ||
                   !writable(root / L"obs-plugins/64bit") || !writable(root / L"data") ||
                   !writable(root / L"data/obs-plugins") ||
                   !writable(root / L"data/obs-plugins/obs-streambridge");
    progress(waiting ? L"Закройте OBS обычным способом. Обновление продолжится автоматически."
                     : L"Установка 0.2.1. Настройки StreamBridge сохраняются.",
             100);
    const auto directory = download.file.parent_path().wstring();
    SHELLEXECUTEINFOW info{sizeof(SHELLEXECUTEINFOW)};
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.hwnd = owner;
    info.lpVerb = elevate ? L"runas" : L"open";
    info.lpFile = download.file.c_str();
    info.lpParameters = arguments.c_str();
    info.lpDirectory = directory.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info))
        throw Failure{GetLastError() == ERROR_CANCELLED
                          ? L"Разрешение Windows отменено. Обновление не установлено."
                          : L"Не удалось запустить установщик."};
    Handle process(info.hProcess);
    require(bool(process) && WaitForSingleObject(process.value, INFINITE) == WAIT_OBJECT_0,
            L"Не удалось дождаться установщика.");
    DWORD code = 1;
    require(GetExitCodeProcess(process.value, &code), L"Не удалось узнать результат установки.");
    if (code == 32)
        throw Failure{L"Файлы заняты OBS или браузером StreamBridge. Закройте их и повторите запуск."};
    if (code == 5)
        throw Failure{L"Windows запретила запись. Запустите updater.exe от имени администратора."};
    require(code == 0,
            L"Установка не завершена. Проверьте папку OBS (32+ x64), закройте OBS и повторите запуск.");
    wchar_t installed[32]{};
    GetPrivateProfileStringW(L"StreamBridge", L"Version", L"", installed, 32,
                             (root / L"data/obs-plugins/obs-streambridge/package.ini").c_str());
    require(std::wstring(installed) == version, L"Не удалось подтвердить установленную версию.");
}
} // namespace bootstrap
