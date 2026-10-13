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
    } else {
        job->fail(QWebEngineUrlRequestJob::UrlNotFound);
        return;
    }
    auto* buf = new QBuffer(job);
    buf->setData(body);
    job->reply(mime, buf);
}
