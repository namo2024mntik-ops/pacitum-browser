#include "SessionManager.h"
#include <QFile>
#include <QSaveFile>
#include <QMutex>
#include <QtConcurrent/QtConcurrentRun>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>

SessionManager::SessionManager(const QString& file) : m_file(file) {}

bool SessionManager::save(const QVector<SessionTab>& tabs) {
    QJsonArray arr;
    for (auto &t : tabs) arr.append(QJsonObject{{"url", t.url},{"title", t.title},{"pinned", t.pinned}});
    QJsonObject obj{{"timestamp", QDateTime::currentSecsSinceEpoch()}, {"tabs", arr}};
    const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    // Atomic (temp file + rename): a crash or power loss mid-write no longer leaves an empty session.
    static QMutex mx; QMutexLocker lk(&mx);
    QSaveFile f(m_file);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(data);
    return f.commit();
}

void SessionManager::saveAsync(const QVector<SessionTab>& tabs) {
    m_last = QtConcurrent::run([file = m_file, tabs]{ SessionManager(file).save(tabs); });
}

void SessionManager::waitForPendingSave() { m_last.waitForFinished(); }

QVector<SessionTab> SessionManager::restore() {
    QFile f(m_file);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return {};
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return {};
    auto obj = doc.object();
    double ts = obj["timestamp"].toDouble(0);
    if (QDateTime::currentSecsSinceEpoch() - ts > 86400*7) return {};
    QVector<SessionTab> r;
    for (auto v : obj["tabs"].toArray()) {
        auto o=v.toObject();
        if (!o["url"].toString().isEmpty()) r.push_back({o["url"].toString(), o["title"].toString(), o["pinned"].toBool()});
    }
    return r;
}

void SessionManager::clear() { QFile::remove(m_file); }
