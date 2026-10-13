#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

// One owner for browser_settings.json (1.5.2). Before, four components (BrowserWindow, SettingsPage,
// FirstRun, DownloadManager) each opened and truncated the file themselves, and BrowserWindow kept a
// cache keyed on mtime+size, which misses same-size rewrites made within the same second (e.g.
// "Dark" -> "Light" accent swaps) and could then write the stale copy back. Now every read and
// write goes through here: an in-memory copy, a revision counter bumped on every write, atomic
// QSaveFile writes, and a cheap stat so edits made by another process are still picked up.
namespace AppSettings {
QJsonObject read();
void write(const QJsonObject& all);
void set(const QString& key, const QJsonValue& value);
inline QJsonValue get(const QString& key, const QJsonValue& def = {}) {
    const QJsonValue v = read().value(key); return v.isUndefined() ? def : v;
}
quint64 revision();
}
