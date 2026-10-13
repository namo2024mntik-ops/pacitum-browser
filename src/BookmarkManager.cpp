#include "BookmarkManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>

BookmarkManager::BookmarkManager(const QString& file) : m_file(file) { load(); }

bool BookmarkManager::load() {
    QFile f(m_file);
    if (!f.exists()) { m_bookmarks.clear(); return true; }
    if (!f.open(QIODevice::ReadOnly)) return false;
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return false;
    m_bookmarks.clear();
    for (auto v : doc.array()) {
        auto o = v.toObject();
        m_bookmarks.push_back({ o["title"].toString(), o["url"].toString(), o["added"].toString() });
    }
    return true;
}

bool BookmarkManager::save() const {
    QJsonArray arr;
    for (auto &b : m_bookmarks) arr.append(QJsonObject{{"title", b.title},{"url", b.url},{"added", b.added}});
    QFile f(m_file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    return true;
}

bool BookmarkManager::addBookmark(const QString& title, const QString& url) {
    if (isBookmarked(url)) return false;
    m_bookmarks.push_back({title, url, QDateTime::currentDateTime().toString(Qt::ISODate)});
    save();
    return true;
}

void BookmarkManager::removeBookmark(const QString& url) {
    m_bookmarks.erase(std::remove_if(m_bookmarks.begin(), m_bookmarks.end(),
        [&](const Bookmark& b){ return b.url == url; }), m_bookmarks.end());
    save();
}

bool BookmarkManager::isBookmarked(const QString& url) const {
    for (auto &b : m_bookmarks) if (b.url == url) return true;
    return false;
}

QVector<Bookmark> BookmarkManager::search(const QString& q) const {
    QVector<Bookmark> r;
    QString lq = q.toLower();
    for (auto &b : m_bookmarks) if (b.title.toLower().contains(lq) || b.url.toLower().contains(lq)) r.push_back(b);
    return r;
}
