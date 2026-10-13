#include "BrowserWindow.h"
#include "BrowserTab.h"
#include "TabWidget.h"
#include "AddressBar.h"
#include "BookmarksBar.h"
#include "BookmarkManager.h"
#include "HistoryManager.h"
#include "DownloadManager.h"
#include "ThemeManager.h"
#include "SessionManager.h"
#include "ZoomController.h"
#include "AdBlocker.h"
#include "SettingsDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QKeySequence>
#include <QShortcut>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineCookieStore>
#include <QStatusBar>
#include <QKeyEvent>
#include <QStyle>
#include <QTimer>
#include <QUrl>

BrowserWindow::BrowserWindow(QWidget* parent) : QMainWindow(parent) {
    m_history = new HistoryManager(QStringLiteral("browser_history.db"));
    m_bookmarks = new BookmarkManager(QStringLiteral("bookmarks.json"));
    m_downloads = new DownloadManager(this);
    m_theme = new ThemeManager(false);
    m_session = new SessionManager(QStringLiteral("browser_session.json"));
    m_zoom = new ZoomController(1.0, this);

    setWindowIcon(QIcon(QStringLiteral(":/icons/safari.png")));
    setupAdBlocker();
    setupDownloadHandler();
    setupUI();
    loadSettings();
    restoreOrNewTab();
}

BrowserWindow::~BrowserWindow() {
    delete m_history; delete m_bookmarks; delete m_theme; delete m_session; delete m_adblock;
}

void BrowserWindow::setupAdBlocker() {
    m_adblock = new AdBlocker(true, this);
    QWebEngineProfile::defaultProfile()->setUrlRequestInterceptor(m_adblock);
}

void BrowserWindow::setupDownloadHandler() {
    connect(QWebEngineProfile::defaultProfile(), &QWebEngineProfile::downloadRequested, this, &BrowserWindow::onDownloadRequested);
}

void BrowserWindow::setupUI() {
    setWindowTitle(QStringLiteral("Safari Browser"));
    setMinimumSize(1000,700); resize(1280,850);

    auto* central = new QWidget(this); setCentralWidget(central);
    auto* mainLay = new QVBoxLayout(central); mainLay->setContentsMargins(0,0,0,0); mainLay->setSpacing(0);

    createToolbar();
    mainLay->addWidget(m_toolbar);

    m_bookmarksBar = new BookmarksBar(m_bookmarks, this);
    connect(m_bookmarksBar, &BookmarksBar::urlActivated, this, &BrowserWindow::navigateToUrl);
    m_bookmarksBar->hide();
    mainLay->addWidget(m_bookmarksBar);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(1);
    mainLay->addWidget(m_splitter, 1);

    m_tabs = new SafariTabWidget(m_verticalTabs, this);
    connect(m_tabs, &SafariTabWidget::tabCloseRequested, this, &BrowserWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, &BrowserWindow::onTabChanged);
    connect(m_tabs, &SafariTabWidget::newTabRequested, this, [this]{ newTab(); });
    connect(m_tabs, &SafariTabWidget::tabSplitRequested, this, &BrowserWindow::toggleSplitView);
    connect(m_tabs, &SafariTabWidget::tabDuplicateRequested, this, &BrowserWindow::duplicateTab);
    connect(m_tabs, &SafariTabWidget::tabReopenRequested, this, &BrowserWindow::reopenClosedTab);
    connect(m_tabs, &SafariTabWidget::tabMuteRequested, this, &BrowserWindow::muteTab);
    connect(m_tabs, &SafariTabWidget::tabPinRequested, this, &BrowserWindow::pinTab);
    m_splitter->addWidget(m_tabs);
    m_activeWidget = m_tabs;

    m_splitTabs = new SafariTabWidget(m_verticalTabs, this);
    m_splitTabs->hide();
    connect(m_splitTabs, &SafariTabWidget::tabCloseRequested, this, &BrowserWindow::closeSplitTab);
    connect(m_splitTabs, &QTabWidget::currentChanged, this, &BrowserWindow::onTabChanged);
    connect(m_splitTabs, &SafariTabWidget::newTabRequested, this, [this]{ newTab(QString{}); /* in split */ });
    connect(m_splitTabs, &SafariTabWidget::tabSplitRequested, this, &BrowserWindow::toggleSplitView);
    connect(m_splitTabs, &SafariTabWidget::tabDuplicateRequested, this, &BrowserWindow::duplicateTab);
    m_splitter->addWidget(m_splitTabs);

    setupShortcuts();
    createMenuBar();
    createStatusBar();
    applyTheme();
}

