// SPDX-License-Identifier: GPL-2.0-or-later
#include "core.hpp"
#include <fstream>
#include <iostream>
#include <winrt/base.h>

void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
    std::cout << "PASS: " << message << '\n';
}
template <typename F> void rejected(F test, const char *message) {
    bool failed = false;
    try {
        test();
    } catch (...) {
        failed = true;
    }
    check(failed, message);
}
int main() {
    try {
        winrt::init_apartment();
        const std::string digest = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
        const auto asset =
            std::string(
                R"({"name":"obs-streambridge-0.1.2-windows-x64-setup.exe","state":"uploaded","size":3,"digest":"sha256:)") +
            digest + R"(","browser_download_url":")" + winrt::to_string(bootstrap::assetUrl) + R"("})";
        const auto json =
            std::string(R"({"tag_name":"v0.1.2","draft":false,"prerelease":true,"assets":[)") + asset + "]}";
        const auto release = bootstrap::parseRelease(json);
        check(release.size == 3 && winrt::to_string(release.digest) == digest,
              "fixed preview release metadata accepted");
        auto bad = json;
        bad.replace(bad.find("v0.1.2"), 6, "v0.1.3");
        rejected([&] { bootstrap::parseRelease(bad); }, "other release rejected");
        bad = json;
        bad.replace(bad.find("false"), 5, "true");
        rejected([&] { bootstrap::parseRelease(bad); }, "draft rejected");
        bad = json;
        bad.replace(bad.find("sha256:"), 7, "sha512:");
        rejected([&] { bootstrap::parseRelease(bad); }, "missing SHA-256 rejected");
        bad = json;
        bad.replace(bad.find("\"size\":3"), 8, "\"size\":1073741825");
        rejected([&] { bootstrap::parseRelease(bad); }, "oversized package rejected");
        bad = json;
        bad.replace(bad.find("\"size\":3"), 8, "\"size\":1.5");
        rejected([&] { bootstrap::parseRelease(bad); }, "fractional size rejected");
        bad = json;
        bad.replace(bad.find("https://github.com/"), 19, "https://evil.test/");
        rejected([&] { bootstrap::parseRelease(bad); }, "foreign download origin rejected");
        bad = json;
        bad.insert(bad.size() - 2, "," + asset);
        rejected([&] { bootstrap::parseRelease(bad); }, "duplicate assets rejected");
        rejected([&] { bootstrap::parseRelease(std::string(2 * 1024 * 1024 + 1, ' ')); },
                 "oversized metadata rejected");
        rejected([&] { bootstrap::parseRelease("{}"); }, "missing metadata rejected");
        check(bootstrap::allowedDownloadUrl(bootstrap::assetUrl), "exact asset origin allowed");
        check(bootstrap::allowedDownloadUrl(
                  L"https://release-assets.githubusercontent.com/item?signature=value"),
              "GitHub signed CDN allowed");
        for (const auto *url : {L"http://release-assets.githubusercontent.com/item",
                                L"https://release-assets.githubusercontent.com.evil.test/item",
                                L"https://user@release-assets.githubusercontent.com/item",
                                L"https://release-assets.githubusercontent.com:444/item",
                                L"https://release-assets.githubusercontent.com/item#fragment",
                                L"https://github.com/other/repo/file.exe"})
            check(!bootstrap::allowedDownloadUrl(url), "unsafe redirect rejected");
        const auto directory = std::filesystem::temp_directory_path() /
                               (L"StreamBridge-unit-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directory(directory);
        const auto file = directory / L"payload.bin";
        std::ofstream(file, std::ios::binary) << "abc";
        check(bootstrap::verifyFile(file, release), "known SHA-256 matches exact bytes");
        std::ofstream(file, std::ios::binary) << "abd";
        check(!bootstrap::verifyFile(file, release), "same-length tampering rejected");
        std::ofstream(file, std::ios::binary) << "ab";
        check(!bootstrap::verifyFile(file, release), "truncated file rejected");
        std::ofstream(file, std::ios::binary) << "abcd";
        check(!bootstrap::verifyFile(file, release), "oversized file rejected");
        std::filesystem::remove(file);
        check(!bootstrap::verifyFile(file, release), "missing file rejected");
        std::filesystem::remove(directory);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
    } catch (const bootstrap::Failure &error) {
        std::cerr << winrt::to_string(error.message) << '\n';
    }
    return 1;
}
