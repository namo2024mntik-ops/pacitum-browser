#include "BrowserTab.h"
#include "FindBar.h"
#include <QVBoxLayout>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineHistory>
#include <QUrl>
#include <QUrlQuery>
#include <QTimer>
#include <QEasingCurve>

int BrowserTab::s_idCounter = 0;

BrowserTab::BrowserTab(const QString& url, bool privateMode, const QString& searchEngine, QWidget* parent)
    : QWidget(parent), m_private(privateMode), m_searchEngine(searchEngine) {
    s_idCounter++; m_tabId = s_idCounter;

    m_opacity = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(m_opacity); m_opacity->setOpacity(0);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);

    m_progress = new QProgressBar(this);
    m_progress->setFixedHeight(3); m_progress->setTextVisible(false); m_progress->setRange(0,100); m_progress->hide();
    layout->addWidget(m_progress);

    m_view = new QWebEngineView(this);
    if (m_private) {
        m_profile = new QWebEngineProfile(this);
        m_profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
        m_page = new QWebEnginePage(m_profile, m_view);
        m_view->setPage(m_page);
    }

    auto* s = m_view->settings();
    s->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, true);
    s->setAttribute(QWebEngineSettings::WebGLEnabled, true);
    s->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, true);
    s->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    s->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    s->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    s->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    s->setAttribute(QWebEngineSettings::PluginsEnabled, true);

    connect(m_view->page(), &QWebEnginePage::renderProcessTerminated, this, &BrowserTab::onRenderCrashed);

    layout->addWidget(m_view, 1);

    m_findBar = new FindBar(this);
    m_findBar->setBrowserView(m_view);
    connect(m_findBar, &FindBar::closed, m_findBar, &QWidget::hide);
    layout->addWidget(m_findBar);

    connect(m_view, &QWebEngineView::urlChanged, this, &BrowserTab::onUrlChanged);
    connect(m_view, &QWebEngineView::titleChanged, this, &BrowserTab::onTitleChanged);
    connect(m_view, &QWebEngineView::loadStarted, this, &BrowserTab::onLoadStarted);
    connect(m_view, &QWebEngineView::loadProgress, this, &BrowserTab::onLoadProgress);
    connect(m_view, &QWebEngineView::loadFinished, this, &BrowserTab::onLoadFinished);
    connect(m_view, &QWebEngineView::iconChanged, this, &BrowserTab::iconChanged);

    fadeIn();
    if (!url.isEmpty()) loadUrl(url);
}

void BrowserTab::loadUrl(const QString& raw) {
    QString url = raw.trimmed();
    if (url.isEmpty()) url = QStringLiteral("https://www.google.com");
    if (!url.startsWith(QStringLiteral("http://")) && !url.startsWith(QStringLiteral("https://"))
        && !url.startsWith(QStringLiteral("file://")) && !url.startsWith(QStringLiteral("about:"))) {
        if (url.contains('.') && !url.contains(' ')) url = QStringLiteral("https://") + url;
        else {
            QMap<QString,QString> engines{
                {QStringLiteral("Google"), QStringLiteral("https://www.google.com/search?q=")},
                {QStringLiteral("Bing"), QStringLiteral("https://www.bing.com/search?q=")},
                {QStringLiteral("DuckDuckGo"), QStringLiteral("https://duckduckgo.com/?q=")},
                {QStringLiteral("Yahoo"), QStringLiteral("https://search.yahoo.com/search?p=")},
                {QStringLiteral("Brave"), QStringLiteral("https://search.brave.com/search?q=")}
            };
            QString base = engines.value(m_searchEngine, QStringLiteral("https://www.google.com/search?q="));
            url = base + QString(QUrl::toPercentEncoding(url));
        }
    }
    m_view->setUrl(QUrl(url));
}

void BrowserTab::goBack() { if (m_view->history()->canGoBack()) m_view->back(); }
void BrowserTab::goForward() { if (m_view->history()->canGoForward()) m_view->forward(); }
void BrowserTab::reload() { m_view->reload(); }
void BrowserTab::stop() { m_view->stop(); }
bool BrowserTab::canGoBack() const { return m_view->history()->canGoBack(); }
bool BrowserTab::canGoForward() const { return m_view->history()->canGoForward(); }

void BrowserTab::setZoomFactor(double f) {
    m_zoom = qBound(0.25, f, 5.0);
    m_view->setZoomFactor(m_zoom);
}

void BrowserTab::setMuted(bool m) { m_view->page()->setAudioMuted(m); }
bool BrowserTab::isMuted() const { return m_view->page()->isAudioMuted(); }

void BrowserTab::showFindBar() { m_findBar->showFind(); }
void BrowserTab::hideFindBar() { m_findBar->hide(); }

void BrowserTab::onUrlChanged(const QUrl& u) { m_url = u.toString(); emit urlChanged(m_url); }
void BrowserTab::onTitleChanged(const QString& t) { m_title = t.isEmpty() ? QStringLiteral("New Tab") : t; emit titleChanged(m_title); }
void BrowserTab::onLoadStarted() { emit loadingChanged(true); m_progress->setValue(0); m_progress->show(); }
void BrowserTab::onLoadProgress(int p) { m_progress->setValue(p); emit progressChanged(p); }
void BrowserTab::onLoadFinished(bool) { emit loadingChanged(false); m_progress->hide(); m_progress->setValue(100); }
void BrowserTab::onRenderCrashed(QWebEnginePage::RenderProcessTerminationStatus, int) { QTimer::singleShot(1000, this, &BrowserTab::reload); }

void BrowserTab::fadeIn() {
    m_anim = new QPropertyAnimation(m_opacity, "opacity", this);
    m_anim->setDuration(300); m_anim->setStartValue(0.0); m_anim->setEndValue(1.0);
    m_anim->setEasingCurve(QEasingCurve::OutCubic); m_anim->start();
}
