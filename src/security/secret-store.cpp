// SPDX-License-Identifier: GPL-2.0-or-later
#include "secret-store.hpp"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <windows.h>

#include <dpapi.h>

namespace sb {
// OAuth and browser source workers can update the same vault concurrently.
static QMutex vaultMutex;
QByteArray SecretStore::read(QString &error) const {
    error.clear();
    QFile file(directory_ + "/vault.dpapi");
    if (!file.exists())
        return "{}";
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
        error = "Cannot read encrypted credential vault.";
        return {};
    }
    auto encrypted = file.readAll();
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()), reinterpret_cast<BYTE *>(encrypted.data())};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        error = "Credential vault cannot be decrypted by this Windows account.";
        return {};
    }
    QByteArray result(reinterpret_cast<const char *>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return result;
}
QByteArray SecretStore::get(const QString &name, QString &error) const {
    QMutexLocker lock(&vaultMutex);
    auto plain = read(error);
    if (!error.isEmpty())
        return {};
    QJsonParseError parse;
    auto doc = QJsonDocument::fromJson(plain, &parse);
    SecureZeroMemory(plain.data(), static_cast<SIZE_T>(plain.size()));
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        error = "Invalid credential vault.";
        return {};
    }
    return QByteArray::fromBase64(doc.object().value(name).toString().toLatin1());
}
bool SecretStore::put(const QString &name, const QByteArray &secret, QString &error) {
    QMutexLocker lock(&vaultMutex);
    if (secret.size() > 65536) {
        error = "Credential exceeds vault size limit.";
        return false;
    }
    auto plain = read(error);
    if (!error.isEmpty())
        return false;
    QJsonParseError parse;
    auto doc = QJsonDocument::fromJson(plain, &parse);
    SecureZeroMemory(plain.data(), static_cast<SIZE_T>(plain.size()));
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        error = "Invalid credential vault; refusing to overwrite.";
        return false;
    }
    auto object = doc.object();
    if (secret.isEmpty())
        object.remove(name);
    else
        object.insert(name, QString::fromLatin1(secret.toBase64()));
    plain = QJsonDocument(object).toJson(QJsonDocument::Compact);
    DATA_BLOB input{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE *>(plain.data())};
    DATA_BLOB output{};
    const BOOL ok = CryptProtectData(&input, L"OBS StreamBridge", nullptr, nullptr, nullptr,
                                     CRYPTPROTECT_UI_FORBIDDEN, &output);
    SecureZeroMemory(plain.data(), static_cast<SIZE_T>(plain.size()));
    if (!ok) {
        error = "Windows could not encrypt credentials.";
        return false;
    }
    QDir().mkpath(directory_);
    QSaveFile file(directory_ + "/vault.dpapi");
    const bool saved =
        file.open(QIODevice::WriteOnly) &&
        file.write(reinterpret_cast<const char *>(output.pbData), output.cbData) == output.cbData &&
        file.commit();
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    if (!saved)
        error = "Cannot save encrypted credential vault.";
    return saved;
}
} // namespace sb
