#include "BookmarksBar.h"
#include "BookmarkManager.h"
#include "ThemeManager.h"
#include "Motion.h"
#include <QSet>
#include <QPropertyAnimation>
#include <QFontMetrics>
#include <QPushButton>
#include <QMenu>
#include <QScrollArea>
#include <QAction>

BookmarksBar::BookmarksBar(BookmarkManager* mgr, QWidget* parent) : QWidget(parent), m_mgr(mgr) {
    setFixedHeight(33);
    setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10,0,10,1); layout->setSpacing(2);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_inner = new QWidget(scroll);
    m_innerLayout = new QHBoxLayout(m_inner);
    m_innerLayout->setContentsMargins(0,0,0,0); m_innerLayout->setSpacing(2);
    m_innerLayout->addStretch();
    m_inner->setLayout(m_innerLayout);
    scroll->setWidget(m_inner);
    scroll->setFrameShape(QFrame::NoFrame);
    layout->addWidget(scroll, 1);
    m_other = new QPushButton(QStringLiteral("Other bookmarks"), this);
    m_other->setFixedHeight(26); m_other->setIconSize(QSize(14,14));
    connect(m_other, &QPushButton::clicked, this, [this]{
        QMenu m(this);
        for (auto& bm : m_mgr->allBookmarks()) { QString u = bm.url; m.addAction(bm.title.isEmpty() ? bm.url : bm.title, this, [this, u]{ emit urlActivated(u); }); }
        if (m.isEmpty()) m.addAction(QStringLiteral("No bookmarks yet"))->setEnabled(false);
        m.exec(m_other->mapToGlobal(QPoint(0, m_other->height())));
    });
    layout->addWidget(m_other);
    refresh();
}

void BookmarksBar::refresh() {
    QSet<QString> before = m_shownUrls; m_shownUrls.clear();
    // remove all except stretch
    while (m_innerLayout->count() > 1) {
        auto* item = m_innerLayout->takeAt(0);
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
    for (auto &bm : m_mgr->allBookmarks()) {
        if (m_innerLayout->count() > 31) break;
        const QString label = bm.title.isEmpty() ? bm.url : bm.title;
        auto* btn = new QPushButton(m_inner);
        btn->setText(QFontMetrics(btn->font()).elidedText(label, Qt::ElideRight, 130));
        btn->setFixedHeight(26); btn->setIconSize(QSize(14,14));
        if (m_theme) btn->setIcon(m_theme->secondaryIcon(QStringLiteral("globe"), 14));
        btn->setToolTip(bm.url); btn->setFlat(true);
        QString url = bm.url;
        connect(btn, &QPushButton::clicked, this, [this, url]{ emit urlActivated(url); });
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(btn, &QWidget::customContextMenuRequested, this, [this, url, btn](const QPoint& p){ showContextMenu(p, url, btn); });
        m_innerLayout->insertWidget(m_innerLayout->count()-1, btn);
        if (m_primed && !before.contains(url) && isVisible()) {   // kit #18: new chip grows in (maxWidth 0→hint, OutBack) + fades
            Motion::fadeIn(btn, Motion::Medium);
            if (Motion::enabled()) {
                const int w = btn->sizeHint().width();
                btn->setMaximumWidth(0);
                auto* a = new QPropertyAnimation(btn, "maximumWidth", btn);
                a->setDuration(Motion::Medium); a->setStartValue(0); a->setEndValue(w);
                QEasingCurve back(QEasingCurve::OutBack); back.setOvershoot(1.2); a->setEasingCurve(back);
                connect(a, &QPropertyAnimation::finished, btn, [btn]{ btn->setMaximumWidth(QWIDGETSIZE_MAX); });
                a->start(QAbstractAnimation::DeleteWhenStopped);
            }
        }
        m_shownUrls.insert(url);
    }
    m_primed = true;
}

void BookmarksBar::showContextMenu(const QPoint& pos, const QString& url, QWidget* btn) {
    QMenu m(this);
    m.addAction(QStringLiteral("Open"), this, [this, url]{ emit urlActivated(url); });
    m.addAction(QStringLiteral("Remove"), this, [this, url]{ m_mgr->removeBookmark(url); refresh(); });
    m.exec(btn->mapToGlobal(pos));
}

void BookmarksBar::applyTheme(ThemeManager* tm) {
    m_theme = tm;
    if (m_other) m_other->setIcon(tm->secondaryIcon(QStringLiteral("folder"), 14));
    refresh();
}