void BrowserWindow::createToolbar() {
    m_toolbar = new QWidget(this); m_toolbar->setFixedHeight(64);
    auto* lay = new QHBoxLayout(m_toolbar); lay->setContentsMargins(16,8,16,8); lay->setSpacing(6);

    m_backBtn = new QPushButton(this); m_backBtn->setFixedSize(38,38); m_backBtn->setIcon(style()->standardIcon(QStyle::SP_ArrowBack)); m_backBtn->setIconSize(QSize(18,18)); m_backBtn->setToolTip(QStringLiteral("Back (Alt+Left)")); connect(m_backBtn, &QPushButton::clicked, this, &BrowserWindow::goBack);
    m_forwardBtn = new QPushButton(this); m_forwardBtn->setFixedSize(38,38); m_forwardBtn->setIcon(style()->standardIcon(QStyle::SP_ArrowForward)); m_forwardBtn->setIconSize(QSize(18,18)); m_forwardBtn->setToolTip(QStringLiteral("Forward (Alt+Right)")); connect(m_forwardBtn, &QPushButton::clicked, this, &BrowserWindow::goForward);
    m_reloadBtn = new QPushButton(this); m_reloadBtn->setFixedSize(38,38); m_reloadBtn->setIcon(style()->standardIcon(QStyle::SP_BrowserReload)); m_reloadBtn->setIconSize(QSize(18,18)); m_reloadBtn->setToolTip(QStringLiteral("Reload (F5)")); connect(m_reloadBtn, &QPushButton::clicked, this, &BrowserWindow::reloadPage);
    m_homeBtn = new QPushButton(this); m_homeBtn->setFixedSize(38,38); m_homeBtn->setIcon(style()->standardIcon(QStyle::SP_DirHomeIcon)); m_homeBtn->setIconSize(QSize(18,18)); m_homeBtn->setToolTip(QStringLiteral("Home")); connect(m_homeBtn, &QPushButton::clicked, this, &BrowserWindow::goHome);
    m_addressBar = new AddressBar(m_history, this); connect(m_addressBar, &AddressBar::returnPressed, this, &BrowserWindow::navigateToUrl);
    m_splitBtn = new QPushButton(this); m_splitBtn->setFixedSize(38,38); m_splitBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarShadeButton)); m_splitBtn->setToolTip(QStringLiteral("Split View")); connect(m_splitBtn, &QPushButton::clicked, this, [this]{ toggleSplitView(); });
    m_bookmarkBtn = new QPushButton(QStringLiteral("☆"), this); m_bookmarkBtn->setFixedSize(38,38); m_bookmarkBtn->setToolTip(QStringLiteral("Bookmark (Ctrl+B)")); connect(m_bookmarkBtn, &QPushButton::clicked, this, &BrowserWindow::toggleBookmark);
    m_downloadsBtn = new QPushButton(this); m_downloadsBtn->setFixedSize(38,38); m_downloadsBtn->setIcon(style()->standardIcon(QStyle::SP_ArrowDown)); m_downloadsBtn->setToolTip(QStringLiteral("Downloads (Ctrl+J)")); connect(m_downloadsBtn, &QPushButton::clicked, this, &BrowserWindow::showDownloads);
    m_menuBtn = new QPushButton(this); m_menuBtn->setFixedSize(38,38); m_menuBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarMenuButton)); m_menuBtn->setToolTip(QStringLiteral("Menu")); connect(m_menuBtn, &QPushButton::clicked, this, &BrowserWindow::showMenu);

    lay->addWidget(m_backBtn); lay->addWidget(m_forwardBtn); lay->addWidget(m_reloadBtn); lay->addWidget(m_homeBtn);
    lay->addWidget(m_addressBar, 1);
    lay->addWidget(m_splitBtn); lay->addWidget(m_bookmarkBtn); lay->addWidget(m_downloadsBtn); lay->addWidget(m_menuBtn);

    m_toolbarOpacity = new QGraphicsOpacityEffect(m_toolbar); m_toolbar->setGraphicsEffect(m_toolbarOpacity); m_toolbarOpacity->setOpacity(0.0);
    m_toolbarAnim = new QPropertyAnimation(m_toolbarOpacity, "opacity", this);
    m_toolbarAnim->setDuration(380); m_toolbarAnim->setStartValue(0.0); m_toolbarAnim->setEndValue(1.0); m_toolbarAnim->setEasingCurve(QEasingCurve::OutCubic);
    QTimer::singleShot(0, m_toolbarAnim, [this]{ m_toolbarAnim->start(); });
}

