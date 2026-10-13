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
#include "PacitumScheme.h"
#include <QWidgetAction>
#include <QStyleHints>
#include <QGuiApplication>

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
#include <QDir>
#include <QWebEngineView>

BrowserWindow::BrowserWindow(QWidget* parent) : QMainWindow(parent) {
    m_history = new HistoryManager(QStringLiteral("browser_history.db"));
    m_bookmarks = new BookmarkManager(QStringLiteral("bookmarks.json"));
    m_downloads = new DownloadManager(this);
    m_theme = new ThemeManager(false);
    m_session = new SessionManager(QStringLiteral("browser_session.json"));
    m_zoom = new ZoomController(1.0, this);

    setWindowIcon(QIcon(QStringLiteral(":/icons/pacitum-icon-256.png")));
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
    setWindowTitle(QStringLiteral("Pacitum Browser"));
    setMinimumSize(900,600); resize(1440,900);

    auto* central = new QWidget(this); setCentralWidget(central);
    auto* mainLay = new QVBoxLayout(central); mainLay->setContentsMargins(0,0,0,0); mainLay->setSpacing(0);

    createToolbar();

    m_bookmarksBar = new BookmarksBar(m_bookmarks, this);
    connect(m_bookmarksBar, &BookmarksBar::urlActivated, this, &BrowserWindow::navigateToUrl);
    mainLay->addWidget(m_bookmarksBar);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(1);
    mainLay->addWidget(m_splitter, 1);

    m_tabs = new PacitumTabWidget(m_verticalTabs, this);
    connect(m_tabs, &PacitumTabWidget::tabCloseRequested, this, &BrowserWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, &BrowserWindow::onTabChanged);
    connect(m_tabs, &PacitumTabWidget::newTabRequested, this, [this]{ newTab(); });
    connect(m_tabs, &PacitumTabWidget::tabSplitRequested, this, &BrowserWindow::toggleSplitView);
    connect(m_tabs, &PacitumTabWidget::tabDuplicateRequested, this, &BrowserWindow::duplicateTab);
    connect(m_tabs, &PacitumTabWidget::tabReopenRequested, this, &BrowserWindow::reopenClosedTab);
    connect(m_tabs, &PacitumTabWidget::tabMuteRequested, this, &BrowserWindow::muteTab);
    connect(m_tabs, &PacitumTabWidget::tabPinRequested, this, &BrowserWindow::pinTab);
    m_splitter->addWidget(m_tabs);
    m_activeWidget = m_tabs;

    // Vertical stack per design: TabStrip 40 -> Toolbar 44 -> BookmarksBar 32 -> content
    m_tabStrip = m_tabs->createTabStrip(central);
    mainLay->insertWidget(0, m_tabStrip);
    mainLay->insertWidget(1, m_toolbar);

    m_splitTabs = new PacitumTabWidget(m_verticalTabs, this);
    m_splitTabs->hide();
    connect(m_splitTabs, &PacitumTabWidget::tabCloseRequested, this, &BrowserWindow::closeSplitTab);
    connect(m_splitTabs, &QTabWidget::currentChanged, this, &BrowserWindow::onTabChanged);
    connect(m_splitTabs, &PacitumTabWidget::newTabRequested, this, [this]{ newTab(QString{}); /* in split */ });
    connect(m_splitTabs, &PacitumTabWidget::tabSplitRequested, this, &BrowserWindow::toggleSplitView);
    connect(m_splitTabs, &PacitumTabWidget::tabDuplicateRequested, this, &BrowserWindow::duplicateTab);
    m_splitter->addWidget(m_splitTabs);

    setupShortcuts();
    createMenuBar();
    createStatusBar();
    menuBar()->hide();          // design uses the ⋮ main menu instead of a menu bar
    statusBar()->hide();
    if (auto* sh = QGuiApplication::styleHints())
        connect(sh, &QStyleHints::colorSchemeChanged, this, [this]{ if (m_themeSetting == QStringLiteral("System")) applyTheme(); });
    applyTheme();
}

QToolButton* BrowserWindow::makeToolBtn(const QString& tip) {
    auto* b = new QToolButton(m_toolbar);
    b->setFixedSize(32,32); b->setIconSize(QSize(18,18)); b->setAutoRaise(true);
    b->setToolTip(tip); b->setFocusPolicy(Qt::TabFocus);
    return b;
}

