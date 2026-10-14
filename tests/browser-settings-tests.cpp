// SPDX-License-Identifier: GPL-2.0-or-later
#include "browser/settings.hpp"
#include "security/secret-store.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>
static void check(bool value, const char *message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    QString error;
    sb::BrowserConfig config;
    config.url = "https://widget.invalid/secret-widget-token";
    config.proxy.host = "127.0.0.1";
    config.proxy.port = 1080;
    config.proxy.username = "test-user";
    config.proxy.password = "test-password";
    auto encoded = sb::encodeBrowserConfig(config);
    const auto decoded = sb::decodeBrowserConfig(encoded, error);
    check(error.isEmpty() && decoded.url == config.url && decoded.proxy.password == config.proxy.password,
          "browser configuration round trip");
    sb::SecretStore vault(directory.path());
    check(vault.put("browser/source-test-uuid", encoded, error), "encrypted browser persistence");
    QFile file(directory.path() + "/vault.dpapi");
    check(file.open(QIODevice::ReadOnly), "ciphertext readable");
    const auto bytes = file.readAll();
    check(!bytes.contains("secret-widget-token") && !bytes.contains("test-password") &&
              !bytes.contains(encoded.toBase64()),
          "URL and proxy secrets absent from vault ciphertext");
    check(vault.get("browser/source-test-uuid", error) == encoded, "encrypted browser settings restored");
    config.url = "http://widget.invalid/";
    check(!sb::validateBrowser(config).isEmpty(), "plaintext URL rejected");
    config.url = "https://user:password@widget.invalid/";
    check(!sb::validateBrowser(config).isEmpty(), "URL user info rejected");
    config.url = "https://widget.invalid/";
    config.width = 1921;
    check(!sb::validateBrowser(config).isEmpty(), "oversized frame rejected");
    config.width = 800;
    config.proxy.protocol = static_cast<sb::ProxyProtocol>(99);
    check(!sb::validateBrowser(config).isEmpty(), "unknown proxy protocol rejected");
    sb::decodeBrowserConfig(QByteArray(65537, 'x'), error);
    check(!error.isEmpty(), "oversized configuration rejected");
    sb::decodeBrowserConfig("{\"version\":99}", error);
    check(!error.isEmpty(), "unknown version rejected");
    std::cout << "PASS: browser configuration validation and DPAPI confidentiality\n";
}