void BrowserWindow::setupShortcuts() {
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Tab")), this, SLOT(nextTab()));
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Tab")), this, SLOT(prevTab()));
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+L")), this, SLOT(focusAddressBar()));
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+F")), this, SLOT(showFind()));
    new QShortcut(QKeySequence(QStringLiteral("F5")), this, SLOT(reloadPage()));
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+H")), this, SLOT(showHistory()));
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+J")), this, SLOT(showDownloads()));
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+B")), this, SLOT(toggleBookmark()));
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")), this, SLOT(toggleBookmarksBar()));
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")), this, SLOT(toggleVerticalTabs()));
    new QShortcut(QKeySequence(Qt::Key_Escape), this, SLOT(handleEscape()));
}

void BrowserWindow::createMenuBar() {
    auto* mb = menuBar();
    auto* file = mb->addMenu(QStringLiteral("File"));
    file->addAction(QStringLiteral("New Tab"), QKeySequence::AddTab, this, [this]{ newTab(); });
    file->addAction(QStringLiteral("New Private Tab"), QKeySequence(QStringLiteral("Ctrl+Shift+N")), this, &BrowserWindow::newPrivateTab);
    file->addAction(QStringLiteral("Reopen Closed Tab"), QKeySequence(QStringLiteral("Ctrl+Shift+T")), this, &BrowserWindow::reopenClosedTab);
    file->addSeparator();
    file->addAction(QStringLiteral("Close Tab"), QKeySequence::Close, this, [this]{ closeTab(m_tabs->currentIndex()); });
    file->addSeparator();
    file->addAction(QStringLiteral("Save Session As..."), this, &BrowserWindow::saveSessionDialog);
    file->addAction(QStringLiteral("Quit"), QKeySequence::Quit, this, &QWidget::close);

    auto* edit = mb->addMenu(QStringLiteral("Edit"));
    edit->addAction(QStringLiteral("Find in Page"), QKeySequence::Find, this, &BrowserWindow::showFind);
    edit->addAction(QStringLiteral("Zoom In"), QKeySequence(QStringLiteral("Ctrl+=")), this, &BrowserWindow::zoomIn);
    edit->addAction(QStringLiteral("Zoom Out"), QKeySequence(QStringLiteral("Ctrl+-")), this, &BrowserWindow::zoomOut);
    edit->addAction(QStringLiteral("Zoom Reset"), QKeySequence(QStringLiteral("Ctrl+0")), this, &BrowserWindow::zoomReset);

    auto* view = mb->addMenu(QStringLiteral("View"));
    view->addAction(QStringLiteral("Toggle Dark Mode"), QKeySequence(QStringLiteral("Ctrl+D")), this, &BrowserWindow::toggleDarkMode);
    view->addAction(QStringLiteral("Toggle Bookmarks Bar"), QKeySequence(QStringLiteral("Ctrl+Shift+O")), this, &BrowserWindow::toggleBookmarksBar);
    view->addAction(QStringLiteral("Toggle Vertical Tabs"), QKeySequence(QStringLiteral("Ctrl+Shift+E")), this, &BrowserWindow::toggleVerticalTabs);
    view->addSeparator();
    view->addAction(QStringLiteral("Full Screen"), QKeySequence(QStringLiteral("F11")), this, &BrowserWindow::toggleFullscreen);

    auto* hist = mb->addMenu(QStringLiteral("History"));
    hist->addAction(QStringLiteral("Show History"), QKeySequence(QStringLiteral("Ctrl+H")), this, &BrowserWindow::showHistory);
    hist->addAction(QStringLiteral("Clear History"), this, &BrowserWindow::clearHistory);

    auto* bm = mb->addMenu(QStringLiteral("Bookmarks"));
    bm->addAction(QStringLiteral("Add Bookmark"), QKeySequence(QStringLiteral("Ctrl+B")), this, &BrowserWindow::toggleBookmark);
    bm->addAction(QStringLiteral("Show All Bookmarks"), this, &BrowserWindow::showBookmarksDialog);

    auto* win = mb->addMenu(QStringLiteral("Window"));
    win->addAction(QStringLiteral("Downloads"), QKeySequence(QStringLiteral("Ctrl+J")), this, &BrowserWindow::showDownloads);
    win->addAction(QStringLiteral("Split View"), this, [this]{ toggleSplitView(); });

    auto* set = mb->addMenu(QStringLiteral("Settings"));
    set->addAction(QStringLiteral("Preferences"), QKeySequence(QStringLiteral("Ctrl+," )), this, &BrowserWindow::showSettings);
}

