#pragma once
// 1.8.8 Google Drive sync.
//
// Sign-in: OAuth 2.0 for desktop apps (system browser + loopback redirect on 127.0.0.1, PKCE).
// Storage: one file, pacitum-sync.json, in the user's Drive "appDataFolder" (hidden, app-only).
// Merge: three-way against the last synced copy (sync-base.json), so a deletion on one computer
// is told apart from an addition on another. Bookmarks, settings and notes follow deletions;
// history only grows (newest 500 pages).
// Files (in the profile folder): sync-account.json (refresh token, account, choices),
// sync-base.json (last merged payload).
// The OAuth client comes from the build (-DPACITUM_GOOGLE_CLIENT_ID=… / _SECRET=…), the
// environment (PACITUM_GOOGLE_CLIENT_ID / _SECRET) or Settings › Sync › Advanced.
#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QImage>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;
class HistoryManager;

class SyncManager : public QObject {
    Q_OBJECT
public:
    static SyncManager* instance();

    struct Account { QString email, name, picture; };
    bool hasClient() const;                 // an OAuth client id is configured
    bool signedIn() const { return !m_refreshToken.isEmpty(); }
    Account account() const { return m_account; }
    QDateTime lastSync() const { return m_lastSync; }
    QString status() const { return m_status; }
    bool busy() const { return m_busy; }
    bool syncs(const QString& kind) const;  // "bookmarks" | "settings" | "notes" | "history"
    void setSyncs(const QString& kind, bool on);
    void setClient(const QString& id, const QString& secret);   // Settings › Advanced
    QString clientId() const;

    void setHistory(HistoryManager* h) { m_history = h; }   // the window's history (for read/import)
    HistoryManager* history() const { return m_history; }

    void signIn();
    void signOut();
    void syncNow();
    void fetchPhoto(std::function<void(const QImage&)> done);   // the account's Google photo

    // Pure merge (unit-tested): base = last synced payload, local = this computer, remote = Drive.
    static QJsonObject merge(const QJsonObject& base, const QJsonObject& local, const QJsonObject& remote, const QString& remoteDevice);

signals:
    void changed();                       // sign-in state / status / last sync changed
    void aboutToCollect();                // flush pending writes (notes, history) before reading files
    void applied(const QStringList& kinds);   // local files were updated from the merge

private:
    explicit SyncManager(QObject* parent = nullptr);
    void loadAccount();
    void saveAccount() const;
    void setStatus(const QString& s);
    void withToken(std::function<void(bool ok)> next);
    void exchangeCode(const QString& code, const QString& redirect, const QString& verifier);
    void fetchAccount();
    QNetworkReply* api(const QByteArray& verb, const QUrl& url, const QByteArray& body = {}, const QByteArray& type = {});
    QJsonObject collectLocal();
    QStringList applyLocal(const QJsonObject& merged, const QJsonObject& local);
    void finish(bool ok, const QString& message);

    QNetworkAccessManager* m_net = nullptr;
    QPointer<QTcpServer> m_loopback;
    HistoryManager* m_history = nullptr;
    QTimer m_timer;
    QString m_refreshToken, m_accessToken, m_status;
    QDateTime m_tokenExpiry, m_lastSync;
    Account m_account;
    QJsonObject m_choices;
    bool m_busy = false;
};
