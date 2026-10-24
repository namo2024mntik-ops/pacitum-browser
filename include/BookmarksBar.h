#pragma once
#include <QWidget>
#include <QSet>
#include <QHBoxLayout>

class BookmarkManager;
class ThemeManager;

class BookmarksBar : public QWidget {
    Q_OBJECT
public:
    explicit BookmarksBar(BookmarkManager* mgr, QWidget* parent = nullptr);
    void refresh();
    void applyTheme(ThemeManager* tm);

signals:
    void urlActivated(const QString& url);
    void urlActivatedInNewTab(const QString& url);
    void bookmarksChanged();   // edited from the bar (the window refreshes its star)

protected:
    void contextMenuEvent(QContextMenuEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void showContextMenu(const QPoint& pos, const QString& url, QWidget* btn);
    void showFolderMenu(const QString& folder, QWidget* btn);
    void showFolderContextMenu(const QPoint& pos, const QString& folder, QWidget* btn);
    void newFolder(const QString& moveUrl = QString());
    void editBookmark(const QString& url);
    void changed();   // save done: refresh + bookmarksChanged
    void addFolderMenu(class QMenu* m, const QString& folder);

    BookmarkManager* m_mgr = nullptr;
    QHBoxLayout* m_innerLayout = nullptr;
    QWidget* m_inner = nullptr;
    ThemeManager* m_theme = nullptr;
    QSet<QString> m_shownUrls; bool m_primed = false;
    class QPushButton* m_other = nullptr;
};
