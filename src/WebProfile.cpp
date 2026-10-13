#include "WebProfile.h"
#include "PacitumScheme.h"
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QtWebEngineCore/qtwebenginecoreglobal.h>
#include <QRegularExpression>
#include <QLocale>
#include <QStringList>
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QWebEngineClientHints>
#endif

namespace Pacitum {

WebPrefs& webPrefs() { static WebPrefs p; return p; }

QWebEngineProfile* mainProfile() {
    static QWebEngineProfile* p = new QWebEngineProfile(QStringLiteral("Pacitum"));
    return p;
}

static QString acceptLanguage() {
    QStringList langs;
    for (QString l : QLocale::system().uiLanguages()) {
        l.replace(QLatin1Char('_'), QLatin1Char('-'));
        if (!langs.contains(l, Qt::CaseInsensitive)) langs << l;
        const QString base = l.section(QLatin1Char('-'), 0, 0);
        if (!langs.contains(base, Qt::CaseInsensitive)) langs << base;
    }
    if (!langs.contains(QStringLiteral("en"), Qt::CaseInsensitive)) { langs << QStringLiteral("en-US") << QStringLiteral("en"); }
    QStringList out; double q = 1.0;
    for (int i = 0; i < langs.size() && i < 6; ++i) {
        if (i == 0) out << langs[i];
        else { q = qMax(0.1, 1.0 - 0.1 * i); out << QStringLiteral("%1;q=%2").arg(langs[i]).arg(q, 0, 'f', 1); }
    }
    return out.join(QLatin1Char(','));
}

static QString chromeUserAgent(const QString& defaultUa) {
    const QString chromiumVer = QString::fromLatin1(qWebEngineChromiumVersion());
    const QString major = chromiumVer.section(QLatin1Char('.'), 0, 0);
    QString ua = defaultUa;
    ua.remove(QRegularExpression(QStringLiteral("QtWebEngine/\\S+\\s*")));
    // Chrome ships a "reduced" UA: Chrome/<major>.0.0.0
    if (!major.isEmpty())
        ua.replace(QRegularExpression(QStringLiteral("Chrome/[\\d.]+")), QStringLiteral("Chrome/%1.0.0.0").arg(major));
    return ua.simplified();
}

void configureProfile(QWebEngineProfile* profile, bool persistent) {
    if (!profile) return;
    profile->setHttpUserAgent(chromeUserAgent(profile->httpUserAgent()));
    profile->setHttpAcceptLanguage(acceptLanguage());
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    if (auto* ch = profile->clientHints()) {
        const QString full = QString::fromLatin1(qWebEngineChromiumVersion());
        const QString major = full.section(QLatin1Char('.'), 0, 0);
        ch->setFullVersion(full);
        ch->setFullVersionList({{QStringLiteral("Chromium"), full}, {QStringLiteral("Google Chrome"), full},
                                {QStringLiteral("Not-A.Brand"), QStringLiteral("99.0.0.0")}});
        Q_UNUSED(major);
    }
#endif
    if (persistent) {
        profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
        profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
        profile->setHttpCacheMaximumSize(250 * 1024 * 1024);
    }
    auto* s = profile->settings();
    s->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    s->setAttribute(QWebEngineSettings::WebGLEnabled, true);
    s->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, true);
    s->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, true);
    s->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    s->setAttribute(QWebEngineSettings::ErrorPageEnabled, true);
    applyWebSettings(s);
    PacitumSchemeHandler::install(profile);
}

void applyWebSettings(QWebEngineSettings* s) {
    if (!s) return;
    s->setAttribute(QWebEngineSettings::JavascriptEnabled, webPrefs().javascript);
    s->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, webPrefs().popups);
}

}
