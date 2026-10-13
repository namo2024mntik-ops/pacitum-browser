#include "BookmarksBar.h"
#include "BookmarkManager.h"
#include <QPushButton>
#include <QMenu>
#include <QScrollArea>
#include <QAction>

BookmarksBar::BookmarksBar(BookmarkManager* mgr, QWidget* parent) : QWidget(parent), m_mgr(mgr) {
    setFixedHeight(36);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8,2,8,2); layout->setSpacing(4);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_inner = new QWidget(scroll);
    m_innerLayout = new QHBoxLayout(m_inner);
    m_innerLayout->setContentsMargins(0,0,0,0); m_innerLayout->setSpacing(4);
    m_innerLayout->addStretch();
    m_inner->setLayout(m_innerLayout);
    scroll->setWidget(m_inner);
    layout->addWidget(scroll);
    refresh();
}

void BookmarksBar::refresh() {
    // remove all except stretch
    while (m_innerLayout->count() > 1) {
        auto* item = m_innerLayout->takeAt(0);
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
    for (auto &bm : m_mgr->allBookmarks()) {
        if (m_innerLayout->count() > 31) break;
        auto* btn = new QPushButton(bm.title.isEmpty() ? bm.url : bm.title, m_inner);
        btn->setToolTip(bm.url); btn->setFlat(true);
        QString url = bm.url;
        connect(btn, &QPushButton::clicked, this, [this, url]{ emit urlActivated(url); });
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(btn, &QWidget::customContextMenuRequested, this, [this, url, btn](const QPoint& p){ showContextMenu(p, url, btn); });
        m_innerLayout->insertWidget(m_innerLayout->count()-1, btn);
    }
}

void BookmarksBar::showContextMenu(const QPoint& pos, const QString& url, QWidget* btn) {
    QMenu m(this);
    m.addAction(QStringLiteral("Open"), this, [this, url]{ emit urlActivated(url); });
    m.addAction(QStringLiteral("Remove"), this, [this, url]{ m_mgr->removeBookmark(url); refresh(); });
    m.exec(btn->mapToGlobal(pos));
}