void BrowserWindow::createStatusBar() {
    statusBar()->showMessage(QStringLiteral("Ready"));
    m_zoomIndicator = new QLabel(QStringLiteral("100%"), this);
    statusBar()->addPermanentWidget(m_zoomIndicator);
}

// ---- Tab management ----
void BrowserWindow::newTab(const QString& url) {
    QString u = url.isEmpty() ? readSettings().value(QStringLiteral("homepage")).toString(QStringLiteral("https://www.google.com")) : url;
    auto* tab = new BrowserTab(u, m_privateMode, m_searchEngine, this);
    tab->setZoomFactor(m_zoom->zoom());
    connect(tab, &BrowserTab::urlChanged, this, &BrowserWindow::updateAddressBar);
    connect(tab, &BrowserTab::titleChanged, this, &BrowserWindow::updateTabTitle);
    connect(tab, &BrowserTab::loadingChanged, this, &BrowserWindow::onTabLoading);
    // choose target
    SafariTabWidget* target = m_splitActive && m_splitTabs->isVisible() && m_splitTabs->count()==0 ? m_splitTabs : m_tabs;
    // if split active and caller wants split, use split; otherwise default to main
    // For regular newTab, use main tabs
    target = m_tabs;
    int idx = target->addTab(tab, QStringLiteral("New Tab"));
    target->setCurrentIndex(idx);
}

void BrowserWindow::newPrivateTab() {
    m_privateMode = true; newTab(); m_privateMode = false;
    int idx = m_tabs->currentIndex();
    m_tabs->setTabText(idx, QStringLiteral("🔒 ") + m_tabs->tabText(idx));
}

void BrowserWindow::closeTab(int index) {
    if (index < 0 || index >= m_tabs->count()) return;
    if (auto* w = qobject_cast<BrowserTab*>(m_tabs->widget(index))) {
        m_recentlyClosed.push_back({w->currentUrl(), w->currentTitle()});
        if (m_recentlyClosed.size() > 20) m_recentlyClosed.pop_front();
    }
    if (m_tabs->count() > 1) m_tabs->removeTab(index);
    else {
        if (!m_splitActive) close();
        else { m_tabs->removeTab(index); toggleSplitView(); }
    }
}

void BrowserWindow::closeSplitTab(int index) {
    if (index < 0) return;
    if (m_splitTabs->count() > 1) m_splitTabs->removeTab(index);
    else toggleSplitView();
}

void BrowserWindow::onTabChanged(int index) {
    auto* sender = qobject_cast<SafariTabWidget*>(QObject::sender());
    if (!sender || index < 0) return;
    m_activeWidget = sender;
    if (auto* tab = qobject_cast<BrowserTab*>(sender->widget(index))) {
        updateAddressBar(tab->currentUrl());
        m_backBtn->setEnabled(tab->canGoBack());
        m_forwardBtn->setEnabled(tab->canGoForward());
    }
}

