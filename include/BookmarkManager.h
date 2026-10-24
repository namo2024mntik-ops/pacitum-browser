#pragma once
#include <QString>
#include <QVector>
#include <QJsonArray>
#include <QPair>
#include <QStringList>

struct Bookmark {
    QString title;
    QString url;
    QString added;  // ISO 8601
    QString folder; // 1.9.0 / 1.8.11: "" = straight on the bookmarks bar, else the folder's name
};

class BookmarkManager {
public:
    explicit BookmarkManager(const QString& file = QStringLiteral("bookmarks.json"));

    bool load();
    bool save() const;

    bool addBookmark(const QString& title, const QString& url, const QString& folder = QString());
    // Bulk add (import): skips duplicates, saves once. Returns the number added.
    int addBookmarks(const QVector<QPair<QString, QString>>& titleUrl);
    void removeBookmark(const QString& url);
    bool isBookmarked(const QString& url) const;
    QVector<Bookmark> allBookmarks() const { return m_bookmarks; }
    QVector<Bookmark> search(const QString& query) const;

    // Folders (one level, like the folders on Chrome's bookmarks bar). Their order lives in the
    // "bookmark_folders" setting, so an empty folder stays and the order syncs with the settings.
    QStringList folders() const;                       // ordered, including empty ones
    QVector<Bookmark> bookmarksIn(const QString& folder) const;
    bool addFolder(const QString& name);               // false: empty name or it exists
    bool renameFolder(const QString& from, const QString& to);
    void removeFolder(const QString& name, bool keepBookmarks);   // keep = move them to the bar
    void moveToFolder(const QString& url, const QString& folder); // "" = back to the bar
    void setTitle(const QString& url, const QString& title);
    void setFolderOrder(const QStringList& order);

private:
    QString m_file;
    QVector<Bookmark> m_bookmarks;
};
