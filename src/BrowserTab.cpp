#include "BrowserTab.h"
#include "FindBar.h"
#include "PacitumScheme.h"
#include "WebProfile.h"
#include <functional>

std::function<BrowserTab*()> BrowserTab::s_popupFactory;

namespace {
class PacitumWebView : public QWebEngineView {
public:
    using QWebEngineView::QWebEngineView;
protected:
    QWebEngineView* createWindow(QWebEnginePage::WebWindowType) override {
        if (!BrowserTab::s_popupFactory) return nullptr;
        BrowserTab* t = BrowserTab::s_popupFactory();
        return t ? t->view() : nullptr;
    }
};
}

#include <QVBoxLayout>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineHistory>
#include <QUrl>
#include <QUrlQuery>
#include <QTimer>
#include <QEasingCurve>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include "ThemeManager.h"

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

    m_view = new PacitumWebView(this);
    if (m_private) {
        m_profile = new QWebEngineProfile(this);   // off-the-record
        m_profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
        Pacitum::configureProfile(m_profile, false);
        m_page = new QWebEnginePage(m_profile, m_view);
    } else {
        m_page = new QWebEnginePage(Pacitum::mainProfile(), m_view);
    }
    m_view->setPage(m_page);
    m_page->setUrlRequestInterceptor(new Pacitum::GoogleSignInInterceptor(m_page));

    applyWebSettings();

    connect(m_view->page(), &QWebEnginePage::renderProcessTerminated, this, &BrowserTab::onRenderCrashed);

    buildSignInBar();
    layout->addWidget(m_signInBar);
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
    if (url.isEmpty()) url = PacitumSchemeHandler::newTabUrl();
    if (!url.startsWith(QStringLiteral("http://")) && !url.startsWith(QStringLiteral("https://"))
        && !url.startsWith(QStringLiteral("file://")) && !url.startsWith(QStringLiteral("about:")) && !url.startsWith(QStringLiteral("pacitum:"))
        && !url.startsWith(QStringLiteral("data:"))) {
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

void BrowserTab::applyWebSettings() { Pacitum::applyWebSettings(m_view->settings()); }

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

void BrowserTab::setLazyUrl(const QString& url, const QString& title) {
    m_pendingUrl = url; m_url = url;
    m_title = title.isEmpty() ? url : title;
    emit urlChanged(m_url); emit titleChanged(m_title);
}
void BrowserTab::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (!m_pendingUrl.isEmpty()) { const QString u = m_pendingUrl; m_pendingUrl.clear(); loadUrl(u); }
}

void BrowserTab::buildSignInBar() {
    m_signInBar = new QFrame(this);
    m_signInBar->setObjectName(QStringLiteral("SignInBar"));
    auto* h = new QHBoxLayout(m_signInBar); h->setContentsMargins(12, 6, 8, 6); h->setSpacing(10);
    m_signInIcon = new QLabel(m_signInBar);
    auto* text = new QLabel(tr("Google sign-in isn't supported here"), m_signInBar);
    text->setObjectName(QStringLiteral("SignInText"));
    auto* open = new QPushButton(tr("Open in default browser"), m_signInBar);
    open->setObjectName(QStringLiteral("SignInOpen")); open->setCursor(Qt::PointingHandCursor);
    auto* close = new QToolButton(m_signInBar);
    close->setObjectName(QStringLiteral("SignInClose")); close->setAutoRaise(true); close->setCursor(Qt::PointingHandCursor);
    m_signInClose = close;
    h->addWidget(m_signInIcon); h->addWidget(text); h->addStretch(1); h->addWidget(open); h->addWidget(close);
    connect(open, &QPushButton::clicked, this, [this]{
        const QUrl target = m_signInUrl.isValid() ? m_signInUrl : m_view->url();
        QDesktopServices::openUrl(target);
    });
    connect(close, &QToolButton::clicked, m_signInBar, &QWidget::hide);
    m_signInBar->hide();
}

void BrowserTab::showSignInBar(const QUrl& target) {
    const auto& p = ThemeManager::current();
    m_signInBar->setStyleSheet(QStringLiteral(
        "#SignInBar{background:%1;border-bottom:1px solid %2;}"
        "#SignInText{color:%3;font-size:13px;}"
        "#SignInOpen{background:%4;color:%5;border:none;border-radius:8px;padding:5px 12px;font-weight:600;}"
        "#SignInOpen:hover{background:%6;}"
        "#SignInClose{border:none;border-radius:6px;padding:3px;} #SignInClose:hover{background:%7;}")
        .arg(p.toolbar, p.border, p.text, p.accent, p.onAccent, p.accent, p.surfaceHover));
    m_signInIcon->setPixmap(ThemeManager::icon(QStringLiteral("warning"), p.textSecondary, 18).pixmap(18, 18));
    m_signInClose->setIcon(ThemeManager::icon(QStringLiteral("close"), p.textSecondary, 16));
    m_signInUrl = target;
    m_signInBar->show();
}

void BrowserTab::checkGoogleSignIn(const QUrl& u) {
    if (Pacitum::isGoogleSignInBlocked(u)) {
        // The rejection page carries the original sign-in URL in "continue" when present.
        QUrl target(QStringLiteral("https://accounts.google.com/"));
        const QString cont = QUrlQuery(u).queryItemValue(QStringLiteral("continue"), QUrl::FullyDecoded);
        if (!cont.isEmpty()) {
            const QUrl c(cont);
            if (c.scheme() == QLatin1String("https")) target = c;
        }
        showSignInBar(target);
        return;
    }
    if (!Pacitum::isGoogleSignInHost(u.host())) { m_signInBar->hide(); return; }
    // Fallback: detect the rejection by page text (Google sometimes keeps the generic /v3/signin URL).
    m_view->page()->runJavaScript(QStringLiteral(
        "(function(){var t=(document.body&&document.body.innerText)||'';"
        "return /Couldn.t sign you in|This browser or app may not be secure/i.test(t);})()"),
        [this, u](const QVariant& v){ if (v.toBool() && m_view->url() == u) showSignInBar(u); });
}

void BrowserTab::onUrlChanged(const QUrl& u) { m_url = u.toString(); emit urlChanged(m_url); checkGoogleSignIn(u); }
void BrowserTab::onTitleChanged(const QString& t) { m_title = t.isEmpty() ? QStringLiteral("New Tab") : t; emit titleChanged(m_title); }
void BrowserTab::onLoadStarted() { m_loading = true; emit loadingChanged(true); m_progress->setValue(0); m_progress->show(); }
void BrowserTab::onLoadProgress(int p) { m_progress->setValue(p); emit progressChanged(p); }
void BrowserTab::onLoadFinished(bool) { checkGoogleSignIn(m_view->url()); m_loading = false; emit loadingChanged(false); m_progress->hide(); m_progress->setValue(100); }
void BrowserTab::onRenderCrashed(QWebEnginePage::RenderProcessTerminationStatus, int) { QTimer::singleShot(1000, this, &BrowserTab::reload); }

void BrowserTab::fadeIn() {
    if (m_anim) { m_anim->stop(); m_anim->deleteLater(); }
    m_opacity->setEnabled(true);
    m_anim = new QPropertyAnimation(m_opacity, "opacity", this);
    m_anim->setDuration(200); m_anim->setStartValue(0.0); m_anim->setEndValue(1.0);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QPropertyAnimation::finished, this, [this]{ m_opacity->setEnabled(false); });  // no offscreen cost when idle
    m_anim->start();
}
