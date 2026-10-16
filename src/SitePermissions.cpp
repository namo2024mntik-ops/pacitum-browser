#include "SitePermissions.h"
#include <QWebEngineProfile>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QPointer>
#include <QTemporaryDir>
#include <algorithm>

namespace Perm {

struct Info { const char* key; const char* title; const char* glyph; const char* off; const char* cap; const char* block; };
static const Info kInfo[] = {
    {"camera", "Camera", "camera", "camera-off", "Use your camera", "Don't allow sites to use your camera"},
    {"microphone", "Microphone", "microphone", "microphone-off", "Use your microphone", "Don't allow sites to use your microphone"},
    {"location", "Location", "location", "location-off", "Know your location", "Don't allow sites to see your location"},
    {"notifications", "Notifications", "notifications", "notifications-off", "Show notifications", "Don't allow sites to send notifications"},
    {"clipboard", "Clipboard", "clipboard", "permission-blocked", "See text and images copied to the clipboard", "Don't allow sites to see your clipboard"},
    {"fonts", "Fonts", "font", "permission-blocked", "Use your fonts", "Don't allow sites to use your fonts"},
    {"screen-share", "Screen sharing", "screen-share", "permission-blocked", "Share your screen", "Don't allow sites to ask to share your screen"},
    {"downloads", "Automatic downloads", "downloads-multiple", "permission-blocked", "Download multiple files", "Don't allow sites to download multiple files automatically"},
    {"popups", "Pop-ups and redirects", "popup-blocked", "popup-blocked", "Open pop-ups and redirects", "Don't allow sites to send pop-ups or use redirects"},
    {"storage", "Persistent storage", "storage", "permission-blocked", "Store data on this device permanently", "Don't allow sites to store data permanently"},
    {"usb", "USB devices", "usb", "permission-blocked", "Connect to a USB device", "Don't allow sites to connect to USB devices"},
    {"bluetooth", "Bluetooth devices", "bluetooth", "permission-blocked", "Pair a Bluetooth device", "Don't allow sites to connect to Bluetooth devices"},
    {"serial", "Serial ports", "serial-port", "permission-blocked", "Connect to a serial port", "Don't allow sites to connect to serial ports"},
    {"hid", "HID devices", "usb", "permission-blocked", "Connect to a HID device", "Don't allow sites to connect to HID devices"},
    {"midi", "MIDI devices", "midi", "permission-blocked", "Use your MIDI devices", "Don't allow sites to use MIDI devices"},
    {"mouselock", "Mouse lock", "mouse", "permission-blocked", "Lock and use your mouse", "Don't allow sites to lock your mouse"},
};
static const Info& info(Kind k) { return kInfo[qBound(0, int(k), int(Kind::Count) - 1)]; }

QString key(Kind k) { return QString::fromLatin1(info(k).key); }
bool fromKey(const QString& s, Kind* out) {
    for (int i = 0; i < int(Kind::Count); ++i) if (s == QLatin1String(kInfo[i].key)) { if (out) *out = Kind(i); return true; }
    return false;
}
QString title(Kind k) { return QString::fromLatin1(info(k).title); }
QString glyph(Kind k) { return QString::fromLatin1(info(k).glyph); }
QString blockedGlyph(Kind k) { return QString::fromLatin1(info(k).off); }
QString capability(Kind k) { return QString::fromLatin1(info(k).cap); }
QString blockVerb(Kind k) { return QString::fromLatin1(info(k).block); }
QList<Kind> settingsKinds() {
    return {Kind::Camera, Kind::Microphone, Kind::Location, Kind::Notifications, Kind::Clipboard, Kind::ScreenShare,
            Kind::Downloads, Kind::Usb, Kind::Serial, Kind::Hid, Kind::Midi, Kind::Bluetooth, Kind::Popups, Kind::Storage, Kind::Fonts};
}
bool isDevice(Kind k) { return k == Kind::Usb || k == Kind::Bluetooth || k == Kind::Serial || k == Kind::Hid; }
bool isTwoState(Kind k) { return k == Kind::Downloads || k == Kind::Storage || k == Kind::Popups; }
Decision builtinDefault(Kind k) {
    switch (k) {
    case Kind::Popups: return Decision::Block;          // §6 pop-ups blocked by default
    case Kind::MouseLock: return Decision::Allow;       // §11: no bubble, Esc hint
    default: return Decision::Ask;
    }
}

QString originOf(const QUrl& u) {
    const QString s = u.scheme().toLower();
    if ((s != QLatin1String("http") && s != QLatin1String("https")) || u.host().isEmpty()) {
        if (s == QLatin1String("file")) return QStringLiteral("file://");
        return QString();
    }
    QString o = s + QStringLiteral("://") + u.host().toLower();
    const int port = u.port(-1);
    if (port > 0 && !((s == QLatin1String("http") && port == 80) || (s == QLatin1String("https") && port == 443)))
        o += QLatin1Char(':') + QString::number(port);
    return o;
}
QString hostOf(const QString& origin) { const QUrl u(origin); return u.host().isEmpty() ? origin : u.host(); }

QString registrableDomain(const QString& rawHost) {
    const QString host = rawHost.toLower();
    const QStringList parts = host.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (parts.size() <= 2) return host;
    bool numeric = true; for (const QString& p : parts) { bool ok; p.toInt(&ok); numeric &= ok; }
    if (numeric) return host;   // IPv4
    // Common two-level public suffixes (co.uk, com.au, ...). A full PSL is not shipped; this only
    // affects how the host is *displayed* (bold part), never how permissions are keyed (origins).
    static const QStringList second = {QStringLiteral("co"), QStringLiteral("com"), QStringLiteral("net"), QStringLiteral("org"), QStringLiteral("gov"),
                                       QStringLiteral("edu"), QStringLiteral("ac"), QStringLiteral("or"), QStringLiteral("ne"), QStringLiteral("go")};
    const QString tld = parts.last(), sld = parts.at(parts.size() - 2);
    if (tld.size() == 2 && second.contains(sld)) return parts.mid(parts.size() - 3).join(QLatin1Char('.'));
    if (sld == QLatin1String("github") && tld == QLatin1String("io")) return parts.mid(parts.size() - 3).join(QLatin1Char('.'));
    return parts.mid(parts.size() - 2).join(QLatin1Char('.'));
}

QString richHost(const QString& host, int maxChars) {
    const QString reg = registrableDomain(host);
    QString sub = host.left(host.size() - reg.size());
    // Middle-elide the subdomain, always keep the eTLD+1 (§1 header row).
    if (host.size() > maxChars && sub.size() > 4) {
        const int keep = qMax(2, maxChars - reg.size() - 1);
        sub = sub.left(keep / 2) + QChar(0x2026) + sub.right(keep - keep / 2);
    }
    return sub.toHtmlEscaped() + QStringLiteral("<b>") + reg.toHtmlEscaped() + QStringLiteral("</b>");
}

// ---------------------------------------------------------------- Store
Store* Store::forProfile(QWebEngineProfile* profile) {
    static QHash<QWebEngineProfile*, QPointer<Store>> stores;
    if (!profile) { static Store* mem = new Store(QString()); return mem; }
    if (auto s = stores.value(profile)) return s;
    QString path;
    if (!profile->isOffTheRecord() && !profile->persistentStoragePath().isEmpty())
        path = QDir(profile->persistentStoragePath()).filePath(QStringLiteral("pacitum-site-permissions.json"));
    auto* s = new Store(path, profile);   // dies with its profile
    stores.insert(profile, s);
    return s;
}

Store::Store(const QString& filePath, QObject* parent) : QObject(parent), m_path(filePath) { load(); }

void Store::load() {
    m_root = QJsonObject{{QStringLiteral("version"), 1}};
    if (m_path.isEmpty()) return;
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isObject() && doc.object().value(QStringLiteral("version")).toInt() >= 1) m_root = doc.object();
}

