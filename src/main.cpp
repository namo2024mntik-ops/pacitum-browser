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
    // Ensure cwd is executable dir (mirrors os.chdir in main.py:12)
    QDir::setCurrent(QCoreApplication::applicationDirPath());

    setupEnvironment();
    PacitumSchemeHandler::registerScheme();

    QApplication app(argc, argv);
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

    // Persistent profile storage (mirrors main.py:82)
    auto* profile = QWebEngineProfile::defaultProfile();
    QString storagePath = QDir::currentPath() + QStringLiteral("/browser_data");
    QDir().mkpath(storagePath + QStringLiteral("/storage"));
    QDir().mkpath(storagePath + QStringLiteral("/cache"));
    profile->setPersistentStoragePath(storagePath + QStringLiteral("/storage"));
    profile->setCachePath(storagePath + QStringLiteral("/cache"));
    profile->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);

    PacitumSchemeHandler::install(profile);

    auto* settings = profile->settings();
    settings->setAttribute(QWebEngineSettings::PluginsEnabled, true);
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    settings->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, true);
    settings->setAttribute(QWebEngineSettings::WebGLEnabled, true);
    settings->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, true);
    settings->setAttribute(QWebEngineSettings::AutoLoadIconsForPage, true);
    settings->setAttribute(QWebEngineSettings::TouchIconsEnabled, true);
    settings->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    settings->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, false);
    settings->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, false);
    settings->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
    settings->setAttribute(QWebEngineSettings::HyperlinkAuditingEnabled, false);
    settings->setAttribute(QWebEngineSettings::ErrorPageEnabled, true);

    profile->setSpellCheckEnabled(true);
    profile->setSpellCheckLanguages({QStringLiteral("en-US")});
    profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
    profile->setHttpCacheMaximumSize(250 * 1024 * 1024);

    try {
        BrowserWindow w;
        w.show();
        return app.exec();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
