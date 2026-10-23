#include "Updater.h"
#include "AppSettings.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QApplication>
#include <QProcess>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>

void pacitumLog(const QString& line);   // main.cpp (startup.log)

namespace {
const char* kDefaultFeed = "https://api.github.com/repos/namo2024mntik-ops/pacitum-browser/releases/latest";
constexpr int kFirstCheckMs = 45 * 1000;            // well after startup: never competes with the first paint
constexpr int kIntervalMs = 6 * 60 * 60 * 1000;     // every 6 h while running
constexpr qint64 kMinCheckGapSecs = 12 * 60 * 60;   // background checks at most twice a day across restarts

QVector<int> versionParts(QString v) {
    v = v.trimmed();
    if (v.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) v.remove(0, 1);
    const int dash = v.indexOf(QRegularExpression(QStringLiteral("[^0-9.]")));
    if (dash >= 0) v.truncate(dash);
    QVector<int> out;
    for (const auto& p : v.split(QLatin1Char('.'), Qt::SkipEmptyParts)) out << p.toInt();
    while (out.size() < 3) out << 0;
    return out;
}
// Testing against the private repo only: a token from the environment (never stored, never shipped).
// With it, assets are fetched through the API asset URL, which works for private repositories.
QByteArray devToken() { return qgetenv("PACITUM_UPDATE_TOKEN"); }
void addAuth(QNetworkRequest& r) { const QByteArray t = devToken(); if (!t.isEmpty()) r.setRawHeader("Authorization", "Bearer " + t); }
QString assetUrl(const QJsonObject& a) {
    return devToken().isEmpty() ? a.value(QStringLiteral("browser_download_url")).toString() : a.value(QStringLiteral("url")).toString();
}
QNetworkRequest apiRequest(const QUrl& u) {
    QNetworkRequest r(u);
    addAuth(r);
    r.setRawHeader("Accept", "application/vnd.github+json");
    r.setRawHeader("User-Agent", QByteArray("PacitumBrowser/") + PACITUM_VERSION);
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setTransferTimeout(30000);
    return r;
}
}

static Updater* g_updater = nullptr;
Updater* Updater::instance() { if (!g_updater) g_updater = new Updater(qApp); return g_updater; }

Updater::Updater(QObject* parent) : QObject(parent) {
    m_net = new QNetworkAccessManager(this);
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this]{
        m_timer->setInterval(kIntervalMs);
        const qint64 last = qint64(AppSettings::get(QStringLiteral("update_last_check")).toDouble(0));
        if (QDateTime::currentSecsSinceEpoch() - last >= kMinCheckGapSecs) check(false);
    });
}

QString Updater::currentVersion() { return QCoreApplication::applicationVersion(); }

QUrl Updater::feedUrl() {
    const QString env = qEnvironmentVariable("PACITUM_UPDATE_FEED");
    if (!env.isEmpty()) return QUrl(env);
    const QString s = AppSettings::get(QStringLiteral("update_feed_url")).toString().trimmed();
    return QUrl(s.isEmpty() ? QString::fromLatin1(kDefaultFeed) : s);
}

int Updater::compareVersions(const QString& a, const QString& b) {
    const auto x = versionParts(a), y = versionParts(b);
    for (int i = 0; i < qMax(x.size(), y.size()); ++i) {
        const int p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
        if (p != q) return p < q ? -1 : 1;
    }
    return 0;
}

void Updater::setStatus(const QString& s) { m_status = s; emit statusChanged(s); }

void Updater::setAutoCheck(bool on) {
    if (on) {
        if (!m_timer->isActive()) {
            bool okEnv = false; const int envMs = qEnvironmentVariableIntValue("PACITUM_UPDATE_CHECK_DELAY_MS", &okEnv);   // testing
            m_timer->setInterval(okEnv ? envMs : kFirstCheckMs); m_timer->start();
        }
    }
    else m_timer->stop();
}

