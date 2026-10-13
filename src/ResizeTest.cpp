// --resize-test (CI): resize / move / maximize / restore the frameless window programmatically
// (Qt resize(), raw SetWindowPos, and a simulated live edge drag inside WM_ENTERSIZEMOVE) and
// check after every step that the Qt widgets and the web view cover exactly the native client
// area. A screenshot of the window is saved per step to <data dir>/resize-shots/. Exits 0 with
// "RESIZE OK" in startup.log, 5 with "RESIZE FAIL" on any mismatch.
#include "BrowserWindow.h"
#include "BrowserTab.h"
#include <QApplication>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QScreen>
#include <QWindow>
#include <QTimer>
#include <QDir>
#include <QPixmap>
#include <functional>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

void pacitumLog(const QString& line);

namespace {
QString r2s(const QRect& r) { return QStringLiteral("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()); }
}

void BrowserWindow::runResizeTest() {
    struct Step { QString name; std::function<void()> act; int waitMs; QSize expect; };
    struct State { QVector<Step> steps; int i = 0; int fails = 0; };
    auto st = std::make_shared<State>();
    const QString shotDir = QDir::currentPath() + QStringLiteral("/resize-shots");
    QDir(shotDir).removeRecursively(); QDir().mkpath(shotDir);

#ifdef Q_OS_WIN
    auto hwnd = [this]{ return reinterpret_cast<HWND>(internalWinId()); };
    auto swp = [hwnd](int x, int y, int w, int h){ SetWindowPos(hwnd(), nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE); };
#endif
    auto add = [st](const QString& n, std::function<void()> f, int wait = 700, QSize expect = {}){ st->steps.push_back({n, std::move(f), wait, expect}); };

    add(QStringLiteral("initial"), []{});
    add(QStringLiteral("qt-resize-900x600"), [this]{ showNormal(); resize(900, 600); });
    add(QStringLiteral("qt-resize-1300x850"), [this]{ resize(1300, 850); });
    add(QStringLiteral("qt-setGeometry-move"), [this]{ setGeometry(140, 90, 1000, 700); });
    add(QStringLiteral("qt-resize-640x480"), [this]{ resize(640, 480); });
#ifdef Q_OS_WIN
    add(QStringLiteral("swp-1100x760"), [swp]{ swp(60, 40, 1100, 760); });
    add(QStringLiteral("swp-800x560"), [swp]{ swp(200, 150, 800, 560); });
    add(QStringLiteral("swp-1400x900"), [swp]{ swp(10, 10, 1400, 900); });
    // simulated live edge drag: size-move loop with many small steps (left edge + bottom edge)
    add(QStringLiteral("drag-enter"), [hwnd]{ SendMessageW(hwnd(), WM_ENTERSIZEMOVE, 0, 0); }, 50);
    for (int k = 1; k <= 12; ++k)
        add(QStringLiteral("drag-%1").arg(k), [swp, k]{ swp(10 + k * 15, 10, 1400 - k * 15, 900 - k * 20); }, k == 12 ? 500 : 40);
    add(QStringLiteral("drag-exit"), [hwnd]{ SendMessageW(hwnd(), WM_EXITSIZEMOVE, 0, 0); });
    add(QStringLiteral("sw-maximize"), [hwnd]{ ShowWindow(hwnd(), SW_MAXIMIZE); }, 900);
    add(QStringLiteral("sw-restore"), [hwnd]{ ShowWindow(hwnd(), SW_RESTORE); }, 900);
    // Aero Snap / Win+arrow style: maximize and restore happen *inside* a move loop that
    // started while maximized. The size changes must go through (1.5.2 pinned them).
    add(QStringLiteral("loop-maximize"), [hwnd]{ ShowWindow(hwnd(), SW_MAXIMIZE); }, 900);
    add(QStringLiteral("loop-enter-while-max"), [hwnd]{ SendMessageW(hwnd(), WM_ENTERSIZEMOVE, 0, 0); }, 50);
    add(QStringLiteral("loop-restore"), [hwnd]{ ShowWindow(hwnd(), SW_RESTORE); }, 700);
    add(QStringLiteral("loop-swp-860x580"), [swp]{ swp(100, 80, 860, 580); }, 700, QSize(860, 580));
    add(QStringLiteral("loop-exit"), [hwnd]{ SendMessageW(hwnd(), WM_EXITSIZEMOVE, 0, 0); }, 500, QSize(860, 580));
    add(QStringLiteral("swp-resize-after-loop"), [swp]{ swp(120, 60, 900, 640); }, 700, QSize(900, 640));
#endif
    add(QStringLiteral("qt-maximize"), [this]{ showMaximized(); }, 900);
    add(QStringLiteral("qt-restore"), [this]{ showNormal(); }, 900);
    add(QStringLiteral("qt-maximize-2"), [this]{ showMaximized(); }, 900);
    add(QStringLiteral("qt-restore-2"), [this]{ showNormal(); }, 900);
    add(QStringLiteral("after-restore-resize-1000x720"), [this]{ resize(1000, 720); });

    auto check = [this, st, shotDir](const QString& name) {
        QStringList problems;
        const qreal dpr = windowHandle() ? windowHandle()->devicePixelRatio() : 1.0;
        QString native;
#ifdef Q_OS_WIN
        HWND h = reinterpret_cast<HWND>(internalWinId());
        RECT wr{}, cr{}; GetWindowRect(h, &wr); GetClientRect(h, &cr);
        POINT org{0, 0}; ClientToScreen(h, &org);
        const QRect clientPhys(org.x, org.y, cr.right, cr.bottom);
        native = QStringLiteral(" win=%1,%2 %3x%4 client=%5 zoomed=%6")
                     .arg(wr.left).arg(wr.top).arg(wr.right - wr.left).arg(wr.bottom - wr.top).arg(r2s(clientPhys)).arg(bool(IsZoomed(h)));
        // Qt's idea of the client area must equal the real one (in physical pixels)
        const QRect qtPhys(qRound(geometry().x() * dpr), qRound(geometry().y() * dpr), qRound(width() * dpr), qRound(height() * dpr));
        if (qAbs(qtPhys.width() - clientPhys.width()) > 2 || qAbs(qtPhys.height() - clientPhys.height()) > 2)
            problems << QStringLiteral("Qt size %1 != client %2").arg(r2s(qtPhys), r2s(clientPhys));
        if (qAbs(qtPhys.x() - clientPhys.x()) > 2 || qAbs(qtPhys.y() - clientPhys.y()) > 2)
            problems << QStringLiteral("Qt origin %1 != client origin %2").arg(r2s(qtPhys), r2s(clientPhys));
        if (!IsZoomed(h) && (cr.right != wr.right - wr.left || cr.bottom != wr.bottom - wr.top))
            problems << QStringLiteral("restored window has a non-client frame");
        if (const QSize ex = st->steps[st->i - 1].expect; ex.isValid() && QSize(wr.right - wr.left, wr.bottom - wr.top) != ex)
            problems << QStringLiteral("window size %1x%2, expected %3x%4 (size change was blocked)").arg(wr.right - wr.left).arg(wr.bottom - wr.top).arg(ex.width()).arg(ex.height());
        if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_LAYERED) problems << QStringLiteral("window is WS_EX_LAYERED");
        if (windowOpacity() < 0.999) problems << QStringLiteral("windowOpacity %1").arg(windowOpacity());
#endif
        const QMargins fm = windowHandle() ? windowHandle()->frameMargins() : QMargins();
        if (!isMaximized() && !fm.isNull())
            problems << QStringLiteral("Qt frame margins %1,%2,%3,%4 on a frameless window").arg(fm.left()).arg(fm.top()).arg(fm.right()).arg(fm.bottom());
        if (centralWidget() && centralWidget()->geometry().right() != rect().right())
            problems << QStringLiteral("central %1 vs window %2").arg(r2s(centralWidget()->geometry()), r2s(rect()));
        QString viewInfo;
        if (auto* t = activeTab(); t && t->view() && t->view()->isVisible()) {
            QWebEngineView* v = t->view();
            const QRect vr(v->mapTo(this, QPoint(0, 0)), v->size());
            viewInfo = QStringLiteral(" view=%1").arg(r2s(vr));
            if (qAbs(vr.right() - rect().right()) > 1) problems << QStringLiteral("web view right %1 != window right %2").arg(vr.right()).arg(rect().right());
            if (vr.bottom() > rect().bottom() + 1 || vr.bottom() < rect().bottom() - 40) problems << QStringLiteral("web view bottom %1 vs window bottom %2").arg(vr.bottom()).arg(rect().bottom());
            if (vr.left() < 0 || vr.top() < 0) problems << QStringLiteral("web view origin negative");
            // the native/compositor child that actually shows the page must follow the view
            if (QWidget* fp = v->focusProxy(); fp && fp != v && fp->size() != v->size())
                problems << QStringLiteral("web view delegate %1x%2 != view %3x%4").arg(fp->width()).arg(fp->height()).arg(v->width()).arg(v->height());
            // the page's own viewport (tests/resize-test.html in CI) must match the view
            const int stepNo = st->i; const QSize vs = v->size();
            v->page()->runJavaScript(QStringLiteral("innerWidth+'x'+innerHeight+' '+location.pathname.split('/').pop()"),
                [st, stepNo, vs](const QVariant& r){
                    const QStringList p = r.toString().split(QLatin1Char(' ')).value(0).split(QLatin1Char('x'));
                    const int jw = p.value(0).toInt(), jh = p.value(1).toInt();
                    const bool bad = jw > 0 && (qAbs(jw - vs.width()) > 2 || qAbs(jh - vs.height()) > 2);
                    pacitumLog(QStringLiteral("RESIZE js step %1: page viewport %2 vs view %3x%4 %5").arg(stepNo).arg(r.toString()).arg(vs.width()).arg(vs.height())
                                   .arg(bad ? QStringLiteral("PROBLEM") : QStringLiteral("ok")));
                    if (bad) st->fails++;
                });
        }
        for (QWidget* c : findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
            if (c->isVisible() && c->autoFillBackground() && c->testAttribute(Qt::WA_TransparentForMouseEvents) && c->geometry() != rect())
                problems << QStringLiteral("overlay %1 not covering window").arg(r2s(c->geometry()));
        // screenshot of the window area as it is on screen
        if (QScreen* s = screen()) {
            const QPixmap px = s->grabWindow(0, frameGeometry().x(), frameGeometry().y(), frameGeometry().width(), frameGeometry().height());
            px.save(QStringLiteral("%1/%2-%3.png").arg(shotDir).arg(st->i, 2, 10, QLatin1Char('0')).arg(name));
        }
        pacitumLog(QStringLiteral("RESIZE step %1 %2: qt=%3 frame=%4 dpr=%5 max=%6%7%8 %9")
                       .arg(st->i).arg(name, r2s(geometry()), r2s(frameGeometry())).arg(dpr).arg(isMaximized())
                       .arg(native, viewInfo, problems.isEmpty() ? QStringLiteral("ok") : QStringLiteral("PROBLEM: ") + problems.join(QStringLiteral("; "))));
        if (!problems.isEmpty()) st->fails++;
    };

    auto runNext = std::make_shared<std::function<void()>>();
    *runNext = [this, st, check, runNext]{
        if (st->i > 0) check(st->steps[st->i - 1].name);
        if (st->i >= st->steps.size()) {
            QTimer::singleShot(600, qApp, [st]{   // let the last page-viewport check report
                pacitumLog(st->fails ? QStringLiteral("RESIZE FAIL (%1 checks with problems)").arg(st->fails) : QStringLiteral("RESIZE OK"));
                QCoreApplication::exit(st->fails ? 5 : 0);
            });
            return;
        }
        const Step& s = st->steps[st->i++];
        s.act();
        QTimer::singleShot(s.waitMs, this, [runNext]{ (*runNext)(); });
    };
    (*runNext)();
}