void Store::scheduleSave() {
    if (m_path.isEmpty()) return;
    if (m_dirty) return;
    m_dirty = true;
    QTimer::singleShot(300, this, &Store::flush);
}

void Store::flush() {
    if (m_path.isEmpty()) { m_dirty = false; return; }
    m_dirty = false;
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(m_root).toJson(QJsonDocument::Indented));
    f.commit();
}

QJsonObject Store::site(const QString& origin) const { return m_root.value(QStringLiteral("sites")).toObject().value(origin).toObject(); }
void Store::putSite(const QString& origin, const QJsonObject& o) {
    QJsonObject sites = m_root.value(QStringLiteral("sites")).toObject();
    if (o.isEmpty()) sites.remove(origin); else sites.insert(origin, o);
    m_root.insert(QStringLiteral("sites"), sites);
    scheduleSave();
    emit changed(origin);
}

static Decision parseDecision(const QString& s) {
    if (s == QLatin1String("allow")) return Decision::Allow;
    if (s == QLatin1String("block")) return Decision::Block;
    return Decision::Ask;
}
static QString decisionStr(Decision d) { return d == Decision::Allow ? QStringLiteral("allow") : d == Decision::Block ? QStringLiteral("block") : QStringLiteral("ask"); }

Decision Store::decision(const QString& origin, Kind k) const {
    if (origin.isEmpty()) return Decision::Ask;
    return parseDecision(site(origin).value(QStringLiteral("perms")).toObject().value(key(k)).toString());
}
bool Store::hasSiteValue(const QString& origin, Kind k) const { return decision(origin, k) != Decision::Ask; }
Decision Store::effective(const QString& origin, Kind k) const {
    const Decision d = decision(origin, k);
    return d != Decision::Ask ? d : defaultFor(k);
}
void Store::setDecision(const QString& origin, Kind k, Decision d) {
    if (origin.isEmpty()) return;
    QJsonObject s = site(origin);
    QJsonObject p = s.value(QStringLiteral("perms")).toObject();
    if (d == Decision::Ask) p.remove(key(k)); else p.insert(key(k), decisionStr(d));
    if (p.isEmpty()) s.remove(QStringLiteral("perms")); else s.insert(QStringLiteral("perms"), p);
    if (d == Decision::Block && isDevice(k)) {   // blocking also forgets picked devices
        QJsonObject dv = s.value(QStringLiteral("devices")).toObject(); dv.remove(key(k));
        if (dv.isEmpty()) s.remove(QStringLiteral("devices")); else s.insert(QStringLiteral("devices"), dv);
    }
    s.insert(QStringLiteral("seen"), QDateTime::currentSecsSinceEpoch());
    putSite(origin, s);
}
void Store::resetOrigin(const QString& origin) { putSite(origin, {}); }
int Store::siteValueCount(const QString& origin) const {
    const QJsonObject s = site(origin);
    return s.value(QStringLiteral("perms")).toObject().size() + s.value(QStringLiteral("devices")).toObject().size();
}