void Updater::check(bool userInitiated) {
    if (m_reply) return;
    emit checking();
    pacitumLog(QStringLiteral("update check: %1 (%2)").arg(feedUrl().toString(), userInitiated ? QStringLiteral("user") : QStringLiteral("background")));
    setStatus(tr("Checking for updates…"));
    auto* reply = m_net->get(apiRequest(feedUrl()));
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, userInitiated]{
        reply->deleteLater();
        m_reply = nullptr;
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            QString err = reply->errorString();
            if (http == 404) err = tr("No public release feed found (the repository may be private).");
            else if (http == 403) err = tr("Update server is rate-limiting requests. Try again later.");
            pacitumLog(QStringLiteral("update check failed: http=%1 %2").arg(http).arg(reply->errorString()));
            setStatus(tr("Couldn't check for updates: %1").arg(err));
            emit checkFailed(err, userInitiated);
            return;
        }
        AppSettings::set(QStringLiteral("update_last_check"), double(QDateTime::currentSecsSinceEpoch()));
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject rel = doc.object();
        if (doc.isArray()) {   // a ".../releases" list: take the newest non-draft, non-prerelease
            for (const auto& v : doc.array()) { const auto o = v.toObject(); if (!o.value(QStringLiteral("draft")).toBool() && !o.value(QStringLiteral("prerelease")).toBool()) { rel = o; break; } }
        }
        UpdateInfo info;
        info.tag = rel.value(QStringLiteral("tag_name")).toString();
        info.version = info.tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive) ? info.tag.mid(1) : info.tag;
        info.name = rel.value(QStringLiteral("name")).toString(info.tag);
        info.notes = rel.value(QStringLiteral("body")).toString();
        info.pageUrl = rel.value(QStringLiteral("html_url")).toString();
        const auto assets = rel.value(QStringLiteral("assets")).toArray();
        // Each platform takes its own release asset: Windows Setup .exe, Linux AppImage, macOS .dmg.
#if defined(Q_OS_WIN)
        static const QRegularExpression setupRe(QStringLiteral("setup.*\\.exe$"), QRegularExpression::CaseInsensitiveOption);
#elif defined(Q_OS_MACOS)
        static const QRegularExpression setupRe(QStringLiteral("macos.*\\.dmg$"), QRegularExpression::CaseInsensitiveOption);
#else
        static const QRegularExpression setupRe(QStringLiteral("linux.*\\.appimage$"), QRegularExpression::CaseInsensitiveOption);
#endif
        for (const auto& av : assets) {
            const auto a = av.toObject(); const QString n = a.value(QStringLiteral("name")).toString();
            if (setupRe.match(n).hasMatch() && info.installerName.isEmpty()) {
                info.installerName = n;
                info.installerUrl = QUrl(assetUrl(a));
                info.installerSize = qint64(a.value(QStringLiteral("size")).toDouble(-1));
            }
        }
        for (const auto& av : assets) {
            const auto a = av.toObject(); const QString n = a.value(QStringLiteral("name")).toString();
            if (!info.installerName.isEmpty() && n.compare(info.installerName + QStringLiteral(".sha256"), Qt::CaseInsensitive) == 0) {
                info.shaUrl = QUrl(assetUrl(a)); info.shaIsSums = false;
            } else if (!info.shaUrl.isValid() && n.compare(QStringLiteral("SHA256SUMS"), Qt::CaseInsensitive) == 0) {
                info.shaUrl = QUrl(assetUrl(a)); info.shaIsSums = true;
            }
        }
        if (info.version.isEmpty()) {
            setStatus(tr("Couldn't read the update feed."));
            emit checkFailed(tr("Unexpected response from the update feed."), userInitiated);
            return;
        }
        m_latest = info;
        pacitumLog(QStringLiteral("update check: current=%1 latest=%2 installer=%3").arg(currentVersion(), info.version, info.installerName));
        if (compareVersions(info.version, currentVersion()) > 0 && info.valid()) {
            setStatus(tr("Pacitum %1 is available.").arg(info.version));
            emit updateAvailable(info, userInitiated);
        } else {
            setStatus(tr("Pacitum is up to date (%1).").arg(currentVersion()));
            emit upToDate(userInitiated);
        }
    });
}