void BrowserWindow::createToolbar() {
    m_toolbar = new QWidget(this); m_toolbar->setObjectName(QStringLiteral("NavToolbar"));
    m_toolbar->setAttribute(Qt::WA_StyledBackground, true);
    m_toolbar->setFixedHeight(44);
    auto* lay = new QHBoxLayout(m_toolbar); lay->setContentsMargins(8,0,8,0); lay->setSpacing(4);

    m_backBtn = makeToolBtn(QStringLiteral("Back (Alt+Left)")); connect(m_backBtn, &QToolButton::clicked, this, &BrowserWindow::goBack);
    m_forwardBtn = makeToolBtn(QStringLiteral("Forward (Alt+Right)")); connect(m_forwardBtn, &QToolButton::clicked, this, &BrowserWindow::goForward);
    m_reloadBtn = makeToolBtn(QStringLiteral("Reload (F5)"));
    connect(m_reloadBtn, &QToolButton::clicked, this, [this]{
        auto* t = activeTab(); if (!t) return;
        if (t->isLoading()) t->stop(); else t->reload();
    });
    m_homeBtn = makeToolBtn(QStringLiteral("Home")); connect(m_homeBtn, &QToolButton::clicked, this, &BrowserWindow::goHome);
    m_addressBar = new AddressBar(m_history, this);
    connect(m_addressBar, &AddressBar::returnPressed, this, &BrowserWindow::navigateToUrl);
    connect(m_addressBar, &AddressBar::starClicked, this, &BrowserWindow::toggleBookmark);
    connect(m_addressBar, &AddressBar::securityIconClicked, this, &BrowserWindow::showPageInfo);
    m_downloadsBtn = makeToolBtn(QStringLiteral("Downloads (Ctrl+J)")); connect(m_downloadsBtn, &QToolButton::clicked, this, &BrowserWindow::showDownloads);
    m_splitBtn = makeToolBtn(QStringLiteral("Split view")); connect(m_splitBtn, &QToolButton::clicked, this, [this]{ toggleSplitView(); });
    m_privateBadge = new QLabel(m_toolbar); m_privateBadge->setFixedSize(32,32); m_privateBadge->setAlignment(Qt::AlignCenter);
    m_privateBadge->setToolTip(QStringLiteral("Private browsing")); m_privateBadge->hide();
    m_profileBtn = makeToolBtn(QStringLiteral("Profile"));
    auto* avatar = new QLabel(QStringLiteral("P"), m_profileBtn); avatar->setObjectName(QStringLiteral("Avatar"));
    avatar->setAlignment(Qt::AlignCenter); avatar->setFixedSize(24,24); avatar->move(4,4);
    avatar->setAttribute(Qt::WA_TransparentForMouseEvents);
    connect(m_profileBtn, &QToolButton::clicked, this, &BrowserWindow::showSettings);
    m_menuBtn = makeToolBtn(QStringLiteral("Customize and control Pacitum (Alt+F)")); connect(m_menuBtn, &QToolButton::clicked, this, &BrowserWindow::showMenu);

    lay->addWidget(m_backBtn); lay->addWidget(m_forwardBtn); lay->addWidget(m_reloadBtn); lay->addWidget(m_homeBtn);
    lay->addSpacing(8);
    lay->addWidget(m_addressBar, 1);
    lay->addSpacing(8);
    lay->addWidget(m_downloadsBtn); lay->addWidget(m_splitBtn); lay->addWidget(m_privateBadge); lay->addWidget(m_profileBtn); lay->addWidget(m_menuBtn);
}

void BrowserWindow::refreshIcons() {
    auto* tm = m_theme;
    m_backBtn->setIcon(tm->icon(QStringLiteral("back")));
    m_forwardBtn->setIcon(tm->icon(QStringLiteral("forward")));
    auto* t = activeTab();
    m_reloadBtn->setIcon(tm->icon(t && t->isLoading() ? QStringLiteral("stop") : QStringLiteral("reload")));
    m_reloadBtn->setToolTip(t && t->isLoading() ? QStringLiteral("Stop loading") : QStringLiteral("Reload (F5)"));
    m_homeBtn->setIcon(tm->icon(QStringLiteral("home")));
    m_downloadsBtn->setIcon(tm->icon(QStringLiteral("download")));
    m_splitBtn->setIcon(tm->icon(QStringLiteral("window")));
    m_menuBtn->setIcon(tm->icon(QStringLiteral("menu")));
    m_privateBadge->setPixmap(tm->icon(QStringLiteral("private")).pixmap(QSize(18,18), devicePixelRatioF()));
}

