// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>
#include <QString>
namespace sb {
// Windows DPAPI, user scope. One atomically replaced encrypted vault per OBS configuration directory.
class SecretStore {
  public:
    explicit SecretStore(QString directory) : directory_(std::move(directory)) {}
    QByteArray get(const QString &name, QString &error) const;
    bool put(const QString &name, const QByteArray &secret, QString &error);
    bool erase(const QString &name, QString &error) { return put(name, {}, error); }

  private:
    QByteArray read(QString &error) const;
    QString directory_;
};
} // namespace sb
