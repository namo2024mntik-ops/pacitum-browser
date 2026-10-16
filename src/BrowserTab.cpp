#include "BrowserTab.h"
#include <QResizeEvent>
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
#include <QDateTime>
#include <QPointer>
#include <QHideEvent>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include "Motion.h"
#include <QEasingCurve>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include "ThemeManager.h"
#include "Widgets.h"

int BrowserTab::s_idCounter = 0;

BrowserTab::BrowserTab(const QString& url, bool privateMode, const QString& searchEngine, QWidget* parent)
    : QWidget(parent), m_private(privateMode), m_searchEngine(searchEngine) {
    s_idCounter++; m_tabId = s_idCounter;

    // (1.5.0-1.5.1 put a QGraphicsOpacityEffect on the whole tab, i.e. on the QWebEngineView. Graphics
    // effects re-render the GPU-composited web view offscreen on every frame; Qt documents them as
    // unsupported for such widgets, and it was a source of black/blank pages, fullscreen glitches
    // and crashes in the compositor. Page transitions now fade a plain veil widget instead.)

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
    // Website fullscreen (YouTube etc.): without a handler Qt rejects every request.
    connect(m_view->page(), &QWebEnginePage::fullScreenRequested, this, &BrowserTab::fullScreenRequested);
    // Background-tab lifecycle (1.5.2, conservative). 1.5.1 applied Chromium's recommendedState
    // directly, including Discarded, on any page whose widget was hidden: pages were frozen while
    // still loading, discarded tabs were then touched (zoom, theme reload, JavaScript, popups opened
    // into them), and the welcome tab's web channel could be discarded. Now: no discarding at all;
    // a tab is only frozen after it has been in the background for kFreezeAfterMs, is fully loaded,
    // silent, not an internal page, not fullscreen, and Chromium agrees it may be frozen. Any API
    // call that needs the page (load, reload, history, zoom, show) thaws it first.
    m_freezeTimer = new QTimer(this);
    m_freezeTimer->setSingleShot(true);
    m_freezeTimer->setInterval(kFreezeAfterMs);
    connect(m_freezeTimer, &QTimer::timeout, this, &BrowserTab::maybeFreeze);

    buildSignInBar();
    layout->addWidget(m_signInBar);
    layout->addWidget(m_view, 1);

    m_findBar = new FindBar(this);
    m_findBar->setBrowserView(m_view);
    layout->addWidget(m_findBar);

    connect(m_view, &QWebEngineView::urlChanged, this, &BrowserTab::onUrlChanged);
    connect(m_view, &QWebEngineView::titleChanged, this, &BrowserTab::onTitleChanged);
    connect(m_view, &QWebEngineView::loadStarted, this, &BrowserTab::onLoadStarted);
    connect(m_view, &QWebEngineView::loadProgress, this, &BrowserTab::onLoadProgress);
    connect(m_view, &QWebEngineView::loadFinished, this, &BrowserTab::onLoadFinished);
    connect(m_view, &QWebEngineView::iconChanged, this, &BrowserTab::iconChanged);

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
    ensureActive();
    m_view->setUrl(QUrl(url));
}

bool BrowserTab::s_freezeEnabled = true;

void BrowserTab::ensureActive() {
    if (m_freezeTimer) m_freezeTimer->stop();
    auto* pg = m_view ? m_view->page() : nullptr;
    if (pg && pg->lifecycleState() != QWebEnginePage::LifecycleState::Active)
        pg->setLifecycleState(QWebEnginePage::LifecycleState::Active);
}

void BrowserTab::maybeFreeze() {
    auto* pg = m_view ? m_view->page() : nullptr;
    if (!s_freezeEnabled || !pg || isVisible() || m_loading || m_noFreeze || !m_pendingUrl.isEmpty()) return;
    if (m_url.isEmpty() || m_url.startsWith(QStringLiteral("pacitum")) || m_url.startsWith(QStringLiteral("about:"))) return;
    if (pg->recentlyAudible() || pg->lifecycleState() != QWebEnginePage::LifecycleState::Active) return;
    if (pg->recommendedState() == QWebEnginePage::LifecycleState::Active) return;   // Chromium says no (e.g. WebRTC, devtools)
    pg->setLifecycleState(QWebEnginePage::LifecycleState::Frozen);
}

void BrowserTab::setNoFreeze(bool on) { m_noFreeze = on; if (on) ensureActive(); }

