// SPDX-License-Identifier: GPL-2.0-or-later
#include "security/secret-store.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>
static void check(bool ok, const char *message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    check(temp.isValid(), "temporary directory");
    sb::SecretStore vault(temp.path());
    QString error;
    const QByteArray secret("test-only-secret-dont-use-on-platforms");
    check(vault.put("YouTube/stream-key", secret, error), "DPAPI encrypt/save");
    check(vault.get("YouTube/stream-key", error) == secret, "DPAPI decrypt/read");
    QFile disk(temp.path() + "/vault.dpapi");
    check(disk.open(QIODevice::ReadOnly), "read ciphertext");
    auto bytes = disk.readAll();
    check(!bytes.contains(secret) && !bytes.contains(secret.toBase64()), "secret absent from file");
    disk.close();
    check(vault.erase("YouTube/stream-key", error), "delete secret");
    check(vault.get("YouTube/stream-key", error).isEmpty(), "secret removed");
    check(disk.open(QIODevice::WriteOnly | QIODevice::Truncate), "corruption fixture");
    disk.write("invalid ciphertext");
    disk.close();
    check(vault.get("YouTube/stream-key", error).isEmpty() && !error.isEmpty(), "corrupt vault fails closed");
    check(!vault.put("Twitch/stream-key", secret, error), "corrupt vault never overwritten");
    std::cout << "PASS: DPAPI round trip, ciphertext, deletion, corruption\n";
}
