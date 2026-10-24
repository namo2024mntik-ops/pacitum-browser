#include "ChromeExtensions.h"
#include "AppSettings.h"
#include "ThemeManager.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QWebEngineProfile>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QtEndian>
#ifdef PACITUM_CHROME_EXTENSIONS
#include <QWebEngineExtensionManager>
#include <QWebEngineExtensionInfo>
#endif

void pacitumLog(const QString& line);   // main.cpp (startup.log)

namespace {
QJsonObject readManifest(const QString& dir) {
    QFile f(dir + QStringLiteral("/manifest.json"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    // Chrome allows comments in manifest.json; strip whole-line // comments before parsing.
    QByteArray raw = f.readAll();
    QByteArray clean;
    for (const QByteArray& line : raw.split('\n')) if (!line.trimmed().startsWith("//")) clean += line + '\n';
    return QJsonDocument::fromJson(clean).object();
}

// "__MSG_appName__" -> text from _locales/<lang or default_locale>/messages.json
QString localized(const QString& dir, const QJsonObject& m, const QString& value) {
    static const QRegularExpression re(QStringLiteral("^__MSG_(\\w+)__$"));
    const auto match = re.match(value);
    if (!match.hasMatch()) return value;
    const QString key = match.captured(1).toLower();
    QStringList langs{QLocale::system().name(), QLocale::system().name().section(QLatin1Char('_'), 0, 0),
                      m.value(QStringLiteral("default_locale")).toString(), QStringLiteral("en")};
    for (const QString& l : langs) {
        if (l.isEmpty()) continue;
        QFile f(dir + QStringLiteral("/_locales/") + l + QStringLiteral("/messages.json"));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonObject msgs = QJsonDocument::fromJson(f.readAll()).object();
        for (auto it = msgs.begin(); it != msgs.end(); ++it)
            if (it.key().toLower() == key) return it.value().toObject().value(QStringLiteral("message")).toString();
    }
    return value;
}

QIcon manifestIcon(const QString& dir, const QJsonObject& m) {
    QJsonValue v = m.value(QStringLiteral("action")).toObject().value(QStringLiteral("default_icon"));
    if (v.isUndefined()) v = m.value(QStringLiteral("browser_action")).toObject().value(QStringLiteral("default_icon"));
    if (v.isUndefined()) v = m.value(QStringLiteral("icons"));
    QIcon icon;
    if (v.isString()) icon.addFile(dir + QLatin1Char('/') + v.toString());
    else for (auto it = v.toObject().begin(); it != v.toObject().end(); ++it) {
        const QString file = dir + QLatin1Char('/') + it.value().toString();
        if (QFileInfo::exists(file)) icon.addFile(file, QSize(it.key().toInt(), it.key().toInt()));
    }
    return icon;
}

const char* kStoreHome = "https://chromewebstore.google.com/";
}

ChromeExtensions* ChromeExtensions::instance() { static auto* c = new ChromeExtensions(qApp); return c; }

ChromeExtensions::ChromeExtensions(QObject* parent) : QObject(parent) {
    m_net = new QNetworkAccessManager(this);
}

bool ChromeExtensions::available() {
#ifdef PACITUM_CHROME_EXTENSIONS
    return true;
#else
    return false;
#endif
}

void ChromeExtensions::attach(QWebEngineProfile* profile) {
    if (!profile || m_profile) return;
    m_profile = profile;
#ifdef PACITUM_CHROME_EXTENSIONS
    if (profile->isOffTheRecord()) return;   // Guest: nothing is installed or kept
    auto* mgr = profile->extensionManager();
    if (!mgr) return;
    connect(mgr, &QWebEngineExtensionManager::installFinished, this, [this](const QWebEngineExtensionInfo& e){
        if (!e.error().isEmpty()) {
            pacitumLog(QStringLiteral("extension install failed: %1").arg(e.error()));
            emit message(tr("Couldn't add the extension: %1").arg(e.error()));
        } else {
            pacitumLog(QStringLiteral("extension installed: %1 (%2)").arg(e.name(), e.id()));
            emit message(tr("Added %1").arg(localized(e.path(), readManifest(e.path()), e.name())));
        }
        emit changed();
    });
    connect(mgr, &QWebEngineExtensionManager::uninstallFinished, this, [this](const QWebEngineExtensionInfo& e){
        setPinned(e.id(), false);
        emit changed();
    });
    connect(mgr, &QWebEngineExtensionManager::loadFinished, this, &ChromeExtensions::changed);
    connect(mgr, &QWebEngineExtensionManager::unloadFinished, this, &ChromeExtensions::changed);
#endif
}

QList<ChromeExtensionItem> ChromeExtensions::items() const {
    QList<ChromeExtensionItem> out;
#ifdef PACITUM_CHROME_EXTENSIONS
    if (!m_profile || m_profile->isOffTheRecord() || !m_profile->extensionManager()) return out;
    for (const QWebEngineExtensionInfo& e : m_profile->extensionManager()->extensions()) {
        if (!e.isInstalled()) continue;   // only what the user added (not Chromium's built-in components)
        ChromeExtensionItem it;
        const QJsonObject m = readManifest(e.path());
        it.id = e.id(); it.path = e.path(); it.error = e.error();
        it.name = localized(e.path(), m, e.name().isEmpty() ? m.value(QStringLiteral("name")).toString() : e.name());
        it.description = localized(e.path(), m, e.description().isEmpty() ? m.value(QStringLiteral("description")).toString() : e.description());
        it.version = m.value(QStringLiteral("version")).toString();
        it.enabled = e.isEnabled(); it.loaded = e.isLoaded();
        it.popupUrl = e.actionPopupUrl();
        QString options = m.value(QStringLiteral("options_ui")).toObject().value(QStringLiteral("page")).toString();
        if (options.isEmpty()) options = m.value(QStringLiteral("options_page")).toString();
        if (!options.isEmpty()) it.optionsUrl = QUrl(QStringLiteral("chrome-extension://%1/%2").arg(e.id(), options));
        it.icon = manifestIcon(e.path(), m);
        it.permissions = describePermissions(e.path());
        out << it;
    }
#endif
    return out;
}

QStringList ChromeExtensions::describePermissions(const QString& dir) {
    const QJsonObject m = readManifest(dir);
    QStringList hosts, out;
    auto addHost = [&hosts](const QString& p){
        if (p == QLatin1String("<all_urls>") || p.startsWith(QLatin1String("*://*/")) || p.startsWith(QLatin1String("http://*/")) || p.startsWith(QLatin1String("https://*/"))) {
            if (!hosts.contains(QObject::tr("all websites"))) hosts.prepend(QObject::tr("all websites"));
        } else if (p.contains(QStringLiteral("://"))) {
            QString h = p.section(QStringLiteral("://"), 1).section(QLatin1Char('/'), 0, 0);
            if (h.startsWith(QStringLiteral("*."))) h = h.mid(2);
            if (!h.isEmpty() && !hosts.contains(h)) hosts << h;
        }
    };
    static const QHash<QString, QString> api{
        {QStringLiteral("tabs"), QObject::tr("See your open tabs and the pages you visit")},
        {QStringLiteral("history"), QObject::tr("Read and change your browsing history")},
        {QStringLiteral("bookmarks"), QObject::tr("Read and change your bookmarks")},
        {QStringLiteral("cookies"), QObject::tr("Read and change cookies")},
        {QStringLiteral("downloads"), QObject::tr("Manage your downloads")},
        {QStringLiteral("webRequest"), QObject::tr("See and change network requests")},
        {QStringLiteral("declarativeNetRequest"), QObject::tr("Block or change network requests")},
        {QStringLiteral("clipboardRead"), QObject::tr("Read what you copy")},
        {QStringLiteral("nativeMessaging"), QObject::tr("Talk to other programs on this computer")},
        {QStringLiteral("management"), QObject::tr("Manage your other extensions")},
        {QStringLiteral("debugger"), QObject::tr("Debug pages (full access to them)")},
        {QStringLiteral("proxy"), QObject::tr("Change your proxy settings")},
        {QStringLiteral("privacy"), QObject::tr("Change privacy settings")},
        {QStringLiteral("geolocation"), QObject::tr("Know your location")},
        {QStringLiteral("notifications"), QObject::tr("Show notifications")},
        {QStringLiteral("scripting"), QObject::tr("Run scripts on pages")},
    };
    for (const auto& v : m.value(QStringLiteral("permissions")).toArray()) {
        const QString p = v.toString();
        if (api.contains(p)) out << api.value(p); else addHost(p);
    }
    for (const auto& v : m.value(QStringLiteral("host_permissions")).toArray()) addHost(v.toString());
    for (const auto& cs : m.value(QStringLiteral("content_scripts")).toArray())
        for (const auto& v : cs.toObject().value(QStringLiteral("matches")).toArray()) addHost(v.toString());
    if (!hosts.isEmpty()) {
        if (hosts.first() == QObject::tr("all websites")) hosts = QStringList{hosts.first()};   // the rest adds nothing
        QStringList shown = hosts.mid(0, 4);
        if (hosts.size() > 4) shown << QObject::tr("%n more", nullptr, int(hosts.size()) - 4);
        out.prepend(QObject::tr("Read and change pages on %1").arg(shown.join(QStringLiteral(", "))));
    }
    return out;
}

QString ChromeExtensions::crxToZip(const QString& crxPath, QString* error) const {
    QFile f(crxPath);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 256 * 1024 * 1024) { *error = tr("Can't read %1").arg(QFileInfo(crxPath).fileName()); return {}; }
    const QByteArray d = f.readAll();
    qsizetype zipAt = -1;
    if (d.startsWith("PK\x03\x04")) zipAt = 0;   // already a zip
    else if (d.startsWith("Cr24") && d.size() > 16) {
        const quint32 ver = qFromLittleEndian<quint32>(d.constData() + 4);
        if (ver == 3) zipAt = 12 + qsizetype(qFromLittleEndian<quint32>(d.constData() + 8));
        else if (ver == 2) zipAt = 16 + qsizetype(qFromLittleEndian<quint32>(d.constData() + 8)) + qFromLittleEndian<quint32>(d.constData() + 12);
    }
    if (zipAt < 0 || zipAt + 4 > d.size() || d.mid(zipAt, 4) != QByteArray("PK\x03\x04", 4)) {
        *error = tr("%1 isn't a Chrome extension package.").arg(QFileInfo(crxPath).fileName());
        return {};
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/PacitumExtensions");
    QDir().mkpath(dir);
    QString name = QFileInfo(crxPath).completeBaseName();
    name.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")));
    const QString zip = dir + QLatin1Char('/') + (name.isEmpty() ? QStringLiteral("extension") : name) + QStringLiteral(".zip");
    QFile out(zip);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(d.constData() + zipAt, d.size() - zipAt) != d.size() - zipAt) {
        *error = tr("Can't write %1").arg(zip); return {};
    }
    return zip;
}

void ChromeExtensions::installFromFile(const QString& path, QWidget* parent) {
#ifdef PACITUM_CHROME_EXTENSIONS
    if (!m_profile || m_profile->isOffTheRecord()) { emit message(tr("Extensions can't be added in Guest mode")); return; }
    const QFileInfo fi(path);
    QString source = path, error;
    QString question;
    if (fi.isDir()) {
        const QJsonObject m = readManifest(path);
        if (m.isEmpty()) { QMessageBox::warning(parent, tr("Add extension"), tr("This folder has no manifest.json.")); return; }
        const QString name = localized(path, m, m.value(QStringLiteral("name")).toString());
        const QStringList perms = describePermissions(path);
        question = tr("Add \"%1\"?").arg(name);
        if (!perms.isEmpty()) question += QStringLiteral("\n\n") + tr("It can:") + QStringLiteral("\n• ") + perms.join(QStringLiteral("\n• "));
    } else {
        source = crxToZip(path, &error);
        if (source.isEmpty()) { QMessageBox::warning(parent, tr("Add extension"), error); return; }
        question = tr("Add the extension in \"%1\"?\n\nOnly add extensions from people you trust: an extension can read and change the pages you visit.").arg(fi.fileName());
    }
    if (QMessageBox::question(parent, tr("Add extension"), question) != QMessageBox::Yes) return;
    m_profile->extensionManager()->installExtension(source);
#else
    Q_UNUSED(path);
    QMessageBox::information(parent, tr("Chrome extensions"), tr("Chrome extensions need Pacitum 1.9 (built with Qt 6.11). This is the %1 version for computers that can't run it.").arg(QCoreApplication::applicationVersion()));
#endif
}

void ChromeExtensions::installFromWebStore(const QString& urlOrId, const QString& title, QWidget* parent) {
#ifdef PACITUM_CHROME_EXTENSIONS
    if (!m_profile || m_profile->isOffTheRecord()) { emit message(tr("Extensions can't be added in Guest mode")); return; }
    static const QRegularExpression idRe(QStringLiteral("^[a-p]{32}$"));
    const QString id = idRe.match(urlOrId).hasMatch() ? urlOrId : webStoreId(QUrl(urlOrId));
    if (id.isEmpty()) return;
    QString name = title;
    name.remove(QRegularExpression(QStringLiteral("\\s*[-–—|]\\s*Chrome Web Store\\s*$"), QRegularExpression::CaseInsensitiveOption));
    if (name.trimmed().isEmpty()) name = id;
    if (QMessageBox::question(parent, tr("Add to Pacitum"),
            tr("Add \"%1\" from the Chrome Web Store?\n\nAn extension can read and change the pages you visit. Pacitum shows what it may do once it's added.").arg(name)) != QMessageBox::Yes) return;
    // The store's own download link, the same one Chrome uses.
    QUrl u(QStringLiteral("https://clients2.google.com/service/update2/crx"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("response"), QStringLiteral("redirect"));
    q.addQueryItem(QStringLiteral("prodversion"), QString::fromLatin1(qWebEngineChromiumVersion()));
    q.addQueryItem(QStringLiteral("acceptformat"), QStringLiteral("crx2,crx3"));
    q.addQueryItem(QStringLiteral("x"), QStringLiteral("id=%1&uc").arg(id));
    u.setQuery(q);
    QNetworkRequest r(u);
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setRawHeader("User-Agent", m_profile->httpUserAgent().toUtf8());
    r.setTransferTimeout(60000);
    emit message(tr("Downloading %1…").arg(name));
    auto* reply = m_net->get(r);
    QPointer<QWidget> guard(parent);
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, name, guard]{
        reply->deleteLater();
        // Only over HTTPS from Google (the redirect lands on a googleusercontent.com host).
        const QUrl final = reply->url();
        if (reply->error() != QNetworkReply::NoError || final.scheme() != QLatin1String("https")
            || !(final.host().endsWith(QStringLiteral(".google.com")) || final.host().endsWith(QStringLiteral(".googleusercontent.com")) || final.host() == QLatin1String("google.com"))) {
            emit message(tr("Couldn't download %1: %2").arg(name, reply->errorString()));
            return;
        }
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/PacitumExtensions");
        QDir().mkpath(dir);
        const QString crx = dir + QLatin1Char('/') + id + QStringLiteral(".crx");
        QFile f(crx);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { emit message(tr("Can't write %1").arg(crx)); return; }
        f.write(reply->readAll()); f.close();
        QString error;
        const QString zip = crxToZip(crx, &error);
        QFile::remove(crx);
        if (zip.isEmpty()) { emit message(error); return; }
        if (m_profile) m_profile->extensionManager()->installExtension(zip);
    });
#else
    Q_UNUSED(urlOrId); Q_UNUSED(title);
    installFromFile(QString(), parent);   // explains that 1.9 is needed
#endif
}

