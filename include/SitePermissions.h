#pragma once
// Pacitum 1.7.5 site permissions: the per-profile decision store (permissions-spec.md §11).
//
// Qt WebEngine only persists Notifications / Geolocation / Clipboard / LocalFonts and forgets
// media + desktop capture on every navigation. Pacitum therefore runs its profiles with
// PersistentPermissionsPolicy::AskEveryTime and keeps *every* decision here instead: one JSON
// file per profile (next to that profile's Chromium storage), or memory only for Guest and
// private tabs. Keys are origins (scheme://host[:port]); nothing is ever shared across profiles.
#include <QObject>
#include <QString>
#include <QUrl>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QDateTime>
class QWebEngineProfile;

namespace Perm {

enum class Kind : int {
    Camera, Microphone, Location, Notifications, Clipboard, Fonts, ScreenShare,
    Downloads, Popups, Storage, Usb, Bluetooth, Serial, Hid, Midi, MouseLock, Count
};
enum class Decision : int { Ask, Allow, Block };

QString key(Kind k);                 // stable id used in JSON ("camera", "usb", ...)
bool fromKey(const QString& s, Kind* out);
QString title(Kind k);               // "Camera"
QString glyph(Kind k);               // icon name in :/icons/ui
QString blockedGlyph(Kind k);        // crossed glyph for the blocked chip (§6)
QString capability(Kind k);          // "Use your camera" (prompt row label, §1 table)
QString blockVerb(Kind k);           // "Don't allow sites to use your camera"
QList<Kind> settingsKinds();         // order of the Settings → Permissions card (§8)
bool isDevice(Kind k);               // USB / Bluetooth / Serial / HID (chooser UI)
bool isTwoState(Kind k);             // Allow/Block only, no "this time" (downloads, storage, popups)
Decision builtinDefault(Kind k);

QString originOf(const QUrl& url);   // "https://meet.example.com" (empty for opaque / internal)
QString hostOf(const QString& origin);
// eTLD+1 heuristic (public-suffix lite): "a.b.example.co.uk" -> "example.co.uk".
QString registrableDomain(const QString& host);
// Rich text "sub.<b>example.com</b>" for the prompt header (§1: eTLD+1 bold, subdomain regular).
QString richHost(const QString& host, int maxChars = 42);

class Store : public QObject {
    Q_OBJECT
public:
    // One store per QWebEngineProfile: disk-backed for persistent profiles, memory for OTR ones.
    static Store* forProfile(QWebEngineProfile* profile);
    // Test / guest helper: explicit file ("" = memory only).
    explicit Store(const QString& filePath, QObject* parent = nullptr);

    Decision decision(const QString& origin, Kind k) const;          // explicit per-site value or Ask
    Decision effective(const QString& origin, Kind k) const;         // site value, else the default
    bool hasSiteValue(const QString& origin, Kind k) const;
    void setDecision(const QString& origin, Kind k, Decision d);     // Ask removes the entry
    void resetOrigin(const QString& origin);
    int siteValueCount(const QString& origin) const;

    Decision defaultFor(Kind k) const;
    bool hasExplicitDefault(Kind k) const;
    void setDefault(Kind k, Decision d);
    bool quietNotifications() const;                                  // §4 "Use quieter messaging"
    void setQuietNotifications(bool on);

    QStringList origins() const;                                      // every origin with any value
    QStringList originsWith(Kind k, Decision d) const;

    // Device grants (USB/BT/serial/HID): a per-origin list of device descriptors the user picked.
    QJsonArray devices(const QString& origin, Kind k) const;
    void grantDevice(const QString& origin, Kind k, const QJsonObject& dev);
    void revokeDevices(const QString& origin, Kind k);
    void revokeDevice(const QString& origin, Kind k, const QString& deviceId);
    bool hasDevice(const QString& origin, Kind k, const QString& deviceId) const;

    // Remembered media device choice per site (camera/mic picker §2 "choices persist per site").
    QString mediaDevice(const QString& origin, Kind k) const;
    void setMediaDevice(const QString& origin, Kind k, const QString& id);

    // Notification prompts blocked/dismissed (any site) within 28 days (§4 trigger).
    void noteNotificationRejected();
    int recentNotificationRejections() const;

    // Recent activity (§8 card): last time an origin asked for / used something.
    void touch(const QString& origin);
    QStringList recentOrigins(int max = 4) const;
    QString summary(const QString& origin) const;                     // "Camera, Microphone allowed · Notifications blocked"

    QString filePath() const { return m_path; }
    bool isPersistent() const { return !m_path.isEmpty(); }
    void flush();                                                     // write now (also done lazily)

signals:
    void changed(const QString& origin);

private:
    void load();
    void scheduleSave();
    QJsonObject site(const QString& origin) const;
    void putSite(const QString& origin, const QJsonObject& o);
    QString m_path;
    QJsonObject m_root;   // {version, defaults{}, quiet, sites{origin:{perms{},devices{},media{},seen}}, rejections[]}
    bool m_dirty = false;
};

// Self-test for CI (--permissions-test): store scoping, persistence, eTLD+1, defaults.
int runStoreSelfTest(QStringList* log);
}