BrowserTab* BrowserWindow::activeTab() const {
    if (m_activeWidget && m_activeWidget->currentWidget()) return qobject_cast<BrowserTab*>(m_activeWidget->currentWidget());
    return qobject_cast<BrowserTab*>(m_tabs->currentWidget());
}

void BrowserWindow::navigateToUrl(const QString& url) {
    if (auto* t = activeTab()) { t->loadUrl(url); m_history->addVisit(url, {}, true); }
}

void BrowserWindow::updateAddressBar(const QString& url) {
    m_addressBar->setUrl(url);
    if (!url.isEmpty() && !url.startsWith(QStringLiteral("about:"))) {
        auto* t = activeTab(); m_history->addVisit(url, t ? t->currentTitle() : QString{});
    }
    // bookmark indicator
    m_bookmarkBtn->setText(m_bookmarks->isBookmarked(url) ? QStringLiteral("★") : QStringLiteral("☆"));
}

void BrowserWindow::updateTabTitle(const QString& title) {
    auto* sender = qobject_cast<BrowserTab*>(QObject::sender());
    if (!sender) return;
    for (auto* tw : {m_tabs, m_splitTabs}) {
        int idx = tw->indexOf(sender);
        if (idx >= 0) { tw->setTabText(idx, title.left(30) + (title.size()>30 ? QStringLiteral("...") : QString{})); break; }
    }
}

void BrowserWindow::onTabLoading(bool loading) {
    auto* tab = qobject_cast<BrowserTab*>(sender());
    if (!tab) return;
    m_backBtn->setEnabled(tab->canGoBack());
    m_forwardBtn->setEnabled(tab->canGoForward());
    m_reloadBtn->setIcon(style()->standardIcon(loading ? QStyle::SP_BrowserStop : QStyle::SP_BrowserReload));
    // reconnect reload btn
    m_reloadBtn->disconnect();
    if (loading) connect(m_reloadBtn, &QPushButton::clicked, tab, &BrowserTab::stop);
    else connect(m_reloadBtn, &QPushButton::clicked, this, &BrowserWindow::reloadPage);
}

void BrowserWindow::goBack() { if (auto* t=activeTab()) { t->goBack(); QTimer::singleShot(200, this, [this]{ if(auto* tt=activeTab()){ m_backBtn->setEnabled(tt->canGoBack()); }}); } }
void BrowserWindow::goForward() { if (auto* t=activeTab()) { t->goForward(); QTimer::singleShot(200, this, [this]{ if(auto* tt=activeTab()){ m_forwardBtn->setEnabled(tt->canGoForward()); }}); } }
void BrowserWindow::reloadPage() { if (auto* t=activeTab()) t->reload(); }
void BrowserWindow::goHome() { navigateToUrl(readSettings().value(QStringLiteral("homepage")).toString(QStringLiteral("https://www.google.com"))); }

void BrowserWindow::toggleBookmark() {
    auto* t = activeTab(); if (!t) return;
    QString url = t->currentUrl(), title = t->currentTitle();
    if (url.isEmpty()) return;
    if (m_bookmarks->isBookmarked(url)) { m_bookmarks->removeBookmark(url); m_bookmarkBtn->setText(QStringLiteral("☆")); statusBar()->showMessage(QStringLiteral("Bookmark removed"), 2000); }
    else { m_bookmarks->addBookmark(title, url); m_bookmarkBtn->setText(QStringLiteral("★")); statusBar()->showMessage(QStringLiteral("Bookmark added"), 2000); }
    m_bookmarksBar->refresh();
}

