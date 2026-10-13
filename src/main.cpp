/*
 * Pacitum Browser — Qt6 WebEngine browser
 * Entry point with GPU detection and Chromium flags (mirrors main.py:31)
 */

#include <QApplication>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QIcon>
#include <QFont>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QtGlobal>
#include "BrowserWindow.h"
#include "PacitumScheme.h"
#include "WebProfile.h"
#include <QJsonDocument>
#include <QJsonObject>

#include <iostream>
#include <QElapsedTimer>
#include <QDateTime>
#include <QMutex>
#include <QTimer>
#include <QUrl>
#include <QTextStream>
#include <QWindow>
#include <exception>
#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// ---------------- startup / crash logging ----------------
// startup.log: every Qt message of the last run, plus timings. crash.log: unhandled exceptions/terminate.
static QString g_logDir;
static FILE* g_log = nullptr;
static QElapsedTimer g_clock;

static QString logDir() {
    if (!g_logDir.isEmpty()) return g_logDir;
#ifdef Q_OS_WIN
    wchar_t buf[MAX_PATH]; DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    QString base = n ? QString::fromWCharArray(buf, int(n)) : QDir::tempPath();
#else
    QString base = QDir::homePath() + QStringLiteral("/.local/share");
#endif
    g_logDir = base + QStringLiteral("/Pacitum/Pacitum Browser");
    QDir().mkpath(g_logDir);
    return g_logDir;
}

void pacitumLog(const QString& line) {
    if (!g_log) return;
    static QMutex mx; QMutexLocker lock(&mx);
    const QByteArray b = QStringLiteral("[%1 ms] %2\n").arg(g_clock.isValid() ? g_clock.elapsed() : 0, 6).arg(line).toUtf8();
    std::fwrite(b.constData(), 1, size_t(b.size()), g_log); std::fflush(g_log);
}

static void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    static const char* names[] = {"debug", "warning", "critical", "fatal", "info"};
    pacitumLog(QStringLiteral("%1: %2%3").arg(QLatin1String(names[qBound(0, int(type), 4)]), msg,
               ctx.file ? QStringLiteral(" (%1:%2)").arg(QLatin1String(ctx.file)).arg(ctx.line) : QString()));
    std::fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
}

static void writeCrash(const char* what) {
    const QString path = logDir() + QStringLiteral("/crash.log");
    if (FILE* f = std::fopen(path.toLocal8Bit().constData(), "a")) {
        std::fprintf(f, "%s Pacitum %s crashed after %lld ms: %s\n", QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8().constData(),
                     PACITUM_VERSION, g_clock.isValid() ? (long long)g_clock.elapsed() : 0LL, what);
        std::fclose(f);
    }
    pacitumLog(QStringLiteral("CRASH: %1").arg(QLatin1String(what)));
}

#ifdef Q_OS_WIN
static LONG WINAPI crashFilter(EXCEPTION_POINTERS* ep) {
    char buf[512];
    HMODULE mod = nullptr; char modName[MAX_PATH] = "?";
    void* addr = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;
    if (addr && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   static_cast<LPCSTR>(addr), &mod)) GetModuleFileNameA(mod, modName, MAX_PATH);
    std::snprintf(buf, sizeof(buf), "exception 0x%08lX at %p in %s (+0x%llx)",
                  ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0UL, addr, modName,
                  (unsigned long long)((char*)addr - (char*)mod));
    writeCrash(buf);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static void installLogging() {
    g_clock.start();
    const QString path = logDir() + QStringLiteral("/startup.log");
    g_log = std::fopen(path.toLocal8Bit().constData(), "w");
    qInstallMessageHandler(messageHandler);
    std::set_terminate([]{
        const char* what = "std::terminate";
        try { if (auto ep = std::current_exception()) std::rethrow_exception(ep); }
        catch (const std::exception& e) { writeCrash(e.what()); std::abort(); }
        catch (...) {}
        writeCrash(what); std::abort();
    });
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(crashFilter);
#endif
    pacitumLog(QStringLiteral("Pacitum %1 starting").arg(QLatin1String(PACITUM_VERSION)));
}

static QString detectGpu() {
#ifdef Q_OS_WIN
    // Simplified: query via WMI would need COM; return generic for flags
    return QStringLiteral("GPU detection: using desktop OpenGL");
#else
    return QStringLiteral("Unknown GPU");
#endif
}

static void setupEnvironment() {
    QString gpu = detectGpu();
    std::cout << "[Browser] " << gpu.toStdString() << std::endl;

    // Conservative, driver-safe Chromium flags. (Earlier builds disabled GPU driver-bug workarounds,
    // forced desktop OpenGL and passed --enable-features twice, which made Chromium drop the first list.)
    QByteArray flags = QByteArray("--enable-gpu-rasterization --enable-zero-copy");
#ifdef Q_OS_WIN
    flags += " --enable-features=OverlayScrollbar,ParallelDownloading --disable-features=DCompPresenter";
#else
    flags += " --enable-features=OverlayScrollbar,ParallelDownloading,VaapiVideoDecoder,VaapiVideoEncoder,WebRTCPipeWireCapturer";
#endif
    if (qEnvironmentVariableIsSet("QTWEBENGINE_CHROMIUM_FLAGS")) flags += ' ' + qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
}

