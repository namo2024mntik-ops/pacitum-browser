// --permissions-test (CI, 1.7.1): exercises the site-permission plumbing end to end on a local
// HTTP server: store scoping/persistence, the JS shims + QWebChannel bridge (origin-scoped device
// grants, gesture gating), pop-up blocking in createWindow, multiple-download throttling in
// downloadRequested, and Qt permission requests answered through the Pacitum queue.
#include "BrowserWindow.h"
#include "BrowserTab.h"
#include "PermissionController.h"
#include "SitePermissions.h"
#include <QApplication>
#include <QScreen>
#include <QGuiApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QEventLoop>
#include <QTimer>
#include <QElapsedTimer>
#include <QWebEngineView>
#include <QWebEngineProfile>

void pacitumLog(const QString& line);

namespace {
struct Http : QTcpServer {
    Http() {
        connect(this, &QTcpServer::newConnection, this, [this]{
            while (auto* s = nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [s]{
                    const QByteArray req = s->readAll();
                    const QByteArray path = req.split(' ').value(1);
                    QByteArray body, type = "text/html", extra;
                    if (path.startsWith("/file")) { body = "pacitum download test\n"; type = "text/plain"; extra = "Content-Disposition: attachment; filename=\"" + path.mid(1, 6) + ".txt\"\r\n"; }
                    else if (path.startsWith("/popup")) body = "<!doctype html><title>popup</title><script>setTimeout(function(){window.open('/target-popup');},300);</script>";
                    else if (path.startsWith("/dl")) body = "<!doctype html><title>dl</title><script>addEventListener('load',function(){['/file1','/file2','/file3'].forEach(function(u,i){setTimeout(function(){var a=document.createElement('a');a.href=u;a.download='';document.body.appendChild(a);a.click();},200+i*150);});});</script><body></body>";
                    else body = "<!doctype html><title>perm</title><body>pacitum permissions test</body>";
                    s->write("HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nCache-Control: no-store\r\nConnection: close\r\n" + extra + "Content-Length: "
                             + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    s->flush(); s->disconnectFromHost();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }
};

void wait(int ms) { QEventLoop l; QTimer::singleShot(ms, &l, &QEventLoop::quit); l.exec(); }

BrowserTab* openAndWait(BrowserWindow* w, const QString& url) {
    w->openUrlInNewTab(url);
    BrowserTab* tab = nullptr;
    for (auto* t : w->findChildren<BrowserTab*>()) if (t->currentUrl() == url || t->view()->url().toString() == url) tab = t;
    if (!tab) { wait(300); for (auto* t : w->findChildren<BrowserTab*>()) if (t->view()->url().toString() == url) tab = t; }
    if (!tab) return nullptr;
    QEventLoop l; bool done = false;
    QObject::connect(tab->view(), &QWebEngineView::loadFinished, &l, [&](bool){ done = true; l.quit(); });
    QTimer::singleShot(20000, &l, &QEventLoop::quit);
    if (!tab->isLoading() && tab->view()->url().toString() == url) done = true; else l.exec();
    wait(300);
    return tab;
}

QVariant evalPoll(BrowserTab* t, const QString& expr, int timeoutMs = 8000) {
    QElapsedTimer et; et.start();
    QVariant out;
    while (et.elapsed() < timeoutMs) {
        QEventLoop l; bool got = false;
        t->view()->page()->runJavaScript(expr, [&](const QVariant& v){ out = v; got = true; l.quit(); });
        QTimer::singleShot(2000, &l, &QEventLoop::quit);
        if (!got) l.exec();
        if (out.isValid() && !out.isNull() && out.toString() != QLatin1String("undefined")) return out;
        wait(150);
    }
    return out;
}
}

void runPermissionTest(BrowserWindow* w) {
    int fails = 0;
    auto check = [&](bool ok, const QString& what){ pacitumLog(QStringLiteral("  %1 %2").arg(ok ? QStringLiteral("ok  ") : QStringLiteral("FAIL"), what)); if (!ok) ++fails; };
    pacitumLog(QStringLiteral("PERMISSIONS test starting (Qt %1)").arg(QLatin1String(qVersion())));
    QStringList log;
    const int storeFails = Perm::runStoreSelfTest(&log);
    for (const QString& l : log) pacitumLog(l);
    check(storeFails == 0, QStringLiteral("store self-test"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    check(true, QStringLiteral("QWebEnginePage::desktopMediaRequested available (screen-share picker uses it)"));
#else
    check(false, QStringLiteral("desktop media API missing"));
#endif
    Http http;
    check(http.listen(QHostAddress::LocalHost, 0), QStringLiteral("local test server"));
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(http.serverPort());
    const QString origin = base;
    PermissionController::s_testAutoAnswer = true;

    // 1) Shims present, bridge round-trip scoped to the origin, gesture gating
    {
        BrowserTab* t = openAndWait(w, base + QStringLiteral("/shim"));
        check(t && t->permissions(), QStringLiteral("tab with permission controller"));
        if (t && t->permissions()) {
            auto* st = t->permissions()->store();
            st->grantDevice(origin, Perm::Kind::Usb, QJsonObject{{QStringLiteral("id"), QStringLiteral("usb:test")}, {QStringLiteral("name"), QStringLiteral("Test board")}, {QStringLiteral("vendorId"), 0x1234}, {QStringLiteral("productId"), 0x5678}});
            st->grantDevice(QStringLiteral("https://other.example"), Perm::Kind::Usb, QJsonObject{{QStringLiteral("id"), QStringLiteral("usb:other")}});
            const QString types = evalPoll(t, QStringLiteral("[typeof navigator.usb.requestDevice, typeof navigator.serial.requestPort, typeof navigator.hid.requestDevice, typeof navigator.bluetooth.requestDevice].join(',')")).toString();
            check(types == QLatin1String("function,function,function,function"), QStringLiteral("navigator.usb/serial/hid/bluetooth shims installed (%1)").arg(types));
            t->view()->page()->runJavaScript(QStringLiteral("navigator.usb.getDevices().then(function(a){window.__devs=JSON.stringify(a.map(function(d){return d.productName+':'+d.vendorId}))},function(e){window.__devs='ERR '+e.name});"
                                                            "navigator.usb.requestDevice({filters:[]}).then(function(){window.__req='granted?!'},function(e){window.__req=e.name});"));
            const QString devs = evalPoll(t, QStringLiteral("window.__devs")).toString();
            check(devs == QStringLiteral("[\"Test board:4660\"]"), QStringLiteral("bridge returns only this origin's granted devices (%1)").arg(devs));
            const QString req = evalPoll(t, QStringLiteral("window.__req")).toString();
            check(req == QLatin1String("SecurityError"), QStringLiteral("requestDevice without a user gesture is refused (%1)").arg(req));
            t->view()->page()->runJavaScript(QStringLiteral("navigator.storage.persisted().then(function(v){window.__pers=String(v)},function(e){window.__pers='ERR '+e.name});"));
            check(!evalPoll(t, QStringLiteral("window.__pers")).toString().startsWith(QLatin1String("ERR")), QStringLiteral("storage.persisted() via bridge"));
            // Notifications request answered through the queue (auto "deny" = Block stored for this origin only)
            PermissionController::s_testAnswer = QStringLiteral("deny");
            t->view()->page()->runJavaScript(QStringLiteral("Notification.requestPermission().then(function(r){window.__notif=r});"));
            const QString notif = evalPoll(t, QStringLiteral("window.__notif")).toString();
            check(notif == QLatin1String("denied"), QStringLiteral("notification request denied through Pacitum queue (%1)").arg(notif));
            check(st->decision(origin, Perm::Kind::Notifications) == Perm::Decision::Block, QStringLiteral("deny stored per origin"));
            check(st->decision(QStringLiteral("http://localhost:%1").arg(http.serverPort()), Perm::Kind::Notifications) == Perm::Decision::Ask, QStringLiteral("other origin unaffected"));
            st->setDecision(origin, Perm::Kind::Notifications, Perm::Decision::Ask);
            st->revokeDevices(origin, Perm::Kind::Usb);
        }
    }
    // 2) Pop-up without a user gesture is blocked and recorded; "Always allow" lets it through
    {
        const int before = w->findChildren<BrowserTab*>().size();
        BrowserTab* t = openAndWait(w, base + QStringLiteral("/popup"));
        wait(1500);
        const int after = w->findChildren<BrowserTab*>().size();
        const QStringList blocked = t && t->permissions() ? t->permissions()->blockedPopupUrls() : QStringList();
        check(after == before + 1, QStringLiteral("no new tab for a gesture-less pop-up (%1 → %2 tabs)").arg(before).arg(after));
        check(!blocked.isEmpty() && blocked.first().endsWith(QLatin1String("/target-popup")), QStringLiteral("blocked pop-up URL recorded (%1)").arg(blocked.join(QLatin1Char(','))));
        if (t && t->permissions()) {
            t->permissions()->store()->setDecision(origin, Perm::Kind::Popups, Perm::Decision::Allow);
            t->reload(); wait(2500);
            check(w->findChildren<BrowserTab*>().size() > after, QStringLiteral("pop-up allowed after per-origin Always allow"));
            t->permissions()->store()->setDecision(origin, Perm::Kind::Popups, Perm::Decision::Ask);
        }
    }
    // 3) Multiple automatic downloads: the 2nd within 5 s is held and prompts; auto "deny" blocks
    {
        PermissionController::s_testAnswer = QStringLiteral("deny");
        BrowserTab* t = openAndWait(w, base + QStringLiteral("/dl"));
        wait(2500);
        auto* st = t && t->permissions() ? t->permissions()->store() : nullptr;
        check(st && st->decision(origin, Perm::Kind::Downloads) == Perm::Decision::Block, QStringLiteral("extra automatic downloads prompted and blocked"));
        if (st) st->setDecision(origin, Perm::Kind::Downloads, Perm::Decision::Ask);
    }
    PermissionController::s_testAutoAnswer = false;
    if (qEnvironmentVariableIsSet("PACITUM_PERM_SHOTS")) {   // local visual check: bubble + site controls screenshots
        const QString dir = qEnvironmentVariable("PACITUM_PERM_SHOTS");
        BrowserTab* t = openAndWait(w, base + QStringLiteral("/shots"));
        if (t && t->permissions()) {
            t->permissions()->store()->setDecision(origin, Perm::Kind::Camera, Perm::Decision::Allow);
            t->permissions()->store()->setDecision(origin, Perm::Kind::Notifications, Perm::Decision::Block);
            t->view()->page()->runJavaScript(QStringLiteral("navigator.requestMIDIAccess({sysex:true}).catch(function(){})"));
            wait(1000);
            t->permissions()->chipClicked(QStringLiteral("request"), false);   // no WM focus under Xvfb: open from the chip
            wait(1000);
            QGuiApplication::primaryScreen()->grabWindow(0).save(dir + QStringLiteral("/bubble.png"));
            t->permissions()->collapse(); wait(400);
            QGuiApplication::primaryScreen()->grabWindow(0).save(dir + QStringLiteral("/chip.png"));
            t->permissions()->showSiteControls(QPoint(w->geometry().x() + 120, w->geometry().y() + 100));
            wait(800);
            QGuiApplication::primaryScreen()->grabWindow(0).save(dir + QStringLiteral("/sitecontrols.png"));
            for (auto* tl : QApplication::topLevelWidgets()) if (tl->inherits("PermUi::SiteControls")) tl->close();
            t->permissions()->siteSettingsRequested(origin);
            wait(1200);
            QGuiApplication::primaryScreen()->grabWindow(0).save(dir + QStringLiteral("/settings.png"));
        }
    }
    pacitumLog(QStringLiteral("PERMISSIONS %1 (%2 failures)").arg(fails ? QStringLiteral("FAIL") : QStringLiteral("OK")).arg(fails));
    QCoreApplication::exit(fails ? 8 : 0);
}