void Updater::download(const UpdateInfo& info) {
    if (m_reply || !info.valid()) return;
    m_pending = info;
    const QString dir = workDir();
    QString name = QFileInfo(info.installerName).fileName();
    name.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")));
#if defined(Q_OS_WIN)
    if (name.isEmpty()) name = QStringLiteral("PacitumBrowser-Setup.exe");
#elif defined(Q_OS_MACOS)
    if (name.isEmpty()) name = QStringLiteral("PacitumBrowser-macOS.dmg");
#else
    if (name.isEmpty()) name = QStringLiteral("PacitumBrowser-linux-x86_64.AppImage");
#endif
    m_file = dir + QLatin1Char('/') + name;
    QFile::remove(m_file);
    delete m_out; m_out = new QFile(m_file + QStringLiteral(".part"), this);
    if (!m_out->open(QIODevice::WriteOnly | QIODevice::Truncate)) { emit downloadFinished(false, tr("Can't write to %1").arg(dir)); return; }
    QNetworkRequest r(info.installerUrl);
    r.setRawHeader("Accept", "application/octet-stream");
    addAuth(r);
    r.setRawHeader("User-Agent", QByteArray("PacitumBrowser/") + PACITUM_VERSION);
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setTransferTimeout(60000);
    auto* reply = m_net->get(r);
    m_reply = reply;
    setStatus(tr("Downloading Pacitum %1…").arg(info.version));
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]{ if (m_out) m_out->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 r, qint64 t){
        emit downloadProgress(r, t > 0 ? t : m_pending.installerSize);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]{
        reply->deleteLater(); m_reply = nullptr;
        if (m_out) { m_out->write(reply->readAll()); m_out->close(); }
        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(m_file + QStringLiteral(".part"));
            setStatus(tr("Download failed: %1").arg(reply->errorString()));
            emit downloadFinished(false, reply->errorString());
            return;
        }
        const qint64 got = QFileInfo(m_file + QStringLiteral(".part")).size();
        if (m_pending.installerSize > 0 && got != m_pending.installerSize) {
            QFile::remove(m_file + QStringLiteral(".part"));
            const QString e = tr("Downloaded size %1 doesn't match the expected %2 bytes.").arg(got).arg(m_pending.installerSize);
            setStatus(e); emit downloadFinished(false, e); return;
        }
        fetchSha();
    });
}

void Updater::cancelDownload() {
    if (m_reply) m_reply->abort();
}

void Updater::fetchSha() {
    if (!m_pending.shaUrl.isValid()) { verifyAndFinish({}); return; }
    QNetworkRequest r(m_pending.shaUrl);
    r.setRawHeader("Accept", "application/octet-stream");
    addAuth(r);
    r.setRawHeader("User-Agent", QByteArray("PacitumBrowser/") + PACITUM_VERSION);
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setTransferTimeout(30000);
    auto* reply = m_net->get(r);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]{
        reply->deleteLater(); m_reply = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(m_file + QStringLiteral(".part"));
            const QString e = tr("Couldn't download the checksum: %1").arg(reply->errorString());
            setStatus(e); emit downloadFinished(false, e); return;
        }
        const QString text = QString::fromUtf8(reply->readAll());
        QByteArray hex;
        static const QRegularExpression line(QStringLiteral("^\\s*([0-9a-fA-F]{64})\\s+\\*?(\\S.*?)\\s*$"), QRegularExpression::MultilineOption);
        auto it = line.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            if (!m_pending.shaIsSums || QFileInfo(m.captured(2)).fileName().compare(m_pending.installerName, Qt::CaseInsensitive) == 0) { hex = m.captured(1).toLatin1().toLower(); break; }
        }
        if (hex.isEmpty()) {
            static const QRegularExpression bare(QStringLiteral("\\b([0-9a-fA-F]{64})\\b"));
            if (!m_pending.shaIsSums) { const auto m = bare.match(text); if (m.hasMatch()) hex = m.captured(1).toLatin1().toLower(); }
        }
        if (hex.isEmpty()) {
            QFile::remove(m_file + QStringLiteral(".part"));
            const QString e = tr("The checksum file doesn't list %1.").arg(m_pending.installerName);
            setStatus(e); emit downloadFinished(false, e); return;
        }
        verifyAndFinish(hex);
    });
}

