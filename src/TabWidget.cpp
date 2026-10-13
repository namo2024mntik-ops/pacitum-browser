#include "TabWidget.h"
#include "ThemeManager.h"
#include <QMenu>
#include <QAction>
#include <QMouseEvent>

// SafariTabBar
SafariTabBar::SafariTabBar(bool vertical, QWidget* parent) : QTabBar(parent), m_vertical(vertical) {
    setElideMode(Qt::ElideRight); setExpanding(false); setTabsClosable(true);
    if (vertical) setMinimumWidth(200);
}
void SafariTabBar::setVerticalMode(bool v) {
    m_vertical = v;
    setMinimumWidth(v ? 200 : 0);
}
void SafariTabBar::setTabPinned(int index, bool pinned) {
    if (pinned) m_pinned.insert(index); else m_pinned.remove(index);
}
void SafariTabBar::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::RightButton) {
        int idx = tabAt(e->pos());
        if (idx >= 0) emit tabRightClicked(idx, e->globalPosition().toPoint());
        else emit emptySpaceRightClicked(e->globalPosition().toPoint());
    } else if (e->button() == Qt::MiddleButton) {
        int idx = tabAt(e->pos());
        if (idx >= 0) emit tabCloseRequested(idx);
    }
    QTabBar::mousePressEvent(e);
}

// SafariTabWidget
SafariTabWidget::SafariTabWidget(bool vertical, QWidget* parent) : QTabWidget(parent), m_vertical(vertical) {
    m_bar = new SafariTabBar(vertical, this);
    setTabBar(m_bar);
    setTabsClosable(true); setMovable(true); setDocumentMode(true);

    m_newTabBtn = new QPushButton(QStringLiteral("+"), this);
    m_newTabBtn->setFixedSize(32,32);
    connect(m_newTabBtn, &QPushButton::clicked, this, &SafariTabWidget::newTabRequested);
    updateCorner();

    connect(m_bar, &SafariTabBar::tabRightClicked, this, &SafariTabWidget::showTabContextMenu);
    connect(m_bar, &SafariTabBar::emptySpaceRightClicked, this, &SafariTabWidget::showBarContextMenu);
}

void SafariTabWidget::setVertical(bool v) {
    m_vertical = v; m_bar->setVerticalMode(v); updateCorner();
}

void SafariTabWidget::setTabPinned(int index, bool pinned) {
    if (index < 0 || index >= count()) return;
    m_bar->setTabPinned(index, pinned);
    if (pinned) {
        QWidget* w = widget(index); QString t = tabText(index); QIcon ic = tabIcon(index);
        removeTab(index);
        int newIndex = 0;
        for (int i=0;i<count();++i) if (!m_bar->isTabPinned(i)) { newIndex=i; break; }
        if (count()==0) newIndex=0;
        insertTab(newIndex, w, ic, t);
        m_bar->setTabPinned(newIndex, true);
        m_bar->setTabButton(newIndex, QTabBar::RightSide, nullptr);
        setCurrentIndex(newIndex);
    }
}

void SafariTabWidget::applyTheme(ThemeManager* tm) {
    setStyleSheet(tm->tabWidgetStyle(m_vertical));
    if (tm->isDarkMode()) m_newTabBtn->setStyleSheet(R"(QPushButton{background:transparent;border:none;border-radius:16px;font-size:20px;color:#8e8e93;} QPushButton:hover{background:#2c2c2e;color:#fff;})");
    else m_newTabBtn->setStyleSheet(R"(QPushButton{background:transparent;border:none;border-radius:16px;font-size:20px;color:#86868b;} QPushButton:hover{background:#ebebed;color:#1d1d1f;})");
}

void SafariTabWidget::updateCorner() {
    auto corner = m_vertical ? Qt::TopLeftCorner : Qt::TopRightCorner;
    setCornerWidget(m_newTabBtn, corner);
}

void SafariTabWidget::showTabContextMenu(int index, const QPoint& pos) {
    QMenu menu(this);
    // Need widget to check muted - simplified: always show Mute
    auto* closeAct = menu.addAction(QStringLiteral("Close Tab"));
    connect(closeAct, &QAction::triggered, this, [this, index]{ emit tabCloseRequested(index); });
    auto* closeOthers = menu.addAction(QStringLiteral("Close Other Tabs"));
    connect(closeOthers, &QAction::triggered, this, [this, index]{ closeOtherTabs(index); });
    auto* closeRight = menu.addAction(QStringLiteral("Close Tabs to the Right"));
    connect(closeRight, &QAction::triggered, this, [this, index]{ closeTabsToRight(index); });
    menu.addSeparator();
    auto* dup = menu.addAction(QStringLiteral("Duplicate Tab"));
    connect(dup, &QAction::triggered, this, [this, index]{ emit tabDuplicateRequested(index); });
    auto* split = menu.addAction(QStringLiteral("Open in Split View"));
    connect(split, &QAction::triggered, this, [this, index]{ emit tabSplitRequested(index); });
    menu.addSeparator();
    auto* pin = menu.addAction(m_bar->isTabPinned(index) ? QStringLiteral("Unpin Tab") : QStringLiteral("Pin Tab"));
    connect(pin, &QAction::triggered, this, [this, index]{ emit tabPinRequested(index, !m_bar->isTabPinned(index)); });
    auto* mute = menu.addAction(QStringLiteral("Mute Tab"));
    connect(mute, &QAction::triggered, this, [this, index]{ emit tabMuteRequested(index, true); });
    menu.exec(pos);
}

void SafariTabWidget::showBarContextMenu(const QPoint& pos) {
    QMenu menu(this);
    auto* nt = menu.addAction(QStringLiteral("New Tab"));
    connect(nt, &QAction::triggered, this, &SafariTabWidget::newTabRequested);
    menu.exec(pos);
}

void SafariTabWidget::closeOtherTabs(int index) {
    for (int i=count()-1;i>=0;--i) if (i!=index) emit tabCloseRequested(i);
}
void SafariTabWidget::closeTabsToRight(int index) {
    for (int i=count()-1;i>index;--i) emit tabCloseRequested(i);
}
