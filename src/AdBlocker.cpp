#include "AdBlocker.h"

const QSet<QString> AdBlocker::s_hosts = {
    QStringLiteral("doubleclick.net"), QStringLiteral("googlesyndication.com"),
    QStringLiteral("googleadservices.com"), QStringLiteral("google-analytics.com"),
    QStringLiteral("googletagmanager.com"), QStringLiteral("adservice.google.com"),
    QStringLiteral("adsrvr.org"), QStringLiteral("adnxs.com"), QStringLiteral("rubiconproject.com"),
    QStringLiteral("criteo.com"), QStringLiteral("criteo.net"), QStringLiteral("pubmatic.com"),
    QStringLiteral("openx.net"), QStringLiteral("amazon-adsystem.com"), QStringLiteral("scorecardresearch.com"),
    QStringLiteral("facebook.com/tr"), QStringLiteral("connect.facebook.net"), QStringLiteral("bat.bing.com"),
    QStringLiteral("hotjar.com"), QStringLiteral("mouseflow.com"), QStringLiteral("taboola.com"),
    QStringLiteral("outbrain.com"), QStringLiteral("moatads.com")
};

const QStringList AdBlocker::s_patterns = {
    QStringLiteral("/ads/"), QStringLiteral("/ad/"), QStringLiteral("/ad-"),
    QStringLiteral("/banner"), QStringLiteral("/tracking"), QStringLiteral("/pixel"),
    QStringLiteral("pagead"), QStringLiteral("doubleclick"), QStringLiteral("advertisement"),
    QStringLiteral("google_ad"), QStringLiteral("sponsored"), QStringLiteral("popunder")
};

AdBlocker::AdBlocker(bool enabled, QObject* parent) : QWebEngineUrlRequestInterceptor(parent), m_enabled(enabled) {}

void AdBlocker::interceptRequest(QWebEngineUrlRequestInfo &info) {
    if (!m_enabled) return;
    const QString host = info.requestUrl().host().toLower();
    const QString url  = info.requestUrl().toString().toLower();
    if (host.isEmpty()) return;
    if (s_hosts.contains(host)) { info.block(true); return; }
    for (auto &p : s_patterns) if (url.contains(p)) { info.block(true); return; }
}
