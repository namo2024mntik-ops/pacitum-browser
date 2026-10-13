#pragma once
#include <QWidget>
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

private:
    void showContextMenu(const QPoint& pos, const QString& url, QWidget* btn);

    BookmarkManager* m_mgr = nullptr;
    QHBoxLayout* m_innerLayout = nullptr;
    QWidget* m_inner = nullptr;
    ThemeManager* m_theme = nullptr;
    class QPushButton* m_other = nullptr;
};
