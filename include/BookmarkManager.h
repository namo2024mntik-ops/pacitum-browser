#pragma once
#include <QString>
#include <QVector>
#include <QJsonArray>

struct Bookmark {
    QString title;
    QString url;
    QString added; // ISO 8601
};

class BookmarkManager {
public:
    explicit BookmarkManager(const QString& file = QStringLiteral("bookmarks.json"));

    bool load();
    bool save() const;

    bool addBookmark(const QString& title, const QString& url);
    void removeBookmark(const QString& url);
    bool isBookmarked(const QString& url) const;
    QVector<Bookmark> allBookmarks() const { return m_bookmarks; }
    QVector<Bookmark> search(const QString& query) const;

private:
    QString m_file;
    QVector<Bookmark> m_bookmarks;
};
