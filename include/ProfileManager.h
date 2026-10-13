#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QJsonObject>
#include <QColor>
#include <QPixmap>

class QLocalServer;
class QLockFile;
class QTemporaryDir;

// Pacitum 1.6 profiles (home-profile-spec §2).
//
// Layout under the app data dir (AppLocalDataLocation):
//   profiles.json                 registry (atomic writes)
//   profiles/<id>/                one folder per profile: browser_settings.json, bookmarks.json,
//                                 browser_session.json, browser_history.db, browser_data/storage
//                                 (cookies, local storage, pacitum_downloads.db) and browser_data/cache
//
// Each process runs exactly one profile: main() picks it, then makes profiles/<id>/ the working
// directory, so every existing component (AppSettings, history, bookmarks, session, downloads,
// the QWebEngineProfile storage) is isolated per profile with no further changes. A second launch
// of a profile that is already open hands its URLs to the running process (QLocalServer) and exits,
// so two processes never share one Chromium storage folder. Guest is off-the-record with a
// throw-away working directory.
struct ProfileInfo {
    QString id, name, email, colour = QStringLiteral("#2DD4BF"), avatar = QStringLiteral("initial");
    qint64 created = 0, lastUsed = 0;
    bool isGuest = false;
    QJsonObject toJson() const;
    static ProfileInfo fromJson(const QJsonObject& o);
};

class ProfileManager : public QObject {
    Q_OBJECT
public:
    static ProfileManager* instance();
    static const QStringList& colours();      // the 8 profile colours
    static const QStringList& glyphs();       // avatar glyphs (first entry "initial")
    static QString defaultId() { return QStringLiteral("default"); }

    // Loads profiles.json from root (creating / migrating it if needed). Call before any WebEngine profile exists.
    bool init(const QString& root);
    QString root() const { return m_root; }
    QString profileDir(const QString& id) const;

    QList<ProfileInfo> profiles() const;     // registry order
    bool contains(const QString& id) const;
    ProfileInfo profile(const QString& id) const;
    QString lastUsed() const;
    bool showPickerOnStartup() const;
    void setShowPickerOnStartup(bool on);
    bool shouldShowPicker() const { return profiles().size() >= 2 && showPickerOnStartup(); }

    ProfileInfo create(const QString& name, const QString& colour, const QString& avatar);
    void update(const ProfileInfo& p);        // name / colour / avatar; notifies the running process
    bool remove(const QString& id, QString* error = nullptr);

    // ---- the profile of this process ----
    bool activate(const QString& id);        // false: already running elsewhere (caller should hand off)
    bool activateGuest();
    QString currentId() const { return m_current; }
    bool isGuest() const { return m_guest; }
    ProfileInfo current() const;
    bool isRunning(const QString& id) const;

    // Hand URLs / a focus request to the process running `id`. True if it accepted.
    bool handOff(const QString& id, const QStringList& urls) const;
    // Open a window in another profile (starts a process, or focuses the running one).
    void launch(const QString& id) const;
    void launchGuest() const;

    // Idempotent, interruption-safe copy of pre-1.6 single-profile data into profiles/default.
    // The old files are left untouched as a backup. Returns false only if copying failed.
    static bool migrateLegacy(const QString& root, QString* log = nullptr);

    static QPixmap avatarPixmap(const ProfileInfo& p, int size, qreal dpr, int ring = 0, const QString& gapColour = QString());
    static int runProfileTest();              // --profile-test (CI): prints PROFILES OK / PROFILES FAIL

signals:
    void activateRequested(const QStringList& urls);   // another launch of this profile
    void profilesChanged();                            // registry changed (names, colours, list)
    void currentProfileChanged();                      // our own profile was edited

private:
    explicit ProfileManager(QObject* parent = nullptr);
    QJsonObject readRegistry() const;
    bool writeRegistry(const QJsonObject& reg) const;
    void touchLastUsed(const QString& id);
    void startServer(const QString& id);
    QString serverName(const QString& id) const;
    void notify(const QString& id, const QByteArray& cmd) const;
    QString m_root, m_current;
    bool m_guest = false;
    QLocalServer* m_server = nullptr;
    QLockFile* m_lock = nullptr;
    QTemporaryDir* m_guestDir = nullptr;
    mutable QJsonObject m_cache; mutable qint64 m_cacheStamp = -1;
};