void BrowserTab::setWindowMinimized(bool on) {
    if (m_windowMinimized == on || !m_page) return;
    m_windowMinimized = on;
    if (isVisible()) {
        // The shown tab of a minimized window: Chromium still treated it as visible and ran
        // requestAnimationFrame, CSS animations and video at full rate. Hidden pages are throttled.
        if (!m_noFreeze || !on) m_page->setVisible(!on);
    } else if (m_freezeTimer && m_freezeTimer->isActive()) {
        m_freezeTimer->start(on ? kFreezeMinimizedMs : kFreezeAfterMs);   // background tab: freeze sooner while minimized
    }
}

void BrowserTab::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    if (!e->spontaneous() && s_freezeEnabled && m_freezeTimer) m_freezeTimer->start(m_windowMinimized ? kFreezeMinimizedMs : kFreezeAfterMs);   // tab switched away (not window minimize)
}

void BrowserTab::applyWebSettings() { Pacitum::applyWebSettings(m_view->settings()); }

void BrowserTab::goBack() { if (m_view->history()->canGoBack()) { ensureActive(); m_view->back(); } }
void BrowserTab::goForward() { if (m_view->history()->canGoForward()) { ensureActive(); m_view->forward(); } }
void BrowserTab::reload() { ensureActive(); m_view->reload(); }
void BrowserTab::stop() { m_view->stop(); }
bool BrowserTab::canGoBack() const { return m_view->history()->canGoBack(); }
bool BrowserTab::canGoForward() const { return m_view->history()->canGoForward(); }

void BrowserTab::setZoomFactor(double f) {
    m_zoom = qBound(0.25, f, 5.0);
    m_view->setZoomFactor(m_zoom);
}

void BrowserTab::setMuted(bool m) { m_view->page()->setAudioMuted(m); }
bool BrowserTab::isMuted() const { return m_view->page()->isAudioMuted(); }

void BrowserTab::refreshTheme() { if (m_signInBar) m_signInBar->refresh(); }
void BrowserTab::showFindBar() { m_findBar->showFind(); }
void BrowserTab::hideFindBar() { m_findBar->hideFind(); }

void BrowserTab::setLazyUrl(const QString& url, const QString& title) {
    m_pendingUrl = url; m_url = url;
    m_title = title.isEmpty() ? url : title;
    emit urlChanged(m_url); emit titleChanged(m_title);
}
void BrowserTab::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    ensureActive();   // thaw a frozen tab before it paints
    if (m_page && !m_windowMinimized && !m_page->isVisible()) m_page->setVisible(true);
    if (!m_pendingUrl.isEmpty()) { const QString u = m_pendingUrl; m_pendingUrl.clear(); loadUrl(u); }
}

void BrowserTab::buildSignInBar() {
    m_signInBar = new InfoBar(this);
    m_signInBar->setKind(InfoBar::Warning);
    m_signInBar->setMessage(tr("Google doesn't allow signing in from this browser."));
    m_signInBar->setPrimary(tr("Open in default browser"), QStringLiteral("open-in-browser"));
    connect(m_signInBar, &InfoBar::primaryClicked, this, [this]{
        const QUrl target = m_signInUrl.isValid() ? m_signInUrl : m_view->url();
        QDesktopServices::openUrl(target);
    });
}

void BrowserTab::showSignInBar(const QUrl& target) {
    m_signInUrl = target;
    m_signInBar->showAnimated();
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
    if (!Pacitum::isGoogleSignInHost(u.host())) { m_signInBar->hideAnimated(); return; }
    // Fallback: detect the rejection by page text (Google sometimes keeps the generic /v3/signin URL).
    m_view->page()->runJavaScript(QStringLiteral(
        "(function(){var t=(document.body&&document.body.innerText)||'';"
        "return /Couldn.t sign you in|This browser or app may not be secure/i.test(t);})()"),
        [this, u](const QVariant& v){ if (v.toBool() && m_view->url() == u) showSignInBar(u); });
}