void BrowserWindow::updateNavState() {
    auto* t = activeTab();
    m_backBtn->setEnabled(t && t->canGoBack());
    m_forwardBtn->setEnabled(t && t->canGoForward());
    refreshIcons();
}

void BrowserWindow::updateTabIcon(BrowserTab* tab) {
    for (auto* tw : {m_tabs, m_splitTabs}) {
        int idx = tw->indexOf(tab);
        if (idx < 0) continue;
        QIcon ic = tab->icon();
        if (ic.isNull()) {
            const QString u = tab->currentUrl();
            ic = u.startsWith(QStringLiteral("pacitum:")) || u.isEmpty()
                 ? QIcon(QStringLiteral(":/icons/pacitum-icon-32.png"))
                 : m_theme->secondaryIcon(QStringLiteral("globe"), 16);
        }
        tw->setTabIcon(idx, ic);
    }
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
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+T")), this, [this]{ newTab(); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+W")), this, [this]{ closeTab(m_tabs->currentIndex()); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+D")), this, SLOT(toggleBookmark()));
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+B")), this, SLOT(toggleBookmarksBar()));
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")), this, SLOT(newPrivateTab()));
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")), this, SLOT(reopenClosedTab()));
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+=")), this, SLOT(zoomIn()));
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+-")), this, SLOT(zoomOut()));
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+0")), this, SLOT(zoomReset()));
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+,")), this, SLOT(showSettings()));
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+P")), this, [this]{ if (auto* t = activeTab()) t->findChild<QWebEngineView*>()->printToPdf(QDir::homePath() + QStringLiteral("/page.pdf")); });
    new QShortcut(QKeySequence(QStringLiteral("F11")), this, SLOT(toggleFullscreen()));
    new QShortcut(QKeySequence(QStringLiteral("Alt+F")), this, SLOT(showMenu()));
    new QShortcut(QKeySequence(QStringLiteral("Alt+Left")), this, SLOT(goBack()));
    new QShortcut(QKeySequence(QStringLiteral("Alt+Right")), this, SLOT(goForward()));
    for (int i = 1; i <= 9; ++i)
        new QShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i)), this, [this, i]{
            if (i == 9) lastTab(); else if (i - 1 < m_tabs->count()) m_tabs->setCurrentIndex(i - 1); });
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
    view->addAction(QStringLiteral("Toggle Dark Mode"), this, &BrowserWindow::toggleDarkMode);
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
    QString u = url.isEmpty() ? PacitumSchemeHandler::newTabUrl() : url;
    auto* tab = new BrowserTab(u, m_privateMode, m_searchEngine, this);
    tab->setZoomFactor(m_zoom->zoom());
    connect(tab, &BrowserTab::urlChanged, this, &BrowserWindow::updateAddressBar);
    connect(tab, &BrowserTab::titleChanged, this, &BrowserWindow::updateTabTitle);
    connect(tab, &BrowserTab::loadingChanged, this, &BrowserWindow::onTabLoading);
    connect(tab, &BrowserTab::iconChanged, this, [this, tab]{ updateTabIcon(tab); });
    connect(tab, &BrowserTab::urlChanged, this, [this, tab]{ updateTabIcon(tab); });
    // choose target
    PacitumTabWidget* target = m_splitActive && m_splitTabs->isVisible() && m_splitTabs->count()==0 ? m_splitTabs : m_tabs;
    // if split active and caller wants split, use split; otherwise default to main
    // For regular newTab, use main tabs
    target = m_tabs;
    int idx = target->addTab(tab, QStringLiteral("New Tab"));
    target->setCurrentIndex(idx);
    updateTabIcon(tab);
    if (url.isEmpty()) QTimer::singleShot(0, this, [this]{ m_addressBar->focusUrl(); });
}