void Updater::verifyAndFinish(const QByteArray& expectedHex) {
    const QString part = m_file + QStringLiteral(".part");
    if (!expectedHex.isEmpty()) {
        QFile f(part);
        QCryptographicHash h(QCryptographicHash::Sha256);
        if (!f.open(QIODevice::ReadOnly) || !h.addData(&f) || h.result().toHex() != expectedHex) {
            f.close(); QFile::remove(part);
            const QString e = tr("Checksum mismatch: the download is damaged or was tampered with.");
            pacitumLog(QStringLiteral("update: SHA256 mismatch"));
            setStatus(e); emit downloadFinished(false, e); return;
        }
    }
    QFile::remove(m_file);
    if (!QFile::rename(part, m_file)) { const QString e = tr("Couldn't finalize the download."); setStatus(e); emit downloadFinished(false, e); return; }
    pacitumLog(QStringLiteral("update: downloaded %1 (%2)").arg(m_file, expectedHex.isEmpty() ? QStringLiteral("size verified, no SHA256 asset") : QStringLiteral("SHA256 verified")));
    setStatus(expectedHex.isEmpty() ? tr("Downloaded (size verified).") : tr("Downloaded and verified."));
    emit downloadFinished(true, {});
}

QString Updater::workDir() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/PacitumUpdate");
    QDir().mkpath(dir);
    return dir;
}

QStringList Updater::noteLines(const QString& markdown, int max) {
    static const QRegularExpression bullet(QStringLiteral("^\\s*(?:[-*+•·]|\\d+[.)])\\s+(.+)$"));
    static const QRegularExpression link(QStringLiteral("\\[([^\\]]+)\\]\\([^)]*\\)"));
    static const QRegularExpression marks(QStringLiteral("[*_`]+|<[^>]+>"));
    auto clean = [](QString t){ t.replace(link, QStringLiteral("\\1")); t.remove(marks); return t.simplified(); };
    QStringList bullets, plain;
    for (const QString& raw : markdown.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('|')) || line.startsWith(QLatin1Char('>'))) continue;
        if (const auto m = bullet.match(line); m.hasMatch()) { if (const QString t = clean(m.captured(1)); !t.isEmpty()) bullets << t; }
        else if (const QString t = clean(line); !t.isEmpty()) plain << t;
    }
    QStringList out = bullets.isEmpty() ? plain : bullets;
    for (QString& t : out) if (t.size() > 90) t = t.left(87).trimmed() + QStringLiteral("…");
    if (max <= 1) return out.mid(0, qMax(0, max));
    if (out.size() > max) { const int more = out.size() - (max - 1); out = out.mid(0, max - 1); out << tr("…and %n more", nullptr, more); }
    return out;
}