void ChromeExtensions::setEnabled(const QString& id, bool on) {
#ifdef PACITUM_CHROME_EXTENSIONS
    if (!m_profile || !m_profile->extensionManager()) return;
    for (const auto& e : m_profile->extensionManager()->extensions())
        if (e.id() == id) { m_profile->extensionManager()->setExtensionEnabled(e, on); break; }
    emit changed();
#else
    Q_UNUSED(id); Q_UNUSED(on);
#endif
}

void ChromeExtensions::uninstall(const QString& id) {
#ifdef PACITUM_CHROME_EXTENSIONS
    if (!m_profile || !m_profile->extensionManager()) return;
    for (const auto& e : m_profile->extensionManager()->extensions())
        if (e.id() == id) { m_profile->extensionManager()->uninstallExtension(e); break; }
#else
    Q_UNUSED(id);
#endif
}

bool ChromeExtensions::pinned(const QString& id) const {
    return AppSettings::get(QStringLiteral("pinned_extensions")).toArray().contains(id);
}

void ChromeExtensions::setPinned(const QString& id, bool on) {
    QJsonArray a = AppSettings::get(QStringLiteral("pinned_extensions")).toArray();
    const bool has = a.contains(id);
    if (on == has) return;
    if (on) a.append(id);
    else for (int i = a.size() - 1; i >= 0; --i) if (a.at(i).toString() == id) a.removeAt(i);
    AppSettings::set(QStringLiteral("pinned_extensions"), a);
    emit changed();
}

