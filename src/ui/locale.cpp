// SPDX-License-Identifier: GPL-2.0-or-later
#include "locale.hpp"
#include <QDir>
#include <QSettings>
#include <util/bmem.h>
#include <util/text-lookup.h>
namespace sb {
static lookup_t *lookup = nullptr;
static QString preferences, selected;
QString text(const char *key) {
    const char *value = nullptr;
    if (lookup && text_lookup_getstr(lookup, key, &value))
        return QString::fromUtf8(value);
    return QString::fromUtf8(obs_module_text(key));
}
void initializeLocale(const QString &directory) {
    shutdownLocale();
    QDir().mkpath(directory);
    preferences = directory + "/ui.ini";
    selected = QSettings(preferences, QSettings::IniFormat).value("language").toString();
    if (selected != "ru-RU" && selected != "en-US")
        selected.clear();
    if (selected.isEmpty())
        return;
    char *english = obs_module_file("locale/en-US.ini");
    if (english) {
        lookup = text_lookup_create(english);
        bfree(english);
    }
    if (lookup && selected != "en-US") {
        char *path = obs_module_file(("locale/" + selected + ".ini").toUtf8().constData());
        if (path) {
            text_lookup_add(lookup, path);
            bfree(path);
        }
    }
}
void shutdownLocale() {
    if (lookup) {
        text_lookup_destroy(lookup);
        lookup = nullptr;
    }
}
QString language() {
    return selected;
}
bool setLanguage(const QString &value) {
    if (value != "ru-RU" && value != "en-US" && !value.isEmpty())
        return false;
    QSettings settings(preferences, QSettings::IniFormat);
    settings.setValue("language", value);
    settings.sync();
    return settings.status() == QSettings::NoError;
}
} // namespace sb