void BrowserWindow::showDownloads() { m_downloads->show(); }
void BrowserWindow::showSettings() { SettingsDialog dlg(this); if (dlg.exec()) { loadSettings(); applyTheme(); } }
void BrowserWindow::showMenu() {
    QMenu m(this);
    m.addAction(QStringLiteral("New Tab (Ctrl+T)"), this, [this]{ newTab(); });
    m.addAction(QStringLiteral("New Private Tab (Ctrl+Shift+N)"), this, &BrowserWindow::newPrivateTab);
    m.addSeparator();
    m.addAction(QStringLiteral("Copy Page Address"), this, &BrowserWindow::copyCurrentUrl);
    m.addAction(QStringLiteral("Page Information"), this, &BrowserWindow::showPageInfo);
    m.addSeparator();
    m.addAction(QStringLiteral("Find in Page (Ctrl+F)"), this, &BrowserWindow::showFind);
    m.addAction(QStringLiteral("Downloads (Ctrl+J)"), this, &BrowserWindow::showDownloads);
    m.addSeparator();
    m.addAction(QStringLiteral("Toggle Dark Mode (Ctrl+D)"), this, &BrowserWindow::toggleDarkMode);
    m.addAction(QStringLiteral("Toggle Bookmarks Bar"), this, &BrowserWindow::toggleBookmarksBar);
    m.addAction(QStringLiteral("Toggle Vertical Tabs"), this, &BrowserWindow::toggleVerticalTabs);
    m.addSeparator();
    m.addAction(QStringLiteral("Preferences (Ctrl+,)"), this, &BrowserWindow::showSettings);
    m.exec(m_menuBtn->mapToGlobal(m_menuBtn->rect().bottomLeft()));
}

void BrowserWindow::copyCurrentUrl() { if (auto* t=activeTab()) { QApplication::clipboard()->setText(t->currentUrl()); statusBar()->showMessage(QStringLiteral("Page address copied"), 1800);} }
void BrowserWindow::showPageInfo() {
    if (auto* t=activeTab()) {
        QString url=t->currentUrl();
        QString sec = url.startsWith(QStringLiteral("https://")) ? QStringLiteral("Secure HTTPS connection") : QStringLiteral("Unencrypted or local page");
        QMessageBox::information(this, QStringLiteral("Page Information"), sec + QStringLiteral("\n\n") + url);
    }
}
void BrowserWindow::showHistory() {
    auto r = m_history->recent(50);
    if (r.isEmpty()) { QMessageBox::information(this, QStringLiteral("History"), QStringLiteral("No browsing history yet.")); return; }
    QStringList lines; for (int i=0;i<qMin(20,(int)r.size());++i) lines << (r[i].title.isEmpty()?r[i].url:r[i].title);
    QMessageBox::information(this, QStringLiteral("History (%1 entries)").arg(r.size()), lines.join(QStringLiteral("\n")));
}
void BrowserWindow::clearHistory() { m_history->clear(); statusBar()->showMessage(QStringLiteral("History cleared"), 2000); }
void BrowserWindow::showBookmarksDialog() {
    auto bms = m_bookmarks->allBookmarks();
    if (bms.isEmpty()) { QMessageBox::information(this, QStringLiteral("Bookmarks"), QStringLiteral("No bookmarks yet.")); return; }
    QStringList lines; for (auto &b: bms) lines << b.title + QStringLiteral(" — ") + b.url;
    QMessageBox::information(this, QStringLiteral("Bookmarks (%1)").arg(bms.size()), lines.join(QStringLiteral("\n")));
}
void BrowserWindow::toggleBookmarksBar() { m_bookmarksBar->setVisible(!m_bookmarksBar->isVisible()); }
void BrowserWindow::toggleVerticalTabs() { m_verticalTabs = !m_verticalTabs; m_tabs->setVertical(m_verticalTabs); m_splitTabs->setVertical(m_verticalTabs); }
void BrowserWindow::toggleFullscreen() { isFullScreen() ? showNormal() : showFullScreen(); }
void BrowserWindow::focusAddressBar() { m_addressBar->focusUrl(); }
void BrowserWindow::zoomIn() { if(auto* t=activeTab()){ m_zoom->zoomIn(); t->setZoomFactor(m_zoom->zoom()); m_zoomIndicator->setText(QString::number(m_zoom->zoomPercent())+QStringLiteral("%")); statusBar()->showMessage(QStringLiteral("Zoom: %1%").arg(m_zoom->zoomPercent()), 1000);} }
void BrowserWindow::zoomOut(){ if(auto* t=activeTab()){ m_zoom->zoomOut(); t->setZoomFactor(m_zoom->zoom()); m_zoomIndicator->setText(QString::number(m_zoom->zoomPercent())+QStringLiteral("%")); statusBar()->showMessage(QStringLiteral("Zoom: %1%").arg(m_zoom->zoomPercent()), 1000);} }
void BrowserWindow::zoomReset(){ if(auto* t=activeTab()){ m_zoom->zoomReset(); t->setZoomFactor(m_zoom->zoom()); m_zoomIndicator->setText(QStringLiteral("100%")); statusBar()->showMessage(QStringLiteral("Zoom: 100%"), 1000);} }
void BrowserWindow::showFind(){ if(auto* t=activeTab()) t->showFindBar(); }
void BrowserWindow::handleEscape(){ if(auto* t=activeTab()) t->hideFindBar(); }
void BrowserWindow::nextTab(){ m_tabs->setCurrentIndex((m_tabs->currentIndex()+1)%m_tabs->count()); }
void BrowserWindow::prevTab(){ m_tabs->setCurrentIndex((m_tabs->currentIndex()-1+m_tabs->count())%m_tabs->count()); }
void BrowserWindow::lastTab(){ m_tabs->setCurrentIndex(m_tabs->count()-1); }
void BrowserWindow::duplicateTab(int index){ if(auto* t=qobject_cast<BrowserTab*>(m_tabs->widget(index))) newTab(t->currentUrl()); }
void BrowserWindow::muteTab(int idx,bool m){ if(auto* t=qobject_cast<BrowserTab*>(m_tabs->widget(idx))){ t->setMuted(m); QString title=m_tabs->tabText(idx); if(m && !title.contains(QStringLiteral("🔇"))) m_tabs->setTabText(idx, title+QStringLiteral(" 🔇")); else if(!m) m_tabs->setTabText(idx, title.replace(QStringLiteral(" 🔇"), QString{})); } }
void BrowserWindow::pinTab(int idx,bool p){ m_tabs->setTabPinned(idx,p); }
void BrowserWindow::reopenClosedTab(){ if(!m_recentlyClosed.isEmpty()){ auto last=m_recentlyClosed.takeLast(); newTab(last.url);} }
void BrowserWindow::saveSessionDialog(){
    QVector<SessionTab> tabs;
    for(int i=0;i<m_tabs->count();++i) if(auto* t=qobject_cast<BrowserTab*>(m_tabs->widget(i))) tabs.push_back({t->currentUrl(), t->currentTitle(), false});
    m_session->save(tabs); statusBar()->showMessage(QStringLiteral("Session saved"), 2000);
}
void BrowserWindow::onDownloadRequested(QWebEngineDownloadRequest* dl){ m_downloads->addDownload(dl); }