void BrowserWindow::newPrivateTab() {
    m_privateMode = true; newTab(); m_privateMode = false;
    int idx = m_tabs->currentIndex();
    m_tabs->setTabText(idx, QStringLiteral("Private — ") + m_tabs->tabText(idx));
    m_privateBadge->show();
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
    auto* sender = qobject_cast<PacitumTabWidget*>(QObject::sender());
    if (!sender || index < 0) return;
    m_activeWidget = sender;
    if (auto* tab = qobject_cast<BrowserTab*>(sender->widget(index))) {
        updateAddressBar(tab->currentUrl());
        updateNavState();
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
    if (!url.isEmpty() && !url.startsWith(QStringLiteral("about:")) && !url.startsWith(QStringLiteral("pacitum:"))) {
        auto* t = activeTab(); m_history->addVisit(url, t ? t->currentTitle() : QString{});
    }
    // bookmark indicator
    m_addressBar->setBookmarked(m_bookmarks->isBookmarked(url));
}

void BrowserWindow::updateTabTitle(const QString& title) {
    auto* sender = qobject_cast<BrowserTab*>(QObject::sender());
    if (!sender) return;
    for (auto* tw : {m_tabs, m_splitTabs}) {
        int idx = tw->indexOf(sender);
        if (idx >= 0) { tw->setTabText(idx, title.left(30) + (title.size()>30 ? QStringLiteral("...") : QString{})); break; }
    }
}

void BrowserWindow::onTabLoading(bool) {
    auto* tab = qobject_cast<BrowserTab*>(sender());
    if (!tab || tab != activeTab()) return;
    updateNavState();
}

void BrowserWindow::goBack() { if (auto* t=activeTab()) { t->goBack(); QTimer::singleShot(200, this, [this]{ if(auto* tt=activeTab()){ m_backBtn->setEnabled(tt->canGoBack()); }}); } }
void BrowserWindow::goForward() { if (auto* t=activeTab()) { t->goForward(); QTimer::singleShot(200, this, [this]{ if(auto* tt=activeTab()){ m_forwardBtn->setEnabled(tt->canGoForward()); }}); } }
void BrowserWindow::reloadPage() { if (auto* t=activeTab()) t->reload(); }
void BrowserWindow::goHome() { navigateToUrl(readSettings().value(QStringLiteral("homepage")).toString(PacitumSchemeHandler::newTabUrl())); }

void BrowserWindow::toggleBookmark() {
    auto* t = activeTab(); if (!t) return;
    QString url = t->currentUrl(), title = t->currentTitle();
    if (url.isEmpty()) return;
    if (m_bookmarks->isBookmarked(url)) { m_bookmarks->removeBookmark(url); m_addressBar->setBookmarked(false); statusBar()->showMessage(QStringLiteral("Bookmark removed"), 2000); }
    else { m_bookmarks->addBookmark(title, url); m_addressBar->setBookmarked(true); statusBar()->showMessage(QStringLiteral("Bookmark added"), 2000); }
    m_bookmarksBar->refresh();
}

void BrowserWindow::showDownloads() { m_downloads->show(); }
void BrowserWindow::showSettings() { SettingsDialog dlg(this); if (dlg.exec()) { loadSettings(); applyTheme(); } }
void BrowserWindow::showMenu() {
    // Main menu popup (design-spec §4 "Main menu"): 300 wide, rows 32, icons 18 secondary.
    QMenu m(this);
    m.setMinimumWidth(300);
    m.setWindowFlags(m.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    m.setAttribute(Qt::WA_TranslucentBackground);
    auto add = [&](const QString& icon, const QString& text, const QString& keys, auto&& fn) {
        auto* a = m.addAction(m_theme->secondaryIcon(icon, 18), text);
        if (!keys.isEmpty()) a->setShortcut(QKeySequence(keys));
        a->setShortcutVisibleInContextMenu(true);
        connect(a, &QAction::triggered, this, fn);
        return a;
    };
    add(QStringLiteral("add"), QStringLiteral("New tab"), QStringLiteral("Ctrl+T"), [this]{ newTab(); });
    add(QStringLiteral("window"), QStringLiteral("New window"), QStringLiteral("Ctrl+N"), [this]{ auto* w = new BrowserWindow(); w->setAttribute(Qt::WA_DeleteOnClose); w->show(); });
    add(QStringLiteral("private"), QStringLiteral("New private tab"), QStringLiteral("Ctrl+Shift+N"), [this]{ newPrivateTab(); });
    m.addSeparator();
    add(QStringLiteral("history"), QStringLiteral("History"), QStringLiteral("Ctrl+H"), [this]{ showHistory(); });
    add(QStringLiteral("download"), QStringLiteral("Downloads"), QStringLiteral("Ctrl+J"), [this]{ showDownloads(); });
    add(QStringLiteral("bookmarks"), QStringLiteral("Bookmarks"), QStringLiteral("Ctrl+Shift+O"), [this]{ showBookmarksDialog(); });
    add(QStringLiteral("extensions"), QStringLiteral("Split view"), QString(), [this]{ toggleSplitView(); });
    m.addSeparator();

    // Zoom row: [icon] Zoom ........ [−] 100% [+]
    auto* zoomRow = new QWidget(&m);
    auto* zl = new QHBoxLayout(zoomRow); zl->setContentsMargins(10,2,6,2); zl->setSpacing(4);
    auto* zi = new QLabel(zoomRow); zi->setPixmap(m_theme->secondaryIcon(QStringLiteral("zoom-in"), 18).pixmap(QSize(18,18), devicePixelRatioF()));
    auto* zt = new QLabel(QStringLiteral("Zoom"), zoomRow);
    auto* zminus = new QToolButton(zoomRow); zminus->setFixedSize(28,28); zminus->setIcon(m_theme->icon(QStringLiteral("zoom-out"), 16)); zminus->setAutoRaise(true);
    auto* zpct = new QLabel(QString::number(m_zoom->zoomPercent()) + QStringLiteral("%"), zoomRow); zpct->setFixedWidth(44); zpct->setAlignment(Qt::AlignCenter);
    auto* zplus = new QToolButton(zoomRow); zplus->setFixedSize(28,28); zplus->setIcon(m_theme->icon(QStringLiteral("zoom-in"), 16)); zplus->setAutoRaise(true);
    connect(zminus, &QToolButton::clicked, this, [this, zpct]{ zoomOut(); zpct->setText(QString::number(m_zoom->zoomPercent()) + QStringLiteral("%")); });
    connect(zplus, &QToolButton::clicked, this, [this, zpct]{ zoomIn(); zpct->setText(QString::number(m_zoom->zoomPercent()) + QStringLiteral("%")); });
    zl->addWidget(zi); zl->addSpacing(8); zl->addWidget(zt); zl->addStretch(1); zl->addWidget(zminus); zl->addWidget(zpct); zl->addWidget(zplus);
    zoomRow->setFixedHeight(32);
    auto* za = new QWidgetAction(&m); za->setDefaultWidget(zoomRow); m.addAction(za);
    m.addSeparator();
    add(QStringLiteral("print"), QStringLiteral("Print to PDF…"), QStringLiteral("Ctrl+P"), [this]{ if (auto* t = activeTab()) if (auto* v = t->findChild<QWebEngineView*>()) { v->printToPdf(QDir::homePath() + QStringLiteral("/page.pdf")); } });
    add(QStringLiteral("find"), QStringLiteral("Find in page"), QStringLiteral("Ctrl+F"), [this]{ showFind(); });
    m.addSeparator();
    add(QStringLiteral("settings"), QStringLiteral("Settings"), QStringLiteral("Ctrl+,"), [this]{ showSettings(); });
    add(QStringLiteral("palette"), m_darkMode ? QStringLiteral("Light theme") : QStringLiteral("Dark theme"), QString(), [this]{ toggleDarkMode(); });
    add(QStringLiteral("info"), QStringLiteral("About Pacitum"), QString(), [this]{
        QMessageBox::about(this, QStringLiteral("About Pacitum Browser"),
            QStringLiteral("<b>Pacitum Browser</b> %1<br>A calm, fast web browser built on Qt %2 WebEngine.")
                .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion()))); });
    add(QStringLiteral("exit"), QStringLiteral("Exit"), QStringLiteral("Ctrl+Q"), [this]{ close(); });

    const QSize sz = m.sizeHint();
    QPoint anchor = m_menuBtn->mapToGlobal(QPoint(m_menuBtn->width(), m_menuBtn->height() + 4));
    m.exec(QPoint(anchor.x() - sz.width(), anchor.y()));
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
void BrowserWindow::toggleBookmarksBar() { m_bookmarksBar->setVisible(!m_bookmarksBar->isVisible()); auto s = readSettings(); s[QStringLiteral("show_bookmarks_bar")] = m_bookmarksBar->isVisible(); QFile f(QStringLiteral("browser_settings.json")); if (f.open(QIODevice::WriteOnly|QIODevice::Truncate)) f.write(QJsonDocument(s).toJson(QJsonDocument::Indented)); }
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

void BrowserWindow::toggleDarkMode(){ m_darkMode=!m_darkMode; m_themeSetting = m_darkMode ? QStringLiteral("Dark") : QStringLiteral("Light"); applyTheme(); saveSettings(); }

void BrowserWindow::applyTheme(){
    if (m_themeSetting == QStringLiteral("System")) {
        auto* sh = QGuiApplication::styleHints();
        m_darkMode = sh ? sh->colorScheme() != Qt::ColorScheme::Light : true;
    }
    m_theme->setAccent(m_accent.compare(QStringLiteral("#2DD4BF"), Qt::CaseInsensitive) == 0 ? QString() : m_accent);
    m_theme->setDarkMode(m_darkMode);
    const auto& p = m_theme->palette();
    qApp->setStyleSheet(m_theme->mainWindowStyle());
    m_tabs->applyTheme(m_theme);
    m_splitTabs->applyTheme(m_theme);
    m_addressBar->applyTheme(m_theme);
    applyToolbarTheme();
    refreshIcons();
    for (auto* tw : {m_tabs, m_splitTabs})
        for (int i = 0; i < tw->count(); ++i) if (auto* t = qobject_cast<BrowserTab*>(tw->widget(i))) updateTabIcon(t);

    auto* h = PacitumSchemeHandler::instance();
    h->setThemeVars({{QStringLiteral("TOOL"), p.toolbar}, {QStringLiteral("PAGE"), p.page}, {QStringLiteral("SURF"), p.surface},
                     {QStringLiteral("BORD"), p.border}, {QStringLiteral("TXT"), p.text}, {QStringLiteral("SEC"), p.textSecondary},
                     {QStringLiteral("ACC"), p.accent}, {QStringLiteral("ACCS"), p.accentSoft}});
    for (auto* tw : {m_tabs, m_splitTabs})
        for (int i = 0; i < tw->count(); ++i)
            if (auto* t = qobject_cast<BrowserTab*>(tw->widget(i)))
                if (t->currentUrl().startsWith(QStringLiteral("pacitum:"))) t->reload();
}

void BrowserWindow::applyToolbarTheme(){
    // Styling for TabStrip / NavToolbar / BookmarksBar lives in the app-wide QSS (ThemeManager).
    m_bookmarksBar->setStyleSheet(QString());
    m_bookmarksBar->applyTheme(m_theme);
    m_toolbar->setStyleSheet(QString());
}

QJsonObject BrowserWindow::readSettings() const {
    QFile f(QStringLiteral("browser_settings.json")); if(!f.open(QIODevice::ReadOnly)) return {};
    auto doc=QJsonDocument::fromJson(f.readAll()); return doc.isObject()?doc.object():QJsonObject{};
}

void BrowserWindow::loadSettings(){
    auto s=readSettings();
    m_themeSetting = s.value(QStringLiteral("theme")).toString(s.contains(QStringLiteral("dark_mode")) ? (s.value(QStringLiteral("dark_mode")).toBool() ? QStringLiteral("Dark") : QStringLiteral("Light")) : QStringLiteral("System"));
    m_darkMode = m_themeSetting == QStringLiteral("Dark");
    m_accent = s.value(QStringLiteral("accent")).toString(QStringLiteral("#2DD4BF"));
    m_searchEngine = s.value(QStringLiteral("search_engine")).toString(QStringLiteral("Google"));
    {
        static const QMap<QString,QString> engines{
            {QStringLiteral("Google"), QStringLiteral("https://www.google.com/search?q=")},
            {QStringLiteral("Bing"), QStringLiteral("https://www.bing.com/search?q=")},
            {QStringLiteral("DuckDuckGo"), QStringLiteral("https://duckduckgo.com/?q=")},
            {QStringLiteral("Yahoo"), QStringLiteral("https://search.yahoo.com/search?p=")},
            {QStringLiteral("Brave"), QStringLiteral("https://search.brave.com/search?q=")}};
        PacitumSchemeHandler::instance()->setSearchBase(engines.value(m_searchEngine, engines.value(QStringLiteral("Google"))));
    }
    m_adblock->setEnabled(s.value(QStringLiteral("adblock_enabled")).toBool(true));
    m_bookmarksBar->setVisible(s.value(QStringLiteral("show_bookmarks_bar")).toBool(true));
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
