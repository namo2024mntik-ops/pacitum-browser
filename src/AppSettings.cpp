#include "AppSettings.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>

namespace {
const QString kFile = QStringLiteral("browser_settings.json");
QMutex g_mx;
QJsonObject g_cache;
bool g_loaded = false;
QDateTime g_stamp;
qint64 g_size = -1;
quint64 g_rev = 0;

void reloadLocked() {
    const QFileInfo fi(kFile);
    if (!fi.exists()) { g_cache = {}; g_stamp = {}; g_size = -1; g_loaded = true; return; }
    if (g_loaded && fi.size() == g_size && fi.lastModified() == g_stamp) return;
    QFile f(kFile);
    if (!f.open(QIODevice::ReadOnly)) return;   // keep the last good copy
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isObject()) g_cache = doc.object();
    else if (!g_loaded) g_cache = {};             // corrupt file on first read: start empty, never crash
    g_stamp = fi.lastModified(); g_size = fi.size(); g_loaded = true;
}
}

namespace AppSettings {
QJsonObject read() {
    QMutexLocker lk(&g_mx);
    reloadLocked();
    return g_cache;
}
void write(const QJsonObject& all) {
    QMutexLocker lk(&g_mx);
    reloadLocked();
    if (g_loaded && all == g_cache && QFileInfo::exists(kFile)) return;
    QSaveFile f(kFile);   // atomic replace: never a half-written file
    if (f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(all).toJson(QJsonDocument::Indented)) >= 0 && f.commit()) {
        g_cache = all; ++g_rev;
        const QFileInfo fi(kFile); g_stamp = fi.lastModified(); g_size = fi.size(); g_loaded = true;
    }
}
void set(const QString& key, const QJsonValue& value) {
    QJsonObject s = read();
    if (s.value(key) == value) return;
    s[key] = value;
    write(s);
}
quint64 revision() { QMutexLocker lk(&g_mx); return g_rev; }
}
