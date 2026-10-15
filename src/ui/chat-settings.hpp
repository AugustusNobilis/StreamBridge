// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QSettings>
#include <QWidget>
#include <memory>
namespace sb {
class ChatPreferences : public QObject {
    Q_OBJECT
  public:
    explicit ChatPreferences(const QString &directory);
    bool enabled() const;
    QString sound() const;
    QString customFile() const;
    int volume() const;
    void set(const QString &key, const QVariant &value);
  signals:
    void changed();

  private:
    QSettings settings_;
};
std::shared_ptr<ChatPreferences> chatPreferences(const QString &directory);
// Returns bounded PCM WAV with gain applied to samples, never to system volume.
QByteArray chatSoundWave(const QString &file, int volume, QString &error);
class ChatSound : public QObject {
  public:
    explicit ChatSound(QObject *parent);
    ~ChatSound() override;
    bool play(const ChatPreferences &preferences, QString &error);

  private:
    struct State;
    std::unique_ptr<State> state_;
};
QWidget *createChatSettings(const QString &directory, QWidget *parent);
} // namespace sb
