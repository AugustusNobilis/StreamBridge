// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <windows.h>

namespace bootstrap {
inline constexpr auto version = L"0.2.5";
inline constexpr auto assetName = L"obs-streambridge-0.2.5-windows-x64-setup.exe";
inline constexpr auto assetUrl = L"https://github.com/AugustusNobilis/StreamBridge/releases/download/v0.2.5/"
                                 L"obs-streambridge-0.2.5-windows-x64-setup.exe";
struct Failure {
    std::wstring message;
};
struct Release {
    std::uint64_t size{};
    std::wstring digest;
};
using Progress = std::function<void(const std::wstring &, int)>;
Release parseRelease(const std::string &json);
bool allowedDownloadUrl(const std::wstring &url);
std::wstring sha256(HANDLE file);
bool verifyFile(const std::filesystem::path &file, const Release &release);
struct Download {
    std::filesystem::path file;
    Release release;
};
Download download(const std::filesystem::path &cache, std::atomic_bool &cancel, const Progress &progress);
void cleanup(const Download &download);
bool isObsRoot(const std::filesystem::path &root);
std::filesystem::path findObs();
// Calls the verified installer. It never terminates OBS or removes user settings.
void install(const Download &download, const std::filesystem::path &root, const Progress &progress,
             HWND owner = nullptr);
} // namespace bootstrap
