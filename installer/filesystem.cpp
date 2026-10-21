// SPDX-License-Identifier: GPL-2.0-or-later
// No DeleteFile, RemoveDirectory, filesystem::remove_all, or absolute-path traversal.
// NtCreateFile is the documented WDK API: Win32 has no directory-relative open.
#include <aclapi.h>
#include <algorithm>
#include <cwctype>
#include <iostream>
#include <map>
#include <memory>
#include <sddl.h>
#include <set>
#include <shlobj.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>
#include <winternl.h>

namespace {
constexpr auto resources = L"data\\obs-plugins\\obs-streambridge";
constexpr auto binary = L"obs-plugins\\64bit\\obs-streambridge.dll";
constexpr auto settings = L"obs-studio\\plugin_config\\obs-streambridge";
constexpr ULONG openReparse = 0x00200000, synchronous = 0x20, directoryFile = 1;
struct Failure {
    DWORD code;
    std::wstring path;
};
void trace(const std::wstring &message) {
    wchar_t path[32768]{};
    if (!GetEnvironmentVariableW(L"STREAMBRIDGE_LIFECYCLE_LOG", path, 32768))
        return;
    const auto line = std::to_wstring(GetTickCount64()) + L" pid=" + std::to_wstring(GetCurrentProcessId()) +
                      L" " + message + L"\r\n";
    HANDLE log =
        CreateFileW((std::wstring(path) + L".files.log").c_str(), FILE_APPEND_DATA,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(log, line.data(), static_cast<DWORD>(line.size() * sizeof(wchar_t)), &written, nullptr);
        CloseHandle(log);
    }
}
[[noreturn]] void fail(DWORD code, const std::wstring &path) {
    throw Failure{code ? code : ERROR_GEN_FAILURE, path};
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
    Handle(const Handle &) = delete;
};
using Ref = std::shared_ptr<Handle>;
struct Entry {
    Ref handle;
    std::wstring path;
    bool directory;
};
std::wstring normalize(std::wstring path) {
    std::replace(path.begin(), path.end(), L'/', L'\\');
    if (path.size() < 3 || !iswalpha(path[0]) || path[1] != L':' || path[2] != L'\\' ||
        path.find_first_of(L":*?", 2) != path.npos)
        fail(87, path);
    std::vector<wchar_t> buffer(32768);
    const auto n = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (!n || n >= buffer.size())
        fail(87, path);
    path.assign(buffer.data(), n);
    while (path.size() > 3 && path.back() == L'\\')
        path.pop_back();
    return path;
}
bool equal(const std::wstring &a, const std::wstring &b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}
std::vector<std::wstring> parts(const std::wstring &path) {
    std::vector<std::wstring> result;
    size_t start = 0;
    while (start < path.size()) {
        auto end = path.find(L'\\', start);
        auto name = path.substr(start, end == path.npos ? end : end - start);
        if (name.empty() || name.size() > 255 || name == L"." || name == L".." || name.back() == L'.' ||
            name.back() == L' ' || name.find_first_of(L"/:*?\"<>|") != name.npos)
            fail(87, path);
        result.push_back(name);
        if (end == path.npos)
            break;
        start = end + 1;
    }
    return result;
}
FILE_ATTRIBUTE_TAG_INFO attributes(const Ref &h, const std::wstring &name) {
    FILE_ATTRIBUTE_TAG_INFO info{};
    if (!GetFileInformationByHandleEx(h->value, FileAttributeTagInfo, &info, sizeof(info)))
        fail(GetLastError(), name);
    if (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        fail(87, name);
    return info;
}
Ref child(const Ref &parent, const std::wstring &name, ACCESS_MASK access, bool createDirectory = false,
          bool createFile = false, bool missing = true) {
    if (parts(name).size() != 1)
        fail(87, name);
    UNICODE_STRING text{};
    text.Buffer = const_cast<wchar_t *>(name.c_str());
    text.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
    text.MaximumLength = text.Length;
    OBJECT_ATTRIBUTES oa{};
    oa.Length = sizeof(oa);
    oa.RootDirectory = parent->value;
    oa.ObjectName = &text;
    oa.Attributes = 0x40 | 0x1000; // OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE
    IO_STATUS_BLOCK io{};
    HANDLE raw = INVALID_HANDLE_VALUE;
    const auto status =
        NtCreateFile(&raw, access | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &io, nullptr,
                     FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     createDirectory || createFile ? 3 : 1, // FILE_OPEN_IF : FILE_OPEN
                     openReparse | synchronous | (createDirectory ? directoryFile : 0), nullptr, 0);
    if (status < 0) {
        if (missing &&
            (status == static_cast<NTSTATUS>(0xc0000034) || status == static_cast<NTSTATUS>(0xc000003a)))
            return {};
        if (status == static_cast<NTSTATUS>(0xc000050b))
            fail(87, name);
        fail(RtlNtStatusToDosError(status), name);
    }
    auto h = std::make_shared<Handle>(raw);
    attributes(h, name); // On the same opened object, never on its former pathname.
    return h;
}
struct Tree {
    std::map<std::wstring, Ref> dirs;
    Ref root;
    bool removeResources;
    explicit Tree(const std::wstring &absolute, bool remove = false) : removeResources(remove) {
        const auto path = normalize(absolute);
        // Only the volume root is opened by absolute path. Every filesystem component follows via handles.
        root = std::make_shared<Handle>(
            CreateFileW((L"\\\\?\\" + path.substr(0, 3)).c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (root->value == INVALID_HANDLE_VALUE)
            fail(GetLastError(), path);
        attributes(root, path.substr(0, 3));
        for (const auto &name : parts(path.substr(3))) {
            root = child(root, name, FILE_LIST_DIRECTORY, false, false, false);
            if (!(attributes(root, name).FileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                fail(87, path);
        }
        dirs[L""] = root;
    }
    Ref dir(const std::wstring &path, bool create = false, bool deletable = false) {
        if (auto i = dirs.find(path); i != dirs.end())
            return i->second;
        const auto slash = path.rfind(L'\\');
        auto parent = dir(slash == path.npos ? L"" : path.substr(0, slash), create, false);
        if (!parent)
            return {};
        deletable = deletable || (removeResources &&
                                  (path == resources || path.starts_with(std::wstring(resources) + L"\\")));
        auto h = child(parent, path.substr(slash == path.npos ? 0 : slash + 1),
                       FILE_LIST_DIRECTORY | (deletable ? DELETE : 0), create);
        if (!h)
            return {};
        if (!(attributes(h, path).FileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            fail(87, path);
        dirs[path] = h;
        return h;
    }
    Ref file(const std::wstring &path, ACCESS_MASK access, bool create = false) {
        parts(path);
        const auto slash = path.rfind(L'\\');
        auto parent = dir(slash == path.npos ? L"" : path.substr(0, slash), create);
        if (!parent)
            return {};
        auto h = child(parent, path.substr(slash == path.npos ? 0 : slash + 1), access, false, create);
        if (h && (attributes(h, path).FileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            fail(87, path);
        return h;
    }
};
std::vector<std::wstring> names(const Ref &dir) {
    std::vector<std::wstring> result;
    alignas(8) BYTE buffer[65536];
    auto kind = FileIdBothDirectoryRestartInfo;
    for (;;) {
        if (!GetFileInformationByHandleEx(dir->value, kind, buffer, sizeof(buffer))) {
            if (GetLastError() == ERROR_NO_MORE_FILES)
                break;
            fail(GetLastError(), L"enumerate directory handle");
        }
        kind = FileIdBothDirectoryInfo;
        auto *entry = reinterpret_cast<FILE_ID_BOTH_DIR_INFO *>(buffer);
        for (;;) {
            std::wstring name(entry->FileName, entry->FileNameLength / sizeof(wchar_t));
            if (name != L"." && name != L"..")
                result.push_back(name);
            if (!entry->NextEntryOffset)
                break;
            entry = reinterpret_cast<FILE_ID_BOTH_DIR_INFO *>(reinterpret_cast<BYTE *>(entry) +
                                                              entry->NextEntryOffset);
        }
    }
    return result;
}
std::string read(const Ref &h) {
    if (!h)
        return {};
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h->value, &size) || size.QuadPart > 4 * 1024 * 1024)
        fail(87, L"metadata size");
    std::string data(static_cast<size_t>(size.QuadPart), '\0');
    DWORD got = 0;
    if (!ReadFile(h->value, data.data(), static_cast<DWORD>(data.size()), &got, nullptr) ||
        got != data.size())
        fail(GetLastError(), L"read metadata");
    return data;
}
std::wstring decode(const std::string &data) {
    if (data.size() >= 2 && static_cast<unsigned char>(data[0]) == 255 &&
        static_cast<unsigned char>(data[1]) == 254) {
        std::wstring result((data.size() - 2) / 2, L'\0');
        memcpy(result.data(), data.data() + 2, result.size() * 2);
        return result;
    }
    auto offset = data.starts_with("\xef\xbb\xbf") ? 3 : 0;
    const auto n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data.data() + offset,
                                       static_cast<int>(data.size()) - offset, nullptr, 0);
    std::wstring text(n, L'\0');
    if (n)
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data.data() + offset,
                            static_cast<int>(data.size()) - offset, text.data(), n);
    return text;
}
std::vector<std::wstring> lines(const std::wstring &text) {
    std::vector<std::wstring> result;
    size_t start = 0;
    while (start < text.size()) {
        const auto end = text.find(L'\n', start);
        auto line = text.substr(start, end == text.npos ? end : end - start);
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();
        if (!line.empty())
            result.push_back(line);
        if (end == text.npos)
            break;
        start = end + 1;
    }
    return result;
}
std::wstring ini(const Ref &h, const std::wstring &section, const std::wstring &key) {
    bool active = false;
    for (const auto &line : lines(decode(read(h)))) {
        if (line.front() == L'[')
            active = equal(line, L"[" + section + L"]");
        else if (active && line.find(L'=') != line.npos && equal(line.substr(0, line.find(L'=')), key))
            return line.substr(line.find(L'=') + 1);
    }
    return {};
}
std::vector<std::wstring> manifest(const std::wstring &path) {
    const auto absolute = normalize(path);
    Tree source(absolute.substr(0, absolute.rfind(L'\\')));
    auto result =
        lines(decode(read(source.file(absolute.substr(absolute.rfind(L'\\') + 1), FILE_READ_DATA))));
    std::set<std::wstring> unique;
    for (const auto &name : result) {
        parts(name);
        if (name != binary && !name.starts_with(std::wstring(resources) + L"\\"))
            fail(87, name);
        auto lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), towlower);
        if (!unique.insert(lower).second)
            fail(87, name);
    }
    if (result.empty())
        fail(87, path);
    return result;
}
void scan(const Ref &dir, const std::wstring &path, std::vector<Entry> *plan, unsigned depth = 0) {
    if (depth > 64)
        fail(87, path);
    for (const auto &name : names(dir)) {
        auto h = child(dir, name, FILE_LIST_DIRECTORY | (plan ? DELETE : 0));
        if (!h)
            continue;
        const bool directory = (attributes(h, name).FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (directory)
            scan(h, path + L"\\" + name, plan, depth + 1);
        if (plan)
            plan->push_back({h, path + L"\\" + name, directory});
    }
}
void erase(std::vector<Entry> &plan) {
    for (auto &item : plan) {
        trace(L"START disposition " + item.path);
        // A static package owns names, not arbitrary descendants. The disposition
        // call below independently enforces emptiness on the same directory object.
        if (item.directory && !names(item.handle).empty()) {
            std::wcout << L"KEEP nonempty directory " << item.path << L'\n';
            item.handle.reset();
            trace(L"END disposition KEEP nonempty");
            continue;
        }
        FILE_DISPOSITION_INFO disposition{TRUE};
        if (!SetFileInformationByHandle(item.handle->value, FileDispositionInfo, &disposition,
                                        sizeof(disposition))) {
            const auto code = GetLastError();
            if (item.directory && code == ERROR_DIR_NOT_EMPTY) {
                std::wcout << L"KEEP nonempty directory " << item.path << L'\n';
            } else
                fail(code, item.path);
        }
        item.handle.reset();
        trace(L"END disposition code=0");
    }
}
void barrier() {
#ifdef SB_FILESYSTEM_TEST
    wchar_t prefix[512]{};
    if (!GetEnvironmentVariableW(L"SB_FILESYSTEM_BARRIER", prefix, 512))
        return;
    auto ready = std::make_shared<Handle>(
        OpenEventW(EVENT_MODIFY_STATE, FALSE, (std::wstring(prefix) + L"-ready").c_str()));
    auto resume =
        std::make_shared<Handle>(OpenEventW(SYNCHRONIZE, FALSE, (std::wstring(prefix) + L"-resume").c_str()));
    if (!ready->value || !resume->value)
        fail(87, L"test barrier handles");
    std::wcout << L"BARRIER validation complete; waiting before first destructive operation\n" << std::flush;
    SetEvent(ready->value);
    if (WaitForSingleObject(resume->value, 10000) != WAIT_OBJECT_0)
        fail(1460, L"test barrier deadline");
#endif
}
void copy(const Ref &source, const Ref &destination) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(destination->value, &info))
        fail(GetLastError(), L"destination info");
    if (info.nNumberOfLinks != 1)
        fail(87, L"hard-linked destination");
    FILE_END_OF_FILE_INFO end{};
    if (!SetFileInformationByHandle(destination->value, FileEndOfFileInfo, &end, sizeof(end)))
        fail(GetLastError(), L"truncate handle");
    BYTE buffer[65536];
    DWORD count = 0, written = 0;
    for (;;) {
        if (!ReadFile(source->value, buffer, sizeof(buffer), &count, nullptr))
            fail(GetLastError(), L"read payload");
        if (!count)
            break;
        if (!WriteFile(destination->value, buffer, count, &written, nullptr) || written != count)
            fail(GetLastError(), L"write payload");
    }
}
void browserAccess(const Ref &h) {
    // Preserve the existing LPAC read/execute grant, but address each opened object.
    // No path-based icacls recursion or ACL propagation into unexpected children.
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL oldAcl = nullptr, newAcl = nullptr;
    PSID sid = nullptr;
    auto code = GetSecurityInfo(h->value, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                &oldAcl, nullptr, &descriptor);
    if (code != ERROR_SUCCESS)
        fail(code, L"browser ACL read");
    if (!ConvertStringSidToSidW(L"S-1-15-2-2", &sid)) {
        LocalFree(descriptor);
        fail(GetLastError(), L"LPAC SID");
    }
    EXPLICIT_ACCESSW ace{};
    ace.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
    ace.grfAccessMode = GRANT_ACCESS;
    ace.grfInheritance = NO_INHERITANCE;
    ace.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ace.Trustee.ptstrName = static_cast<LPWSTR>(sid);
    code = SetEntriesInAclW(1, &ace, oldAcl, &newAcl);
    if (!code)
        code = SetSecurityInfo(h->value, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, newAcl,
                               nullptr);
    LocalFree(newAcl);
    LocalFree(sid);
    LocalFree(descriptor);
    if (code)
        fail(code, L"browser ACL write");
}
int run(int argc, wchar_t **argv) {
    if (argc < 4)
        fail(87, L"arguments");
    const std::wstring mode = argv[1], rootPath = normalize(argv[2]);
    if (mode != L"install" && mode != L"uninstall" && mode != L"check")
        fail(87, mode);
    const auto files = manifest(argv[3]);
    Tree root(rootPath, mode == L"uninstall");
    auto resourceRoot = root.dir(resources);
    if (resourceRoot)
        scan(resourceRoot, resources, nullptr);
    const auto product = ini(root.file(std::wstring(resources) + L"\\package.ini", FILE_READ_DATA),
                             L"StreamBridge", L"Product");
    if ((resourceRoot && !names(resourceRoot).empty()) || root.file(binary, FILE_READ_ATTRIBUTES)) {
        if (product != L"obs-streambridge")
            fail(2, L"package ownership");
    }
    if (mode == L"uninstall") {
        const auto marker =
            ini(root.file(std::wstring(resources) + L"\\install.ini", FILE_READ_DATA), L"Install", L"Root");
        if (marker.empty() || !equal(normalize(marker), rootPath) || product != L"obs-streambridge")
            fail(2, L"installation marker");
        std::vector<Entry> purge;
        std::unique_ptr<Tree> config;
        if (argc > 4 && std::wstring(argv[4]) == L"purge") {
            std::wstring base = rootPath + L"\\config";
            if (!root.file(L"portable_mode.txt", FILE_READ_ATTRIBUTES)) {
                PWSTR appData = nullptr;
                if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)))
                    fail(5, L"AppData");
                base = appData;
                CoTaskMemFree(appData);
#ifdef SB_FILESYSTEM_TEST
                wchar_t testAppData[32768]{};
                if (GetEnvironmentVariableW(L"SB_FILESYSTEM_TEST_APPDATA", testAppData, 32768))
                    base = testAppData;
#endif
            }
            // A missing config ancestor is a valid no-settings installation.
            try {
                config = std::make_unique<Tree>(base);
            } catch (const Failure &e) {
                if (e.code != 2 && e.code != 3)
                    throw;
            }
            if (config) {
                auto h = config->dir(settings, false, true);
                if (h) {
                    scan(h, settings, &purge);
                    purge.push_back({h, settings, true});
                }
                config->dirs.clear();
                config->root.reset();
            }
        }
        std::vector<Entry> plan;
        std::set<std::wstring> directories;
        for (const auto &file : files) {
            auto h = root.file(file, DELETE);
            if (h)
                plan.push_back({h, file, false});
            auto path = file;
            while (path.find(L'\\') != path.npos) {
                path.resize(path.rfind(L'\\'));
                if (path == resources || path.starts_with(std::wstring(resources) + L"\\"))
                    directories.insert(path);
            }
        }
        // The very same directory handles used for marker validation and child opens.
        for (auto i = directories.rbegin(); i != directories.rend(); ++i) {
            if (auto h = root.dir(*i, false, true))
                plan.push_back({h, *i, true});
        }
        root.dirs.clear();
        root.root.reset();
        resourceRoot.reset();
        std::wcout << L"VALIDATED uninstall files=" << plan.size() << L" purge=" << purge.size() << L'\n';
        barrier();
        erase(plan);
        erase(purge);
    } else {
        std::unique_ptr<Tree> payload;
        if (mode == L"install") {
            if (argc != 5)
                fail(87, L"payload argument");
            payload = std::make_unique<Tree>(argv[4]);
        }
        std::vector<std::pair<Ref, Ref>> copies;
        std::vector<Ref> browserHandles;
        const auto browser = std::wstring(resources) + L"\\browser";
        for (const auto &file : files) {
            const bool inBrowser = file.starts_with(browser + L"\\");
            auto target = root.file(file, FILE_WRITE_DATA | (inBrowser ? READ_CONTROL | WRITE_DAC : 0),
                                    mode == L"install");
            if (mode == L"install") {
                auto source = payload->file(file, FILE_READ_DATA);
                if (!source || !target)
                    fail(2, file);
                copies.emplace_back(source, target);
                if (inBrowser)
                    browserHandles.push_back(target);
            }
        }
        if (mode == L"install") {
            for (const auto &[path, h] : root.dirs) {
                if (path == browser || path.starts_with(browser + L"\\")) {
                    const auto split = path.rfind(L'\\');
                    // SetSecurityInfo documents that MAXIMUM_ALLOWED handles do
                    // not propagate ACEs to children. Every owned child gets its
                    // own explicit grant; unknown/reparse children are untouched.
                    browserHandles.push_back(child(root.dir(path.substr(0, split)), path.substr(split + 1),
                                                   MAXIMUM_ALLOWED, false, false, false));
                }
            }
            barrier();
            for (const auto &pair : copies)
                copy(pair.first, pair.second);
            for (const auto &h : browserHandles)
                browserAccess(h);
        }
    }
    return 0;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    try {
        const auto code = run(argc, argv);
        std::wcout << L"END filesystem code=" << code << L'\n';
        return code;
    } catch (const Failure &e) {
        trace(L"END filesystem code=" + std::to_wstring(e.code) + L" path=" + e.path);
        std::wcout << L"END filesystem code=" << e.code << L'\n';
        return static_cast<int>(e.code);
    } catch (...) {
        std::wcerr << L"END filesystem unexpected failure\n";
        return 3;
    }
}