Decision Store::defaultFor(Kind k) const {
    const QString v = m_root.value(QStringLiteral("defaults")).toObject().value(key(k)).toString();
    return v.isEmpty() ? builtinDefault(k) : parseDecision(v);
}
bool Store::hasExplicitDefault(Kind k) const { return m_root.value(QStringLiteral("defaults")).toObject().contains(key(k)); }
void Store::setDefault(Kind k, Decision d) {
    QJsonObject o = m_root.value(QStringLiteral("defaults")).toObject();
    o.insert(key(k), decisionStr(d));
    m_root.insert(QStringLiteral("defaults"), o);
    scheduleSave();
    emit changed(QString());
}
bool Store::quietNotifications() const { return m_root.value(QStringLiteral("quiet")).toBool(true); }
void Store::setQuietNotifications(bool on) { m_root.insert(QStringLiteral("quiet"), on); scheduleSave(); emit changed(QString()); }

QStringList Store::origins() const { return m_root.value(QStringLiteral("sites")).toObject().keys(); }
QStringList Store::originsWith(Kind k, Decision d) const {
    QStringList out;
    for (const QString& o : origins()) if (decision(o, k) == d) out << o;
    return out;
}

QJsonArray Store::devices(const QString& origin, Kind k) const {
    return site(origin).value(QStringLiteral("devices")).toObject().value(key(k)).toArray();
}
void Store::grantDevice(const QString& origin, Kind k, const QJsonObject& dev) {
    if (origin.isEmpty() || !isDevice(k)) return;
    const QString id = dev.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || hasDevice(origin, k, id)) return;
    QJsonObject s = site(origin);
    QJsonObject dv = s.value(QStringLiteral("devices")).toObject();
    QJsonArray arr = dv.value(key(k)).toArray(); arr.append(dev);
    dv.insert(key(k), arr); s.insert(QStringLiteral("devices"), dv);
    s.insert(QStringLiteral("seen"), QDateTime::currentSecsSinceEpoch());
    putSite(origin, s);
}
void Store::revokeDevices(const QString& origin, Kind k) {
    QJsonObject s = site(origin);
    QJsonObject dv = s.value(QStringLiteral("devices")).toObject(); dv.remove(key(k));
    if (dv.isEmpty()) s.remove(QStringLiteral("devices")); else s.insert(QStringLiteral("devices"), dv);
    putSite(origin, s);
}
void Store::revokeDevice(const QString& origin, Kind k, const QString& id) {
    QJsonArray keep;
    for (const auto& v : devices(origin, k)) if (v.toObject().value(QStringLiteral("id")).toString() != id) keep.append(v);
    QJsonObject s = site(origin); QJsonObject dv = s.value(QStringLiteral("devices")).toObject();
    if (keep.isEmpty()) dv.remove(key(k)); else dv.insert(key(k), keep);
    if (dv.isEmpty()) s.remove(QStringLiteral("devices")); else s.insert(QStringLiteral("devices"), dv);
    putSite(origin, s);
}
bool Store::hasDevice(const QString& origin, Kind k, const QString& id) const {
    for (const auto& v : devices(origin, k)) if (v.toObject().value(QStringLiteral("id")).toString() == id) return true;
    return false;
}