bool Updater::startInstall(const QStringList& overlayArgs) {
    if (m_file.isEmpty() || !QFile::exists(m_file)) return false;
    const QString log = QDir::currentPath() + QStringLiteral("/startup.log");
    QStringList args = overlayArgs;
    args << QStringLiteral("--pid") << QString::number(QCoreApplication::applicationPid())
         << QStringLiteral("--version") << m_pending.version
         << QStringLiteral("--ready") << readyFile() << QStringLiteral("--marker") << markerFile()
         << QStringLiteral("--log") << log;
    QFile::remove(readyFile()); QFile::remove(markerFile());
#ifdef Q_OS_WIN
    // The helper runs from a copy in the temp folder with the few Qt files it needs, so Setup can
    // replace everything in the install folder while the "Installing" pop-up is on screen.
    const QString appDir = QCoreApplication::applicationDirPath(), helperDir = workDir() + QStringLiteral("/helper");
    QDir(helperDir).removeRecursively();
    QDir().mkpath(helperDir + QStringLiteral("/platforms"));
    bool copied = QFile::copy(appDir + QStringLiteral("/PacitumUpdateHelper.exe"), helperDir + QStringLiteral("/PacitumUpdateHelper.exe"));
    for (const char* f : {"Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "platforms/qwindows.dll"})
        copied = copied && QFile::copy(appDir + QLatin1Char('/') + QLatin1String(f), helperDir + QLatin1Char('/') + QLatin1String(f));
    for (const QString& f : QDir(appDir).entryList({QStringLiteral("vcruntime*.dll"), QStringLiteral("msvcp*.dll"), QStringLiteral("concrt*.dll")}, QDir::Files))
        QFile::copy(appDir + QLatin1Char('/') + f, helperDir + QLatin1Char('/') + f);
    if (copied) {
        args << QStringLiteral("--installer") << QDir::toNativeSeparators(m_file)
             << QStringLiteral("--app") << QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        const bool ok = QProcess::startDetached(helperDir + QStringLiteral("/PacitumUpdateHelper.exe"), args, helperDir);
        pacitumLog(QStringLiteral("update: helper started=%1").arg(ok));
        if (ok) return true;
    } else pacitumLog(QStringLiteral("update: couldn't copy the helper; falling back to the hidden installer"));
    // Fallback (developer builds without the helper): a hidden PowerShell waits for us, then runs Setup.
    auto q = [](QString s){ return QStringLiteral("'") + s.replace(QLatin1Char('\''), QStringLiteral("''")) + QStringLiteral("'"); };
    const QString script = QStringLiteral(
        "$ErrorActionPreference='SilentlyContinue'; Wait-Process -Id %1 -Timeout 30; Stop-Process -Id %1 -Force; Start-Sleep -Milliseconds 500; "
        "Start-Process -FilePath %2 -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CLOSEAPPLICATIONS','/NORESTARTAPPLICATIONS','/RELAUNCH=1'")
        .arg(QCoreApplication::applicationPid()).arg(q(QDir::toNativeSeparators(m_file)));
    const bool ok = QProcess::startDetached(QStringLiteral("powershell.exe"),
        {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"), QStringLiteral("-Command"), script});
    pacitumLog(QStringLiteral("update: installer helper (PowerShell) started=%1").arg(ok));
    return ok;
#elif defined(Q_OS_MACOS)
    // macOS: open the downloaded disk image; the user drags the new app over the old one.
    const bool ok = QProcess::startDetached(QStringLiteral("/usr/bin/open"), {m_file});
    pacitumLog(QStringLiteral("update: opened %1 ok=%2").arg(m_file).arg(ok));
    Q_UNUSED(args);
    return false;
#else
    // Linux AppImage: swap the AppImage file now; the new AppImage, started in --update-overlay
    // mode, shows the pop-up, waits for us to exit and starts itself normally.
    const QString current = qEnvironmentVariable("APPIMAGE");
    if (current.isEmpty() || !QFileInfo(current).isWritable()) return false;
    const QString staged = current + QStringLiteral(".new");
    QFile::remove(staged);
    if (!QFile::copy(m_file, staged)) return false;
    QFile::setPermissions(staged, QFile::permissions(current) | QFileDevice::ExeOwner | QFileDevice::ExeUser);
    if (!QFile::remove(current) || !QFile::rename(staged, current)) { pacitumLog(QStringLiteral("update: could not replace %1").arg(current)); return false; }
    args << QStringLiteral("--relaunch") << current << QStringLiteral("--app") << current;
    bool ok = QProcess::startDetached(current, QStringList{QStringLiteral("--update-overlay")} + args);
    pacitumLog(QStringLiteral("update: replaced %1, overlay=%2").arg(current).arg(ok));
    if (!ok) ok = QProcess::startDetached(current, {});   // no pop-up, but still restart
    return ok;
#endif
}
