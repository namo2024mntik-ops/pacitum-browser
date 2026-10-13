#pragma once
#include <QTabWidget>
#include <QTabBar>
#include <QPushButton>
#include <QSet>

class SafariTabBar : public QTabBar {
    Q_OBJECT
public:
    explicit SafariTabBar(bool vertical = false, QWidget* parent = nullptr);
    void setVerticalMode(bool v);
    bool isTabPinned(int index) const { return m_pinned.contains(index); }
    void setTabPinned(int index, bool pinned);

signals:
    void tabRightClicked(int index, const QPoint& globalPos);
    void emptySpaceRightClicked(const QPoint& globalPos);

protected:
    void mousePressEvent(QMouseEvent* e) override;

private:
    bool m_vertical = false;
    QSet<int> m_pinned;
};

class SafariTabWidget : public QTabWidget {
    Q_OBJECT
public:
    explicit SafariTabWidget(bool vertical = false, QWidget* parent = nullptr);
    void setVertical(bool v);
    bool isVertical() const { return m_vertical; }
    void setTabPinned(int index, bool pinned);
    void applyTheme(class ThemeManager* tm);

signals:
    void newTabRequested();
    void tabSplitRequested(int index);
    void tabDuplicateRequested(int index);
    void tabMuteRequested(int index, bool muted);
    void tabPinRequested(int index, bool pinned);
    void tabReopenRequested();

private slots:
    void showTabContextMenu(int index, const QPoint& pos);
    void showBarContextMenu(const QPoint& pos);

private:
    void closeOtherTabs(int index);
    void closeTabsToRight(int index);
    void updateCorner();

    bool m_vertical = false;
    SafariTabBar* m_bar = nullptr;
    QPushButton* m_newTabBtn = nullptr;
};
