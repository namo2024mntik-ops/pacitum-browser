#include "TabWidget.h"
#include "ThemeManager.h"
#include <QMenu>
#include <QAction>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QShowEvent>

// PacitumTabBar
PacitumTabBar::PacitumTabBar(bool vertical, QWidget* parent) : QTabBar(parent), m_vertical(vertical) {
    setElideMode(Qt::ElideRight); setExpanding(false); setTabsClosable(true);
    setDrawBase(false); setIconSize(QSize(16,16)); setUsesScrollButtons(true);
    if (vertical) setMinimumWidth(200);
}
void PacitumTabBar::setVerticalMode(bool v) {
    m_vertical = v;
    setMinimumWidth(v ? 200 : 0);
}
void PacitumTabBar::setTabPinned(int index, bool pinned) {
    if (pinned) m_pinned.insert(index); else m_pinned.remove(index);
}
void PacitumTabBar::mousePressEvent(QMouseEvent* e) {
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
void PacitumTabBar::mouseDoubleClickEvent(QMouseEvent* e) {
    if (tabAt(e->pos()) < 0 && window()) {
        window()->isMaximized() ? window()->showNormal() : window()->showMaximized();
    }
    QTabBar::mouseDoubleClickEvent(e);
}

// PacitumTabWidget
PacitumTabWidget::PacitumTabWidget(bool vertical, QWidget* parent) : QTabWidget(parent), m_vertical(vertical) {
    m_bar = new PacitumTabBar(vertical, this);
    setTabBar(m_bar);
    setTabsClosable(true); setMovable(true); setDocumentMode(true);

    m_newTabBtn = new QToolButton(this);
    m_newTabBtn->setFixedSize(28,28); m_newTabBtn->setIconSize(QSize(18,18)); m_newTabBtn->setAutoRaise(true);
    m_newTabBtn->setToolTip(QStringLiteral("New tab (Ctrl+T)"));
    connect(m_newTabBtn, &QToolButton::clicked, this, &PacitumTabWidget::newTabRequested);
    updateCorner();

    connect(m_bar, &PacitumTabBar::tabRightClicked, this, &PacitumTabWidget::showTabContextMenu);
    connect(m_bar, &PacitumTabBar::emptySpaceRightClicked, this, &PacitumTabWidget::showBarContextMenu);
}

QWidget* PacitumTabWidget::createTabStrip(QWidget* parent) {
    if (m_strip) return m_strip;
    m_strip = new QWidget(parent);
    m_strip->setObjectName(QStringLiteral("TabStrip"));
    m_strip->setAttribute(Qt::WA_StyledBackground, true);
    m_strip->setFixedHeight(40);
    auto* lay = new QHBoxLayout(m_strip);
    lay->setContentsMargins(12,0,8,0); lay->setSpacing(0);

    m_ext = new PacitumTabBar(false, m_strip);
    m_ext->setMovable(true);
    m_ext->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_ext->setFixedHeight(40);
    lay->addWidget(m_ext, 0, Qt::AlignBottom);

    m_stripNewTab = new QToolButton(m_strip);
    m_stripNewTab->setFixedSize(28,28); m_stripNewTab->setIconSize(QSize(18,18)); m_stripNewTab->setAutoRaise(true);
    m_stripNewTab->setToolTip(QStringLiteral("New tab (Ctrl+T)"));
    connect(m_stripNewTab, &QToolButton::clicked, this, &PacitumTabWidget::newTabRequested);
    lay->addSpacing(6);
    lay->addWidget(m_stripNewTab, 0, Qt::AlignVCenter);
    lay->addStretch(1);

    m_tabListBtn = new QToolButton(m_strip);
    m_tabListBtn->setFixedSize(46,40); m_tabListBtn->setIconSize(QSize(16,16)); m_tabListBtn->setAutoRaise(true);
    m_tabListBtn->setToolTip(QStringLiteral("Search tabs"));
    connect(m_tabListBtn, &QToolButton::clicked, this, &PacitumTabWidget::showTabList);
    lay->addWidget(m_tabListBtn);

    // existing tabs
    for (int i = 0; i < count(); ++i) m_ext->addTab(tabIcon(i), tabText(i));
    m_ext->setCurrentIndex(currentIndex());

    connect(m_ext, &QTabBar::currentChanged, this, [this](int i){ if (!m_syncing && i >= 0) { m_syncing = true; setCurrentIndex(i); m_syncing = false; } });
    connect(this, &QTabWidget::currentChanged, this, [this](int i){ if (!m_syncing && m_ext && i >= 0) { m_syncing = true; m_ext->setCurrentIndex(i); m_syncing = false; } });
    connect(m_ext, &QTabBar::tabMoved, this, [this](int from, int to){ if (!m_syncing) { m_syncing = true; m_bar->moveTab(from, to); m_syncing = false; } });
    connect(m_ext, &QTabBar::tabCloseRequested, this, &QTabWidget::tabCloseRequested);
    connect(m_ext, &PacitumTabBar::tabRightClicked, this, &PacitumTabWidget::showTabContextMenu);
    connect(m_ext, &PacitumTabBar::emptySpaceRightClicked, this, &PacitumTabWidget::showBarContextMenu);

    updateCorner();
    updateBarVisibility();
    return m_strip;
}

void PacitumTabWidget::setTabText(int index, const QString& text) {
    QTabWidget::setTabText(index, text);
    if (m_ext && index >= 0 && index < m_ext->count()) m_ext->setTabText(index, text);
}
void PacitumTabWidget::setTabIcon(int index, const QIcon& icon) {
    QTabWidget::setTabIcon(index, icon);
    if (m_ext && index >= 0 && index < m_ext->count()) m_ext->setTabIcon(index, icon);
}
void PacitumTabWidget::setTabToolTip(int index, const QString& tip) {
    QTabWidget::setTabToolTip(index, tip);
    if (m_ext && index >= 0 && index < m_ext->count()) m_ext->setTabToolTip(index, tip);
}

void PacitumTabWidget::tabInserted(int index) {
    QTabWidget::tabInserted(index);
    if (m_ext) {
        m_syncing = true;
        m_ext->insertTab(index, tabIcon(index), tabText(index));
        m_ext->setCurrentIndex(currentIndex());
        m_syncing = false;
    }
}
void PacitumTabWidget::tabRemoved(int index) {
    QTabWidget::tabRemoved(index);
    if (m_ext && index < m_ext->count()) {
        m_syncing = true;
        m_ext->removeTab(index);
        if (currentIndex() >= 0) m_ext->setCurrentIndex(currentIndex());
        m_syncing = false;
    }
}

void PacitumTabWidget::showEvent(QShowEvent* e) {
    QTabWidget::showEvent(e);
    updateBarVisibility();
}

void PacitumTabWidget::updateBarVisibility() {
    const bool external = m_ext && !m_vertical;
    m_bar->setVisible(!external);
    if (m_ext) m_ext->setVisible(external);
    if (m_stripNewTab) m_stripNewTab->setVisible(external);
    if (m_tabListBtn) m_tabListBtn->setVisible(external);
    m_newTabBtn->setVisible(!external);
}

void PacitumTabWidget::setVertical(bool v) {
    m_vertical = v; m_bar->setVerticalMode(v);
    setTabPosition(v ? QTabWidget::West : QTabWidget::North);
    updateCorner(); updateBarVisibility();
}

void PacitumTabWidget::setTabPinned(int index, bool pinned) {
    if (index < 0 || index >= count()) return;
    m_bar->setTabPinned(index, pinned);
    if (m_ext) m_ext->setTabPinned(index, pinned);
    if (pinned) {
        QWidget* w = widget(index); QString t = tabText(index); QIcon ic = tabIcon(index);
        removeTab(index);
        int newIndex = 0;
        for (int i=0;i<count();++i) if (!m_bar->isTabPinned(i)) { newIndex=i; break; }
        if (count()==0) newIndex=0;
        insertTab(newIndex, w, ic, t);
        m_bar->setTabPinned(newIndex, true);
        m_bar->setTabButton(newIndex, QTabBar::RightSide, nullptr);
        if (m_ext) { m_ext->setTabPinned(newIndex, true); m_ext->setTabButton(newIndex, QTabBar::RightSide, nullptr); }
        setCurrentIndex(newIndex);
    }
}

void PacitumTabWidget::applyTheme(ThemeManager* tm) {
    const QString qss = tm->tabWidgetStyle(m_vertical);
    setStyleSheet(qss);
    if (m_strip) m_ext->setStyleSheet(tm->tabWidgetStyle(false));
    const QIcon add = tm->secondaryIcon(QStringLiteral("add"), 18);
    m_newTabBtn->setIcon(add);
    if (m_stripNewTab) m_stripNewTab->setIcon(add);
    if (m_tabListBtn) m_tabListBtn->setIcon(tm->secondaryIcon(QStringLiteral("chevron-down"), 16));
}

void PacitumTabWidget::updateCorner() {
    auto corner = m_vertical ? Qt::TopLeftCorner : Qt::TopRightCorner;
    setCornerWidget(m_newTabBtn, corner);
}

void PacitumTabWidget::showTabList() {
    QMenu menu(this);
    for (int i = 0; i < count(); ++i) {
        auto* a = menu.addAction(tabIcon(i), tabText(i));
        a->setCheckable(true); a->setChecked(i == currentIndex());
        connect(a, &QAction::triggered, this, [this, i]{ setCurrentIndex(i); });
    }
    menu.exec(m_tabListBtn->mapToGlobal(QPoint(m_tabListBtn->width() - menu.sizeHint().width(), m_tabListBtn->height())));
}

void PacitumTabWidget::showTabContextMenu(int index, const QPoint& pos) {
    QMenu menu(this);
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
    auto* pin = menu.addAction(activeBar()->isTabPinned(index) ? QStringLiteral("Unpin Tab") : QStringLiteral("Pin Tab"));
    connect(pin, &QAction::triggered, this, [this, index]{ emit tabPinRequested(index, !activeBar()->isTabPinned(index)); });
    auto* mute = menu.addAction(QStringLiteral("Mute Tab"));
    connect(mute, &QAction::triggered, this, [this, index]{ emit tabMuteRequested(index, true); });
    menu.exec(pos);
}

void PacitumTabWidget::showBarContextMenu(const QPoint& pos) {
    QMenu menu(this);
    auto* nt = menu.addAction(QStringLiteral("New Tab"));
    connect(nt, &QAction::triggered, this, &PacitumTabWidget::newTabRequested);
    auto* re = menu.addAction(QStringLiteral("Reopen Closed Tab"));
    connect(re, &QAction::triggered, this, &PacitumTabWidget::tabReopenRequested);
    menu.exec(pos);
}

void PacitumTabWidget::closeOtherTabs(int index) {
    for (int i=count()-1;i>=0;--i) if (i!=index) emit tabCloseRequested(i);
}
void PacitumTabWidget::closeTabsToRight(int index) {
    for (int i=count()-1;i>index;--i) emit tabCloseRequested(i);
}
