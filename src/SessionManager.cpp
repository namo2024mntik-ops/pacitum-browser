#include "SessionManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>

SessionManager::SessionManager(const QString& file) : m_file(file) {}

bool SessionManager::save(const QVector<SessionTab>& tabs) {
    QJsonArray arr;
    for (auto &t : tabs) arr.append(QJsonObject{{"url", t.url},{"title", t.title},{"pinned", t.pinned}});
    QJsonObject obj{{"timestamp", QDateTime::currentSecsSinceEpoch()}, {"tabs", arr}};
    QFile f(m_file);
    if (!f.open(QIODevice::WriteOnly|QIODevice::Truncate)) return false;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}

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
