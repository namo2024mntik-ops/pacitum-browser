#pragma once
#include <QTabWidget>
#include <QTabBar>
#include <QPushButton>
#include <QToolButton>
#include <QSet>

class ThemeManager;

class PacitumTabBar : public QTabBar {
    Q_OBJECT
public:
    explicit PacitumTabBar(bool vertical = false, QWidget* parent = nullptr);
    void setVerticalMode(bool v);
    bool isTabPinned(int index) const { return m_pinned.contains(index); }
    void setTabPinned(int index, bool pinned);

signals:
    void tabRightClicked(int index, const QPoint& globalPos);
    void emptySpaceRightClicked(const QPoint& globalPos);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    bool m_vertical = false;
    QSet<int> m_pinned;
};

// QTabWidget whose (horizontal) tabs can be shown in a separate "TabStrip" widget placed
// above the navigation toolbar, as in the Pacitum design (TabStrip 40 → Toolbar 44 → Bookmarks 32).
class PacitumTabWidget : public QTabWidget {
    Q_OBJECT
public:
    explicit PacitumTabWidget(bool vertical = false, QWidget* parent = nullptr);
    void setVertical(bool v);
    bool isVertical() const { return m_vertical; }
    void setTabPinned(int index, bool pinned);
    void applyTheme(ThemeManager* tm);

    // Creates the external tab strip (call once, for the main tab widget).
    QWidget* createTabStrip(QWidget* parent);

    // Shadow QTabWidget setters so the external strip stays in sync.
    void setTabText(int index, const QString& text);
    void setTabIcon(int index, const QIcon& icon);
    void setTabToolTip(int index, const QString& tip);

signals:
    void newTabRequested();
    void tabSplitRequested(int index);
    void tabDuplicateRequested(int index);
    void tabMuteRequested(int index, bool muted);
    void tabPinRequested(int index, bool pinned);
    void tabReopenRequested();
    void stripDoubleClicked();

protected:
    void tabInserted(int index) override;
    void tabRemoved(int index) override;
    void showEvent(QShowEvent* e) override;

private slots:
    void showTabContextMenu(int index, const QPoint& pos);
    void showBarContextMenu(const QPoint& pos);
    void showTabList();

private:
    void closeOtherTabs(int index);
    void closeTabsToRight(int index);
    void updateCorner();
    void updateBarVisibility();
    PacitumTabBar* activeBar() const { return m_ext ? m_ext : m_bar; }

    bool m_vertical = false;
    bool m_syncing = false;
    PacitumTabBar* m_bar = nullptr;
    PacitumTabBar* m_ext = nullptr;
    QWidget* m_strip = nullptr;
    QToolButton* m_newTabBtn = nullptr;
    QToolButton* m_stripNewTab = nullptr;
    QToolButton* m_tabListBtn = nullptr;
};
