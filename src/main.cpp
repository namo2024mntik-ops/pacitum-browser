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

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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

    qputenv("QT_OPENGL", QByteArray("desktop"));
    QByteArray flags = QByteArray(
        "--enable-features=VaapiVideoDecoder,VaapiVideoEncoder,OverlayScrollbar,ParallelDownloading,"
        "EnableIncomingRTCPDrops,WebRTCPipeWireCapturer"
        " --disable-gpu-driver-bug-workarounds"
        " --enable-zero-copy"
        " --enable-gpu-rasterization"
        " --enable-unsafe-swiftshader"
    );
#ifdef Q_OS_WIN
    flags += QByteArray(" --disable-features=DCompPresenter --enable-features=UseSkiaRenderer");
#endif
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
}

int main(int argc, char *argv[]) {
    setupEnvironment();
    PacitumSchemeHandler::registerScheme();

    QApplication app(argc, argv);
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

    try {
        BrowserWindow w;
        w.show();
        return app.exec();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