QString Store::mediaDevice(const QString& origin, Kind k) const { return site(origin).value(QStringLiteral("media")).toObject().value(key(k)).toString(); }
void Store::setMediaDevice(const QString& origin, Kind k, const QString& id) {
    QJsonObject s = site(origin); QJsonObject m = s.value(QStringLiteral("media")).toObject();
    m.insert(key(k), id); s.insert(QStringLiteral("media"), m); putSite(origin, s);
}

void Store::noteNotificationRejected() {
    QJsonArray a = m_root.value(QStringLiteral("rejections")).toArray();
    a.append(QDateTime::currentSecsSinceEpoch());
    while (a.size() > 20) a.removeFirst();
    m_root.insert(QStringLiteral("rejections"), a);
    scheduleSave();
}
int Store::recentNotificationRejections() const {
    const qint64 cutoff = QDateTime::currentSecsSinceEpoch() - 28LL * 24 * 3600;
    int n = 0;
    for (const auto& v : m_root.value(QStringLiteral("rejections")).toArray()) if (v.toInteger() >= cutoff) ++n;
    return n;
}

void Store::touch(const QString& origin) {
    if (origin.isEmpty()) return;
    QJsonObject s = site(origin);
    s.insert(QStringLiteral("seen"), QDateTime::currentSecsSinceEpoch());
    putSite(origin, s);
}
QStringList Store::recentOrigins(int max) const {
    QStringList o = origins();
    std::sort(o.begin(), o.end(), [this](const QString& a, const QString& b){
        return site(a).value(QStringLiteral("seen")).toInteger() > site(b).value(QStringLiteral("seen")).toInteger(); });
    QStringList out;
    for (const QString& x : o) if (siteValueCount(x) > 0) { out << x; if (out.size() >= max) break; }
    return out;
}
QString Store::summary(const QString& origin) const {
    QStringList allowed, blocked;
    for (int i = 0; i < int(Kind::Count); ++i) {
        const Decision d = decision(origin, Kind(i));
        if (d == Decision::Allow) allowed << title(Kind(i)); else if (d == Decision::Block) blocked << title(Kind(i));
    }
    for (Kind k : {Kind::Usb, Kind::Bluetooth, Kind::Serial, Kind::Hid}) {
        const int n = devices(origin, k).size();
        if (n && decision(origin, k) != Decision::Allow) allowed << QStringLiteral("%1 (%2)").arg(title(k)).arg(n);
    }
    QStringList parts;
    if (!allowed.isEmpty()) parts << allowed.join(QStringLiteral(", ")) + QStringLiteral(" allowed");
    if (!blocked.isEmpty()) parts << blocked.join(QStringLiteral(", ")) + QStringLiteral(" blocked");
    return parts.isEmpty() ? QStringLiteral("No permissions changed") : parts.join(QStringLiteral(" · "));
}

