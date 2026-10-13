#include "PacitumScheme.h"
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineProfile>
#include <QBuffer>
#include <QFile>
#include <QUrl>

void PacitumSchemeHandler::registerScheme() {
    QWebEngineUrlScheme s("pacitum");
    s.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    s.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalScheme | QWebEngineUrlScheme::LocalAccessAllowed);
    QWebEngineUrlScheme::registerScheme(s);
}

PacitumSchemeHandler* PacitumSchemeHandler::instance() {
    static PacitumSchemeHandler* h = new PacitumSchemeHandler();
    return h;
}

void PacitumSchemeHandler::install(QWebEngineProfile* profile) {
    if (profile && !profile->urlSchemeHandler("pacitum")) profile->installUrlSchemeHandler("pacitum", instance());
}

void PacitumSchemeHandler::requestStarted(QWebEngineUrlRequestJob* job) {
    const QString host = job->requestUrl().host();
    QByteArray body;
    QByteArray mime = "text/html";
    if (host == QStringLiteral("newtab") || host.isEmpty()) {
        QFile f(QStringLiteral(":/pages/newtab.html"));
        if (!f.open(QIODevice::ReadOnly)) { job->fail(QWebEngineUrlRequestJob::UrlNotFound); return; }
        QString html = QString::fromUtf8(f.readAll());
        for (auto it = m_vars.cbegin(); it != m_vars.cend(); ++it) html.replace(QLatin1Char('%') + it.key() + QLatin1Char('%'), it.value());
        html.replace(QStringLiteral("%SEARCH%"), m_search);
        QFile logo(QStringLiteral(":/pages/logo.png"));
        if (logo.open(QIODevice::ReadOnly))
            html.replace(QStringLiteral("qrc:/pages/logo.png"), QStringLiteral("data:image/png;base64,") + QString::fromLatin1(logo.readAll().toBase64()));
        body = html.toUtf8();
    } else if (host == QStringLiteral("welcome")) {
        // First-run flow (firstrun-spec.md). Self-contained: tokens, icons, logo and qwebchannel.js inlined.
        QFile f(QStringLiteral(":/pages/welcome.html"));
        if (!f.open(QIODevice::ReadOnly)) { job->fail(QWebEngineUrlRequestJob::UrlNotFound); return; }
        QString html = QString::fromUtf8(f.readAll());
        for (auto it = m_vars.cbegin(); it != m_vars.cend(); ++it) html.replace(QLatin1Char('%') + it.key() + QLatin1Char('%'), it.value());
        auto svg = [](const QString& name, const QString& cls) {
            QFile s(QStringLiteral(":/icons/ui/%1.svg").arg(name));
            if (!s.open(QIODevice::ReadOnly)) return QString();
            QString t = QString::fromUtf8(s.readAll()).trimmed();
            t.replace(QStringLiteral(" width=\"24\" height=\"24\""), QString());
            t.replace(QStringLiteral("<svg "), QStringLiteral("<svg class=\"%1\" aria-hidden=\"true\" ").arg(cls));
            return t;
        };
        html.replace(QStringLiteral("%ARROW%"), svg(QStringLiteral("arrow-right"), QStringLiteral("i")));
        html.replace(QStringLiteral("%I_IMPORT%"), svg(QStringLiteral("import"), QStringLiteral("i")));
        html.replace(QStringLiteral("%I_DEFAULT%"), svg(QStringLiteral("default-browser"), QStringLiteral("i")));
        html.replace(QStringLiteral("%I_SYSTEM%"), svg(QStringLiteral("theme-system"), QStringLiteral("i")));
        html.replace(QStringLiteral("%I_SUN%"), svg(QStringLiteral("sun"), QStringLiteral("i")));
        html.replace(QStringLiteral("%I_MOON%"), svg(QStringLiteral("moon"), QStringLiteral("i")));
        html.replace(QStringLiteral("%I_CHECK%"), svg(QStringLiteral("check"), QStringLiteral("i")));
        html.replace(QStringLiteral("%I_SPARKLE%"), svg(QStringLiteral("sparkle"), QStringLiteral("sp")));
        QFile wc(QStringLiteral(":/qtwebchannel/qwebchannel.js"));
        html.replace(QStringLiteral("%QWEBCHANNEL%"), wc.open(QIODevice::ReadOnly) ? QString::fromUtf8(wc.readAll()) : QString());
        QFile logo(QStringLiteral(":/pages/logo.png"));
        if (logo.open(QIODevice::ReadOnly))
            html.replace(QStringLiteral("qrc:/pages/logo.png"), QStringLiteral("data:image/png;base64,") + QString::fromLatin1(logo.readAll().toBase64()));
        body = html.toUtf8();
    } else {
        job->fail(QWebEngineUrlRequestJob::UrlNotFound);
        return;
    }
    auto* buf = new QBuffer(job);
    buf->setData(body);
    job->reply(mime, buf);
}