void BrowserWindow::toggleSplitView(int index) {
    if (!m_splitActive) {
        m_splitTabs->show(); m_splitActive=true;
        if (index>=0) { auto* tab=m_tabs->widget(index); QString title=m_tabs->tabText(index); m_tabs->removeTab(index); m_splitTabs->addTab(tab, title); }
        else newTab();
        // Actually newTab goes to m_tabs; move last tab to split if we created one
        if (index<0 && m_tabs->count()>0) { auto* tab=m_tabs->widget(m_tabs->count()-1); QString title=m_tabs->tabText(m_tabs->count()-1); m_tabs->removeTab(m_tabs->count()-1); m_splitTabs->addTab(tab, title); }
    } else {
        while(m_splitTabs->count()>0){ auto* tab=m_splitTabs->widget(0); QString title=m_splitTabs->tabText(0); m_splitTabs->removeTab(0); m_tabs->addTab(tab, title); }
        m_splitTabs->hide(); m_splitActive=false;
    }
}

void BrowserWindow::toggleDarkMode(){ m_darkMode=!m_darkMode; applyTheme(); saveSettings(); }

void BrowserWindow::applyTheme(){
    m_theme->setDarkMode(m_darkMode);
    setStyleSheet(m_theme->mainWindowStyle());
    m_tabs->applyTheme(m_theme);
    m_splitTabs->applyTheme(m_theme);
    m_addressBar->applyTheme(m_theme);
    applyToolbarTheme();
}