void BrowserTab::onUrlChanged(const QUrl& u) { m_url = u.toString(); emit urlChanged(m_url); checkGoogleSignIn(u); }
void BrowserTab::onTitleChanged(const QString& t) { m_title = t.isEmpty() ? QStringLiteral("New Tab") : t; emit titleChanged(m_title); }
// Progress is shown in the omnibox (animations-spec §7). The old in-page 3 px QProgressBar was
// shown/hidden on every load and shifted the page down by 3 px each time; it is no longer shown.
void BrowserTab::onLoadStarted() { m_loading = true; m_loadValue = 0; emit loadingChanged(true); m_progress->setValue(0); }
void BrowserTab::onLoadProgress(int p) { m_loadValue = p; m_progress->setValue(p); emit progressChanged(p); }
void BrowserTab::onLoadFinished(bool ok) { checkGoogleSignIn(m_view->url()); m_loading = false; m_loadValue = 100; emit loadingChanged(false); emit loadDone(ok); m_progress->setValue(100); }
// Renderer gone: show the crashed state and reload once after a second -- but not in a loop (1.5.1
// reloaded forever when a page crashed its renderer on load, e.g. after a discard), and not when the
// renderer exited normally.
void BrowserTab::onRenderCrashed(QWebEnginePage::RenderProcessTerminationStatus st, int) {
    emit crashed();
    if (st == QWebEnginePage::NormalTerminationStatus) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastCrashMs > 60000) m_crashCount = 0;
    m_lastCrashMs = now;
    if (++m_crashCount > 2) return;   // user can reload manually
    QTimer::singleShot(1000, this, &BrowserTab::reload);
}

// Page transition (tab switch): a page-coloured veil over the tab fades out (160 ms OutCubic). The
// web view itself is never wrapped in a graphics effect; only the small veil widget is.
void BrowserTab::fadeIn() {
    if (m_veil) { m_veil->deleteLater(); m_veil = nullptr; }
    const int ms = Pacitum::motionMs(160);
    if (Pacitum::reducedMotion() || ms <= 0 || !isVisible() || width() < 2) return;
    auto* veil = new QWidget(this);
    veil->setAttribute(Qt::WA_TransparentForMouseEvents);
    veil->setAttribute(Qt::WA_StyledBackground, true);
    veil->setAutoFillBackground(true);
    QPalette pal = veil->palette(); pal.setColor(QPalette::Window, QColor(ThemeManager::current().page)); veil->setPalette(pal);
    veil->setGeometry(rect()); veil->show(); veil->raise();
    auto* eff = new QGraphicsOpacityEffect(veil); eff->setOpacity(1.0); veil->setGraphicsEffect(eff);
    auto* a = new QPropertyAnimation(eff, "opacity", veil);
    a->setDuration(ms); a->setStartValue(0.85); a->setEndValue(0.0); a->setEasingCurve(QEasingCurve::OutCubic);
    QPointer<QWidget> guard(veil);
    connect(a, &QPropertyAnimation::finished, veil, [this, guard]{ if (guard) { if (m_veil == guard) m_veil = nullptr; guard->deleteLater(); } });
    m_veil = veil;
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

// Back / forward (kit #10/#11): a grab() snapshot of the old page slides 32 px towards the
// direction of travel while fading out (200 ms OutCubic) over the view; the web view itself is
// never animated. Reduced motion: the snapshot only fades (120 ms). Dropped on resize.
void BrowserTab::navTransition(int dir) {
    if (m_navShot) { m_navShot->deleteLater(); m_navShot = nullptr; }
    if (!m_view || !isVisible() || m_view->width() < 2) return;
    const int fade = Motion::fadeMs(Motion::Medium);
    if (fade <= 0) return;
    const QPixmap pm = m_view->grab();
    if (pm.isNull()) return;
    auto* shot = new QLabel(this);
    shot->setAttribute(Qt::WA_TransparentForMouseEvents);
    shot->setPixmap(pm); shot->setGeometry(QRect(m_view->mapTo(this, QPoint(0, 0)), m_view->size())); shot->show(); shot->raise();
    auto* eff = new QGraphicsOpacityEffect(shot); shot->setGraphicsEffect(eff);
    auto* group = new QParallelAnimationGroup(shot);
    auto* op = new QPropertyAnimation(eff, "opacity"); op->setDuration(fade); op->setStartValue(1.0); op->setEndValue(0.0); op->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(op);
    if (Motion::enabled()) {
        auto* mv = new QPropertyAnimation(shot, "pos"); mv->setDuration(fade);
        mv->setStartValue(shot->pos()); mv->setEndValue(shot->pos() + QPoint(dir < 0 ? 32 : -32, 0)); mv->setEasingCurve(QEasingCurve::OutCubic);
        group->addAnimation(mv);
    }
    QPointer<QLabel> g(shot);
    connect(group, &QAbstractAnimation::finished, shot, [this, g]{ if (g) { if (m_navShot == g) m_navShot = nullptr; g->deleteLater(); } });
    m_navShot = shot;
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void BrowserTab::resizeEvent(QResizeEvent* e) {
    if (m_navShot) { m_navShot->deleteLater(); m_navShot = nullptr; }   // snapshot never outlives a resize
    QWidget::resizeEvent(e);
    if (m_veil) m_veil->setGeometry(rect());   // page-transition veil follows the tab size
}