int main(int argc, char *argv[]) {
    installLogging();
    bool smoke = false;
    for (int i = 1; i < argc; ++i) if (qstrcmp(argv[i], "--smoke-test") == 0) smoke = true;
    const QString launchDir = QDir::currentPath();   // before we switch to the data dir (relative file args)
    setupEnvironment();
    PacitumSchemeHandler::registerScheme();

    QApplication app(argc, argv);
    pacitumLog(QStringLiteral("QApplication ready"));
#ifdef Q_OS_WIN
    // Lets the installer (AppMutex) detect a running instance before upgrading.
    CreateMutexW(nullptr, FALSE, L"PacitumBrowserRunning");
#endif
    app.setApplicationName(QStringLiteral("Pacitum Browser"));
    app.setOrganizationName(QStringLiteral("Pacitum"));
    app.setApplicationDisplayName(QStringLiteral("Pacitum Browser"));
    app.setApplicationVersion(QStringLiteral(PACITUM_VERSION));
    app.setDesktopFileName(QStringLiteral("pacitum-browser"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/pacitum-icon-256.png")));
    {
        QFont f = app.font();
        f.setFamilies({QStringLiteral("Inter"), QStringLiteral("Segoe UI"), QStringLiteral("Cantarell"), f.family()});
        f.setPixelSize(13);
        app.setFont(f);
    }

    // User data lives in %LOCALAPPDATA%/Pacitum/Pacitum Browser (survives upgrades/reinstalls).
    {
        const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QDir().mkpath(dataDir);
        const QString oldDir = QCoreApplication::applicationDirPath();   // 1.0/1.1 kept data next to the exe
        for (const char* n : {"browser_settings.json", "bookmarks.json", "browser_session.json", "browser_history.db"}) {
            const QString src = oldDir + QLatin1Char('/') + QLatin1String(n), dst = dataDir + QLatin1Char('/') + QLatin1String(n);
            if (QFile::exists(src) && !QFile::exists(dst)) QFile::copy(src, dst);
        }
        QDir::setCurrent(dataDir);
    }
    {   // web prefs from settings before any page is created
        QFile f(QStringLiteral("browser_settings.json"));
        if (f.open(QIODevice::ReadOnly)) {
            const auto o = QJsonDocument::fromJson(f.readAll()).object();
            Pacitum::webPrefs().javascript = o.value(QStringLiteral("javascript_enabled")).toBool(true);
            Pacitum::webPrefs().popups = o.value(QStringLiteral("popups_allowed")).toBool(false);
        }
    }

    // Persistent profile storage (mirrors main.py:82)
    auto* profile = Pacitum::mainProfile();
    QString storagePath = QDir::currentPath() + QStringLiteral("/browser_data");  // persistent storage incl. cookies
    QDir().mkpath(storagePath + QStringLiteral("/storage"));
    QDir().mkpath(storagePath + QStringLiteral("/cache"));
    profile->setPersistentStoragePath(storagePath + QStringLiteral("/storage"));
    profile->setCachePath(storagePath + QStringLiteral("/cache"));

    auto* settings = profile->settings();
    settings->setAttribute(QWebEngineSettings::AutoLoadIconsForPage, true);
    settings->setAttribute(QWebEngineSettings::TouchIconsEnabled, true);
    settings->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    settings->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, false);
    settings->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, false);
    settings->setAttribute(QWebEngineSettings::HyperlinkAuditingEnabled, false);
    profile->setSpellCheckEnabled(true);
    profile->setSpellCheckLanguages({QStringLiteral("en-US")});
    Pacitum::configureProfile(profile, true);
    Pacitum::configureProfile(QWebEngineProfile::defaultProfile(), false);
    std::cout << "[Browser] profile off-the-record: " << profile->isOffTheRecord() << " storage: " << profile->persistentStoragePath().toStdString() << std::endl;

    pacitumLog(QStringLiteral("profile configured"));
    try {
        BrowserWindow w;
        pacitumLog(QStringLiteral("window constructed"));
        // URLs / files passed on the command line (default-browser handler: "PacitumBrowser.exe" "%1").
        for (const QString& a : app.arguments().mid(1)) {
            if (a.startsWith(QLatin1Char('-')) || a.isEmpty()) continue;
            const QUrl u = QUrl::fromUserInput(a, launchDir, QUrl::AssumeLocalFile);
            if (u.isValid()) w.openUrlInNewTab(u.toString());
        }
        w.show();
        pacitumLog(QStringLiteral("window shown: visible=%1 geometry=%2,%3 %4x%5").arg(w.isVisible())
                   .arg(w.geometry().x()).arg(w.geometry().y()).arg(w.width()).arg(w.height()));
        QTimer::singleShot(0, &app, []{ pacitumLog(QStringLiteral("event loop running (startup time)")); });
        if (smoke) {
            // CI smoke test: window must exist and be visible; quit cleanly after a few seconds.
            QTimer::singleShot(8000, &app, [&w]{
                const bool ok = w.isVisible() && w.windowHandle() && w.windowHandle()->isExposed() && w.width() > 200 && w.height() > 200;
                pacitumLog(QStringLiteral("SMOKE %1 visible=%2 exposed=%3 size=%4x%5").arg(ok ? QStringLiteral("OK") : QStringLiteral("FAIL"))
                           .arg(w.isVisible()).arg(w.windowHandle() && w.windowHandle()->isExposed()).arg(w.width()).arg(w.height()));
                QCoreApplication::exit(ok ? 0 : 3);
            });
        }
        const int rc = app.exec();
        pacitumLog(QStringLiteral("exit %1").arg(rc));
        return rc;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