void BrowserWindow::applyToolbarTheme(){
    if(m_darkMode){
        m_toolbar->setStyleSheet(R"(QWidget{background:#17191f;border-bottom:1px solid #30343f;} QPushButton{background:transparent;border:none;border-radius:11px;font-size:15px;color:#e8edf5;} QPushButton:hover{background:#2a2f3a;} QPushButton:pressed{background:#3b4352;} QPushButton:disabled{color:#565d6b;})");
        m_bookmarksBar->setStyleSheet(R"(QWidget{background-color:#252525;border-bottom:1px solid #333;} QPushButton{background:transparent;border:none;padding:4px 10px;font-size:12px;color:#aaa;} QPushButton:hover{background:#333;color:#fff;border-radius:4px;})");
    } else {
        m_toolbar->setStyleSheet(R"(QWidget{background:#f7f8fb;border-bottom:1px solid #dfe3ea;} QPushButton{background:transparent;border:none;border-radius:11px;font-size:15px;color:#202938;} QPushButton:hover{background:#e8edf5;} QPushButton:pressed{background:#d9e1ee;} QPushButton:disabled{color:#b4bcc9;})");
        m_bookmarksBar->setStyleSheet(R"(QWidget{background-color:#fafafa;border-bottom:1px solid #e0e0e0;} QPushButton{background:transparent;border:none;padding:4px 10px;font-size:12px;color:#666;} QPushButton:hover{background:#e8e8ed;color:#1d1d1f;border-radius:4px;})");
    }
}

QJsonObject BrowserWindow::readSettings() const {
    QFile f(QStringLiteral("browser_settings.json")); if(!f.open(QIODevice::ReadOnly)) return {};
    auto doc=QJsonDocument::fromJson(f.readAll()); return doc.isObject()?doc.object():QJsonObject{};
}

void BrowserWindow::loadSettings(){
    auto s=readSettings();
    m_darkMode = s.value(QStringLiteral("dark_mode")).toBool(false) || s.value(QStringLiteral("theme")).toString()==QStringLiteral("Dark");
    m_searchEngine = s.value(QStringLiteral("search_engine")).toString(QStringLiteral("Google"));
    m_adblock->setEnabled(s.value(QStringLiteral("adblock_enabled")).toBool(true));
    m_bookmarksBar->setVisible(s.value(QStringLiteral("show_bookmarks_bar")).toBool(false));
    if(s.value(QStringLiteral("vertical_tabs")).toBool(false)){ m_verticalTabs=true; m_tabs->setVertical(true); m_splitTabs->setVertical(true); }
    int zoomPct=s.value(QStringLiteral("default_zoom")).toInt(100); m_zoom->setZoom(zoomPct/100.0);
}

void BrowserWindow::saveSettings(){
    auto s=readSettings();
    s[QStringLiteral("dark_mode")] = m_darkMode;
    s[QStringLiteral("theme")] = m_darkMode?QStringLiteral("Dark"):QStringLiteral("Light");
    QFile f(QStringLiteral("browser_settings.json")); if(f.open(QIODevice::WriteOnly|QIODevice::Truncate)) f.write(QJsonDocument(s).toJson(QJsonDocument::Indented));
}

void BrowserWindow::restoreOrNewTab(){
    auto s=readSettings(); bool restore=s.value(QStringLiteral("restore_session")).toBool(true);
    if(restore){
        auto tabs=m_session->restore();
        if(!tabs.isEmpty()){ for(auto &t:tabs) newTab(t.url); return; }
    }
    newTab();
}

void BrowserWindow::closeEvent(QCloseEvent* e){
    QVector<SessionTab> tabs;
    for(int i=0;i<m_tabs->count();++i) if(auto* t=qobject_cast<BrowserTab*>(m_tabs->widget(i))) tabs.push_back({t->currentUrl(), t->currentTitle(), false});
    if(!tabs.isEmpty()) m_session->save(tabs);
    auto s=readSettings();
    if(s.value(QStringLiteral("clear_on_exit")).toBool(false)){
        m_history->clear();
        QWebEngineProfile::defaultProfile()->clearHttpCache();
        QWebEngineProfile::defaultProfile()->cookieStore()->deleteAllCookies();
    }
    QMainWindow::closeEvent(e);
}

void BrowserWindow::keyPressEvent(QKeyEvent* e){
    if(e->key()==Qt::Key_Escape && isFullScreen()) showNormal();
    QMainWindow::keyPressEvent(e);
}
