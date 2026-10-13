#include "Updater.h"
#include "AppSettings.h"
#include "ThemeManager.h"
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
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>
#include <QDesktopServices>

void pacitumLog(const QString& line);   // main.cpp (startup.log)

namespace {
const char* kDefaultFeed = "https://api.github.com/repos/namo2024mntik-ops/safari-browser-cpp/releases/latest";
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
        static const QRegularExpression setupRe(QStringLiteral("setup.*\\.exe$"), QRegularExpression::CaseInsensitiveOption);
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
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/PacitumUpdate");
    QDir().mkpath(dir);
    QString name = QFileInfo(info.installerName).fileName();
    name.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")));
    if (name.isEmpty()) name = QStringLiteral("PacitumBrowser-Setup.exe");
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

bool Updater::installAndRelaunch() {
#ifdef Q_OS_WIN
    if (m_file.isEmpty() || !QFile::exists(m_file)) return false;
    // The installer refuses to run while our AppMutex is held, so a tiny hidden PowerShell helper
    // waits for this process to exit, then runs Setup silently. Setup's [Run] entry with
    // /RELAUNCH=1 starts the new version when it is done.
    auto q = [](QString s){ return QStringLiteral("'") + s.replace(QLatin1Char('\''), QStringLiteral("''")) + QStringLiteral("'"); };
    const QString script = QStringLiteral(
        "$ErrorActionPreference='SilentlyContinue'; Wait-Process -Id %1 -Timeout 120; Start-Sleep -Milliseconds 500; "
        "Start-Process -FilePath %2 -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CLOSEAPPLICATIONS','/NORESTARTAPPLICATIONS','/RELAUNCH=1'")
        .arg(QCoreApplication::applicationPid()).arg(q(QDir::toNativeSeparators(m_file)));
    const bool ok = QProcess::startDetached(QStringLiteral("powershell.exe"),
        {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"), QStringLiteral("-Command"), script});
    pacitumLog(QStringLiteral("update: installer helper started=%1").arg(ok));
    if (ok) QTimer::singleShot(0, qApp, []{ for (auto* w : QApplication::topLevelWidgets()) w->close(); QCoreApplication::quit(); });
    return ok;
#else
    return false;   // the installer is Windows-only; the dialog shows where the file was saved
#endif
}

// ---------------------------------------------------------------------------------------------
UpdateDialog::UpdateDialog(const UpdateInfo& info, QWidget* parent) : QDialog(parent), m_info(info) {
    setObjectName(QStringLiteral("UpdateDialog"));
    setWindowTitle(tr("Update Pacitum"));
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumWidth(480);
    const auto& p = ThemeManager::current();
    setStyleSheet(QStringLiteral(
        "#UpdateDialog{background:%1;} QLabel{color:%2;} QLabel#UpdSub{color:%3;}"
        "QTextBrowser{background:%4;color:%2;border:1px solid %5;border-radius:10px;padding:8px;}"
        "QProgressBar{background:%4;border:none;border-radius:3px;max-height:6px;} QProgressBar::chunk{background:%6;border-radius:3px;}"
        "QPushButton{padding:7px 16px;border-radius:8px;border:1px solid %5;background:%4;color:%2;}"
        "QPushButton#UpdPrimary{background:%6;color:%7;border:none;font-weight:600;}")
        .arg(p.toolbar, p.text, p.textSecondary, p.surface, p.border, p.accent, p.onAccent));
    auto* l = new QVBoxLayout(this); l->setContentsMargins(24, 22, 24, 18); l->setSpacing(10);
    auto* head = new QHBoxLayout(); head->setSpacing(14);
    auto* logo = new QLabel(this); logo->setPixmap(QIcon(QStringLiteral(":/icons/pacitum-icon-128.png")).pixmap(QSize(44, 44), devicePixelRatioF()));
    head->addWidget(logo, 0, Qt::AlignTop);
    auto* tl = new QVBoxLayout(); tl->setSpacing(2);
    auto* title = new QLabel(tr("Pacitum %1 is available").arg(info.version), this);
    QFont f = title->font(); f.setPointSizeF(f.pointSizeF() * 1.3); f.setWeight(QFont::DemiBold); title->setFont(f);
    auto* sub = new QLabel(tr("You have %1. The update installs in the background and Pacitum restarts with your tabs.").arg(Updater::currentVersion()), this);
    sub->setObjectName(QStringLiteral("UpdSub")); sub->setWordWrap(true);
    tl->addWidget(title); tl->addWidget(sub);
    head->addLayout(tl, 1);
    l->addLayout(head);
    m_notes = new QTextBrowser(this);
    m_notes->setOpenExternalLinks(true);
    m_notes->setMarkdown(info.notes.isEmpty() ? tr("_No release notes._") : info.notes);
    m_notes->setMinimumHeight(180);
    l->addWidget(m_notes, 1);
    m_bar = new QProgressBar(this); m_bar->setTextVisible(false); m_bar->setRange(0, 1000); m_bar->hide();
    l->addWidget(m_bar);
    m_status = new QLabel(this); m_status->setObjectName(QStringLiteral("UpdSub")); m_status->setWordWrap(true); m_status->hide();
    l->addWidget(m_status);
    auto* btns = new QHBoxLayout(); btns->setSpacing(8);
    m_skip = new QPushButton(tr("Skip this version"), this);
    m_later = new QPushButton(tr("Later"), this);
    m_update = new QPushButton(tr("Update now"), this); m_update->setObjectName(QStringLiteral("UpdPrimary")); m_update->setDefault(true);
    btns->addWidget(m_skip); btns->addStretch(1); btns->addWidget(m_later); btns->addWidget(m_update);
    l->addLayout(btns);
    connect(m_skip, &QPushButton::clicked, this, [this]{ AppSettings::set(QStringLiteral("update_skipped_version"), m_info.version); reject(); });
    connect(m_later, &QPushButton::clicked, this, [this]{ Updater::instance()->cancelDownload(); reject(); });
    connect(m_update, &QPushButton::clicked, this, &UpdateDialog::startDownload);
    auto* up = Updater::instance();
    connect(up, &Updater::downloadProgress, this, [this](qint64 r, qint64 t){
        if (t > 0) { m_bar->setRange(0, 1000); m_bar->setValue(int(r * 1000 / t)); } else m_bar->setRange(0, 0);
        m_status->setText(tr("Downloading… %1 of %2 MB").arg(r / 1048576.0, 0, 'f', 1).arg(t > 0 ? QString::number(t / 1048576.0, 'f', 1) : QStringLiteral("?")));
    });
    connect(up, &Updater::downloadFinished, this, [this](bool ok, const QString& err){
        if (!ok) {
            m_status->setText(tr("Update failed: %1").arg(err));
            m_update->setEnabled(true); m_update->setText(tr("Try again")); m_skip->setEnabled(true);
            return;
        }
        m_bar->setValue(1000);
        m_status->setText(tr("Installing… Pacitum will restart in a moment."));
        if (!Updater::instance()->installAndRelaunch()) {
            m_status->setText(tr("Couldn't start the installer. It was saved to %1").arg(QDir::toNativeSeparators(Updater::instance()->downloadedPath())));
            m_update->setEnabled(true);
        }
    });
}

void UpdateDialog::startDownload() {
    m_update->setEnabled(false); m_skip->setEnabled(false);
    m_bar->show(); m_status->show(); m_status->setText(tr("Starting download…"));
    Updater::instance()->download(m_info);
}
