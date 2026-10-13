#include "TabWidget.h"
#include "ThemeManager.h"
#include <QMenu>
#include <QAction>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QShowEvent>
#include <QVariantAnimation>
#include <QResizeEvent>
#include "Widgets.h"

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
quint64 PacitumTabBar::idOf(int index) const {
    return tabData(index).isValid() ? tabData(index).toULongLong() : 0;
}
int PacitumTabBar::indexOfId(quint64 id) const {
    for (int i = 0; i < count(); ++i) if (idOf(i) == id) return i;
    return -1;
}
void PacitumTabBar::relayout() { setIconSize(iconSize()); updateGeometry(); }   // forces QTabBar to re-layout tabs

void PacitumTabBar::setAvailableWidth(int w) { if (w != m_avail) { m_avail = w; relayout(); } }

QSize PacitumTabBar::tabSizeHint(int index) const {
    QSize s = QTabBar::tabSizeHint(index);
    if (m_vertical || shape() != QTabBar::RoundedNorth) return s;
    int w = 220;
    if (m_pinned.contains(index)) w = 40;
    else if (m_avail > 0 && count() > 0) w = qBound(72, m_avail / count(), 220);
    const qreal prog = m_progress.value(idOf(index), 1.0);
    return QSize(qMax(1, int(w * prog)), 32);
}
QSize PacitumTabBar::minimumTabSizeHint(int index) const { return tabSizeHint(index); }

void PacitumTabBar::animateIn(int index) {
    if (index < 0 || index >= count()) return;
    const quint64 id = m_nextId++;
    setTabData(index, id);
    if (!animationsEnabled) return;
    m_progress[id] = 0.0;
    auto* a = new QVariantAnimation(this);
    a->setDuration(150); a->setStartValue(0.0); a->setEndValue(1.0); a->setEasingCurve(QEasingCurve::OutCubic);
    connect(a, &QVariantAnimation::valueChanged, this, [this, id](const QVariant& v){ m_progress[id] = v.toReal(); relayout(); });
    connect(a, &QVariantAnimation::finished, this, [this, id]{ m_progress.remove(id); relayout(); });
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

void PacitumTabBar::animateOut(int index, std::function<void()> done) {
    if (!animationsEnabled || index < 0 || index >= count()) { done(); return; }
    quint64 id = idOf(index);
    if (!id) { id = m_nextId++; setTabData(index, id); }
    auto* a = new QVariantAnimation(this);
    a->setDuration(150); a->setStartValue(m_progress.value(id, 1.0)); a->setEndValue(0.0); a->setEasingCurve(QEasingCurve::InCubic);
    connect(a, &QVariantAnimation::valueChanged, this, [this, id](const QVariant& v){ m_progress[id] = v.toReal(); relayout(); });
    connect(a, &QVariantAnimation::finished, this, [this, id, done]{ m_progress.remove(id); done(); });
    a->start(QAbstractAnimation::DeleteWhenStopped);
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
    m_stripLayout = lay;
    lay->setContentsMargins(10,0,0,0); lay->setSpacing(0);

    m_logo = new QLabel(m_strip);
    m_logo->setFixedSize(30, 40); m_logo->setAlignment(Qt::AlignCenter);
    m_logo->setPixmap(QIcon(QStringLiteral(":/icons/pacitum-icon-64.png")).pixmap(QSize(18,18), m_strip->devicePixelRatioF()));
    m_logo->setToolTip(QStringLiteral("Pacitum Browser"));
    lay->addWidget(m_logo);
    lay->addSpacing(4);

    m_ext = new PacitumTabBar(false, m_strip);
    m_ext->setMovable(true);
    m_ext->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_ext->setFixedHeight(40);
    lay->addWidget(m_ext, 0, Qt::AlignBottom);

    m_stripNewTab = new AnimatedToolButton(m_strip);
    m_stripNewTab->setRadius(7);
    m_stripNewTab->setFixedSize(28,28); m_stripNewTab->setIconSize(QSize(18,18));
    m_stripNewTab->setToolTip(QStringLiteral("New tab (Ctrl+T)"));
    connect(m_stripNewTab, &QToolButton::clicked, this, &PacitumTabWidget::newTabRequested);
    lay->addSpacing(6);
    lay->addWidget(m_stripNewTab, 0, Qt::AlignVCenter);
    lay->addStretch(1);   // empty strip = window drag area

    m_tabListBtn = new AnimatedToolButton(m_strip);
    m_tabListBtn->setRadius(0);
    m_tabListBtn->setFixedSize(46,40); m_tabListBtn->setIconSize(QSize(16,16));
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

    m_strip->installEventFilter(this);
    updateCorner();
    updateBarVisibility();
    return m_strip;
}

void PacitumTabWidget::updateTabWidths() {
    if (!m_strip || !m_ext || !m_stripLayout) return;
    int used = m_stripLayout->contentsMargins().left() + m_stripLayout->contentsMargins().right();
    for (int i = 0; i < m_stripLayout->count(); ++i) {
        auto* it = m_stripLayout->itemAt(i);
        if (it->widget() == m_ext) continue;
        if (it->widget() && !it->widget()->isVisible()) continue;
        if (it->spacerItem() && it->spacerItem()->expandingDirections()) continue;
        used += it->widget() ? it->widget()->width() : it->sizeHint().width();
    }
    m_ext->setAvailableWidth(qMax(100, m_strip->width() - used - 48));   // keep >= 48 px of drag area
}

bool PacitumTabWidget::eventFilter(QObject* o, QEvent* e) {
    if (o == m_strip && (e->type() == QEvent::Resize || e->type() == QEvent::LayoutRequest)) updateTabWidths();
    return QTabWidget::eventFilter(o, e);
}

void PacitumTabWidget::animateRemove(int index, std::function<void()> done) {
    if (!m_ext || m_vertical || !m_ext->isVisible()) { done(); return; }
    m_ext->animateOut(index, std::move(done));
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
        m_ext->animateIn(index);
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