// ---------------------------------------------------------------- self-test
int runStoreSelfTest(QStringList* log) {
    int fails = 0;
    auto check = [&](bool ok, const QString& what){ if (log) *log << QStringLiteral("  %1 %2").arg(ok ? QStringLiteral("ok  ") : QStringLiteral("FAIL"), what); if (!ok) ++fails; };
    check(originOf(QUrl(QStringLiteral("https://Meet.Example.com:443/a?b"))) == QLatin1String("https://meet.example.com"), "origin: default port dropped, host lowercased");
    check(originOf(QUrl(QStringLiteral("http://localhost:8080/x"))) == QLatin1String("http://localhost:8080"), "origin keeps custom port");
    check(originOf(QUrl(QStringLiteral("pacitum://settings"))).isEmpty(), "internal pages have no origin");
    check(originOf(QUrl(QStringLiteral("http://a.com"))) != originOf(QUrl(QStringLiteral("https://a.com"))), "scheme is part of the origin");
    check(registrableDomain(QStringLiteral("a.b.example.co.uk")) == QLatin1String("example.co.uk"), "eTLD+1 co.uk");
    check(registrableDomain(QStringLiteral("meet.example.com")) == QLatin1String("example.com"), "eTLD+1 .com");
    check(registrableDomain(QStringLiteral("192.168.1.10")) == QLatin1String("192.168.1.10"), "IPv4 kept whole");
    check(richHost(QStringLiteral("meet.example.com")) == QLatin1String("meet.<b>example.com</b>"), "rich host bolds eTLD+1");
    QTemporaryDir tmp;
    const QString pa = tmp.filePath(QStringLiteral("a/perm.json")), pb = tmp.filePath(QStringLiteral("b/perm.json"));
    const QString o = QStringLiteral("https://meet.example.com");
    {
        Store a(pa), b(pb), mem{QString()};
        check(a.effective(o, Kind::Camera) == Decision::Ask, "camera defaults to Ask");
        check(a.effective(o, Kind::Popups) == Decision::Block, "pop-ups default to Block");
        a.setDecision(o, Kind::Camera, Decision::Allow);
        a.setDecision(o, Kind::Microphone, Decision::Block);
        a.setDecision(o, Kind::ScreenShare, Decision::Allow);
        a.grantDevice(o, Kind::Usb, QJsonObject{{QStringLiteral("id"), QStringLiteral("usb:1234:5678:SN1")}, {QStringLiteral("name"), QStringLiteral("Board")}});
        a.grantDevice(o, Kind::Usb, QJsonObject{{QStringLiteral("id"), QStringLiteral("usb:1234:5678:SN1")}});
        check(a.devices(o, Kind::Usb).size() == 1, "device grant is de-duplicated");
        check(!a.hasDevice(QStringLiteral("https://evil.example"), Kind::Usb, QStringLiteral("usb:1234:5678:SN1")), "device grant scoped to its origin");
        check(!a.hasDevice(o, Kind::Serial, QStringLiteral("usb:1234:5678:SN1")), "device grant scoped to its kind");
        check(b.decision(o, Kind::Camera) == Decision::Ask, "second profile does not see the first profile's grant");
        check(a.decision(QStringLiteral("https://other.example.com"), Kind::Camera) == Decision::Ask, "grant does not leak to sibling subdomain");
        mem.setDecision(o, Kind::Location, Decision::Allow);
        check(!mem.isPersistent(), "guest / private store is memory only");
        a.flush();
    }
    {
        Store a(pa);
        check(a.decision(o, Kind::Camera) == Decision::Allow, "camera Allow persisted to disk");
        check(a.decision(o, Kind::Microphone) == Decision::Block, "microphone Block persisted");
        check(a.decision(o, Kind::ScreenShare) == Decision::Allow, "screen-share decision persisted");
        check(a.hasDevice(o, Kind::Usb, QStringLiteral("usb:1234:5678:SN1")), "USB device grant persisted");
        check(a.summary(o).contains(QStringLiteral("Camera")) && a.summary(o).contains(QStringLiteral("blocked")), "summary text");
        a.setDecision(o, Kind::Usb, Decision::Block);
        check(a.devices(o, Kind::Usb).isEmpty(), "blocking USB forgets picked devices");
        a.setDefault(Kind::Location, Decision::Block);
        check(a.effective(QStringLiteral("https://x.test"), Kind::Location) == Decision::Block, "default applies to sites without a value");
        a.resetOrigin(o);
        check(a.siteValueCount(o) == 0, "reset clears the origin");
        for (int i = 0; i < 3; ++i) a.noteNotificationRejected();
        check(a.recentNotificationRejections() >= 3, "notification rejections counted (quiet chip trigger)");
        a.flush();
    }
    { Store none{QString()}; check(none.origins().isEmpty(), "memory store starts empty"); }
    return fails;
}

}
