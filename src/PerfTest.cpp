// --perf-test (CI): measures memory and tab switching so releases can quote before/after numbers.
// Opens N offline tabs (data: pages, some animating), then logs:
//   PERF rss idle / with N tabs (main + all QtWebEngine child processes, MB, process count)
//   PERF tab switch (synchronous cost of switching + repainting the chrome, median / max ms)
//   PERF minimized cpu (CPU ms used by all processes during 5 s minimized with an animating tab)
// and exits 0 with "PERF OK". N defaults to 12 (PACITUM_PERF_TABS overrides).
#include "BrowserWindow.h"
#include "BrowserTab.h"
#include "TabWidget.h"
#include <QApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <memory>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#else
#include <unistd.h>
#endif

void pacitumLog(const QString& line);

namespace {
struct ProcStats { double rssMb = 0; double cpuMs = 0; int procs = 0; };

// The browser process and every descendant (QtWebEngineProcess: GPU, network, renderers).
ProcStats sampleTree() {
    ProcStats r;
#ifdef Q_OS_WIN
    QList<DWORD> tree{GetCurrentProcessId()};
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        QList<QPair<DWORD, DWORD>> all;
        PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) do { all.push_back({pe.th32ProcessID, pe.th32ParentProcessID}); } while (Process32NextW(snap, &pe));
        CloseHandle(snap);
        for (int i = 0; i < tree.size(); ++i) for (const auto& p : all) if (p.second == tree[i] && !tree.contains(p.first)) tree.push_back(p.first);
    }
    for (DWORD pid : tree) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!h) continue;
        PROCESS_MEMORY_COUNTERS pmc{}; pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) r.rssMb += pmc.WorkingSetSize / 1048576.0;
        FILETIME c, e, k, u;
        if (GetProcessTimes(h, &c, &e, &k, &u)) {
            auto ms = [](FILETIME f){ return ((quint64(f.dwHighDateTime) << 32) | f.dwLowDateTime) / 10000.0; };
            r.cpuMs += ms(k) + ms(u);
        }
        ++r.procs; CloseHandle(h);
    }
#else
    QList<int> tree{int(getpid())};
    QList<QPair<int, int>> all;
    for (const QString& d : QDir(QStringLiteral("/proc")).entryList(QDir::Dirs)) {
        bool ok = false; const int pid = d.toInt(&ok); if (!ok) continue;
        QFile f(QStringLiteral("/proc/%1/stat").arg(pid)); if (!f.open(QIODevice::ReadOnly)) continue;
        const QByteArray s = f.readAll(); const int rp = s.lastIndexOf(')');
        const QList<QByteArray> fl = s.mid(rp + 2).split(' ');
        if (fl.size() > 1) all.push_back({pid, fl[1].toInt()});
    }
    for (int i = 0; i < tree.size(); ++i) for (const auto& p : all) if (p.second == tree[i] && !tree.contains(p.first)) tree.push_back(p.first);
    const double tick = 1000.0 / sysconf(_SC_CLK_TCK);
    for (int pid : tree) {
        QFile st(QStringLiteral("/proc/%1/status").arg(pid));
        if (st.open(QIODevice::ReadOnly)) for (const QByteArray& l : st.readAll().split('\n'))
            if (l.startsWith("VmRSS:")) r.rssMb += l.mid(6).trimmed().split(' ').value(0).toDouble() / 1024.0;
        QFile s(QStringLiteral("/proc/%1/stat").arg(pid));
        if (s.open(QIODevice::ReadOnly)) {
            const QByteArray b = s.readAll(); const QList<QByteArray> fl = b.mid(b.lastIndexOf(')') + 2).split(' ');
            if (fl.size() > 12) r.cpuMs += (fl[11].toDouble() + fl[12].toDouble()) * tick;
        }
        ++r.procs;
    }
#endif
    return r;
}
}

void BrowserWindow::runPerfTest() {
    struct St { QList<double> sw; ProcStats idle; ProcStats loaded; double cpuMin = 0, cpuNormal = 0; };
    auto st = std::make_shared<St>();
    const int n = qEnvironmentVariableIsSet("PACITUM_PERF_TABS") ? qEnvironmentVariableIntValue("PACITUM_PERF_TABS") : 12;
    st->idle = sampleTree();
    pacitumLog(QStringLiteral("PERF rss idle: %1 MB in %2 processes").arg(st->idle.rssMb, 0, 'f', 1).arg(st->idle.procs));
    for (int i = 0; i < n; ++i) {
        QString html = QStringLiteral("data:text/html,<title>perf %1</title><body style='font:14px sans-serif'>").arg(i);
        for (int k = 0; k < 300; ++k) html += QStringLiteral("<p>row %1 lorem ipsum dolor sit amet</p>").arg(k);
        if (i % 3 == 0) html += QStringLiteral("<canvas id=c width=300 height=200></canvas><script>let n=0;(function f(){const g=c.getContext('2d');"
                                               "g.fillStyle='hsl('+(n++%360)+',60%,50%)';g.fillRect(0,0,300,200);requestAnimationFrame(f)})();setInterval(()=>{for(let j=0;j<2e5;j++);},50)</script>");
        newTab(html);
    }
    QTimer::singleShot(8000, this, [this, st, n]{
        st->loaded = sampleTree();
        pacitumLog(QStringLiteral("PERF rss with %1 tabs: %2 MB in %3 processes").arg(n + 1).arg(st->loaded.rssMb, 0, 'f', 1).arg(st->loaded.procs));
        auto* t = new QTimer(this); t->setInterval(120);
        auto k = std::make_shared<int>(0);
        connect(t, &QTimer::timeout, this, [this, st, t, k]{
            const int i = (*k * 5 + 1) % m_tabs->count();
            QElapsedTimer et; et.start();
            m_tabs->setCurrentIndex(i);
            repaint();   // the chrome (tab strip, address bar) repainted synchronously
            st->sw << et.nsecsElapsed() / 1e6;
            if (++*k < 40) return;
            t->stop(); t->deleteLater();
            auto v = st->sw; std::sort(v.begin(), v.end());
            pacitumLog(QStringLiteral("PERF tab switch: median %1 ms, max %2 ms over %3 switches").arg(v[v.size() / 2], 0, 'f', 2).arg(v.last(), 0, 'f', 2).arg(v.size()));
            // CPU while the window is visible vs minimized, with an animating tab active.
            m_tabs->setCurrentIndex(1);
            QTimer::singleShot(1500, this, [this, st]{
                const double c0 = sampleTree().cpuMs;
                QTimer::singleShot(5000, this, [this, st, c0]{
                    st->cpuNormal = sampleTree().cpuMs - c0;
                    showMinimized();
                    QTimer::singleShot(1500, this, [this, st]{
                        const double c1 = sampleTree().cpuMs;
                        QTimer::singleShot(5000, this, [this, st, c1]{
                            st->cpuMin = sampleTree().cpuMs - c1;
                            pacitumLog(QStringLiteral("PERF cpu over 5 s: visible %1 ms, minimized %2 ms").arg(st->cpuNormal, 0, 'f', 0).arg(st->cpuMin, 0, 'f', 0));
                            const auto after = sampleTree();
                            pacitumLog(QStringLiteral("PERF rss minimized: %1 MB in %2 processes").arg(after.rssMb, 0, 'f', 1).arg(after.procs));
                            showNormal();
                            QTimer::singleShot(1000, this, []{ pacitumLog(QStringLiteral("PERF OK")); QCoreApplication::exit(0); });
                        });
                    });
                });
            });
        });
        t->start();
    });
}