QString ChromeExtensions::webStoreId(const QUrl& url) {
    const QString host = url.host().toLower();
    const bool store = host == QLatin1String("chromewebstore.google.com")
        || (host == QLatin1String("chrome.google.com") && url.path().startsWith(QStringLiteral("/webstore/")));
    if (!store || !url.path().contains(QStringLiteral("/detail/"))) return {};
    static const QRegularExpression idRe(QStringLiteral("^[a-p]{32}$"));
    const QStringList parts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (auto it = parts.crbegin(); it != parts.crend(); ++it) if (idRe.match(*it).hasMatch()) return *it;
    return {};
}

// ------------------------------------------------------------------------------------- pop-up
namespace {
// The extension's popup.html in a small frameless window under its toolbar button. It grows to
// the page's size (Chrome caps pop-ups at 800 × 600) and closes when focus leaves it.
class ExtensionPopup : public QWidget {
public:
    ExtensionPopup(QWebEngineProfile* profile, const QUrl& url, QWidget* anchor)
        : QWidget(anchor ? anchor->window() : nullptr, Qt::Popup | Qt::FramelessWindowHint) {
        setAttribute(Qt::WA_DeleteOnClose);
        const auto& p = ThemeManager::current();
        setStyleSheet(QStringLiteral("ExtensionPopup, QWidget#ExtPopup{background:%1;border:1px solid %2;}").arg(p.toolbar, p.borderStrong));
        setObjectName(QStringLiteral("ExtPopup"));
        auto* l = new QVBoxLayout(this); l->setContentsMargins(1, 1, 1, 1);
        m_view = new QWebEngineView(this);
        auto* page = new QWebEnginePage(profile, m_view);
        m_view->setPage(page);
        l->addWidget(m_view);
        resize(360, 240);
        connect(page, &QWebEnginePage::contentsSizeChanged, this, [this](const QSizeF& s){
            const QSize want(qBound(200, qCeil(s.width()) + 2, 800), qBound(60, qCeil(s.height()) + 2, 600));
            if (want != size()) { resize(want); place(); }
        });
        connect(page, &QWebEnginePage::windowCloseRequested, this, &QWidget::close);   // window.close() in the popup
        m_view->load(url);
        m_anchor = anchor;
        place();
        setWindowOpacity(0.0);
        show();
        auto* a = new QVariantAnimation(this);   // short fade + drop, like the other menus
        a->setStartValue(0.0); a->setEndValue(1.0); a->setDuration(Pacitum::motionMs(160) > 0 ? 160 : 1);
        a->setEasingCurve(QEasingCurve::OutCubic);
        const QPoint end = pos();
        connect(a, &QVariantAnimation::valueChanged, this, [this, end](const QVariant& v){
            const double t = v.toDouble(); setWindowOpacity(t); move(end - QPoint(0, qRound(6 * (1 - t))));
        });
        a->start(QAbstractAnimation::DeleteWhenStopped);
    }
private:
    void place() {
        if (!m_anchor) return;
        QPoint at = m_anchor->mapToGlobal(QPoint(m_anchor->width() - width(), m_anchor->height() + 6));
        if (const QScreen* s = m_anchor->screen()) {
            const QRect a = s->availableGeometry();
            at.setX(qBound(a.left() + 4, at.x(), a.right() - width() - 4));
            at.setY(qMin(at.y(), a.bottom() - height() - 4));
        }
        move(at);
    }
    QWebEngineView* m_view = nullptr;
    QPointer<QWidget> m_anchor;
};
}

void ChromeExtensions::openPopup(const QString& id, QWidget* anchor) {
    for (const auto& it : items()) {
        if (it.id != id) continue;
        if (!it.enabled) { emit message(tr("%1 is turned off").arg(it.name)); return; }
        if (it.popupUrl.isValid() && !it.popupUrl.isEmpty()) new ExtensionPopup(m_profile, it.popupUrl, anchor);
        else if (it.optionsUrl.isValid()) emit message(tr("%1 has no pop-up; open its options from the extensions menu").arg(it.name));
        else emit message(tr("%1 works on its own and has no pop-up").arg(it.name));
        return;
    }
}
