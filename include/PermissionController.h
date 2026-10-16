#pragma once
// Per-tab permission logic (permissions-spec.md §1-§11): Qt permission requests, the Pacitum
// store, the FIFO prompt queue, desktop capture, pop-up blocking, multiple-download throttling,
// the native device bridge for the JS shims, and in-use / blocked bookkeeping for the chrome.
#include <QObject>
#include <QPointer>
#include <QWebEnginePage>
#include <QWebEnginePermission>
#include <QWebEngineDesktopMediaRequest>
#include <QJsonObject>
#include <QDateTime>
#include <QSet>
#include <QHash>
#include <QIcon>
#include <functional>
#include <memory>
#include "SitePermissions.h"
#include "PermissionUi.h"
class QWebChannel;
class QWebEngineView;
class QWebEngineDownloadRequest;
class BluetoothScanner;
class QSerialPort;

class PermissionController;

// Exposed to the page's main world as "pacitumPermissions" (see resources/permissions/shim.js).
class PermissionBridge : public QObject {
    Q_OBJECT
public:
    explicit PermissionBridge(PermissionController* c);
    Q_INVOKABLE void request(int id, const QString& kind, const QString& json);
signals:
    void resolved(int id, const QString& json);
    void serialData(const QString& token, const QString& base64);
    void stopRequested(const QString& what, const QString& token);
private:
    PermissionController* m_c;
};


class PermissionController : public QObject {
    Q_OBJECT
public:
    PermissionController(QWidget* tab, QWebEngineView* view, QWebEnginePage* page);
    ~PermissionController() override;

    // ---- hooks set by the window
    std::function<QPoint()> anchor;                                         // global top-left for bubbles (§1 placement)
    std::function<bool()> canPrompt;                                        // tab active and window focused
    std::function<QList<PermUi::ScreenSharePicker::TabSource>()> shareableTabs;
    std::function<QIcon()> favicon;

    Perm::Store* store() const { return m_store; }
    QString origin() const;
    QWebChannel* channel() const { return m_channel; }
    QWebEnginePage* page() const { return m_page; }

    // ---- state for the chrome
    QList<OmniChip> chips() const;
    int tabUsage() const;                  // 0 none, 1 location, 2 screen, 3 recording (§5 precedence)
    bool capturing() const { return m_camera || m_mic; }
    bool sharingScreen() const { return m_screen; }
    QList<PermUi::SiteRow> siteRows() const;
    bool hasPending() const { return !m_queue.isEmpty(); }

    // ---- actions from the chrome
    void chipClicked(const QString& id, bool fromGesture = true);
    void focusPending();                   // F6 / Alt+Shift+A
    void showSiteControls(const QPoint& at);
    void stopCapture();
    void tabActivated();
    void collapse();                       // tab switched away: bubble → chip

    // ---- hooks from the tab
    bool allowPopup(QWebEnginePage::WebWindowType type);
    void popupBlocked(const QUrl& url);
    bool filterDownload(QWebEngineDownloadRequest* dl);   // false = cancel (held or blocked)
    void noteUserInput();
    bool recentGesture(int ms = 1000) const;

    // Bridge entry point (validated: nonce, origin)
    void bridgeRequest(int id, const QString& kind, const QJsonObject& opts);
    QString nonce() const { return m_nonce; }

    // Registry for tab sharing (all live tabs of this process)
    static QList<PermissionController*>& all();
    QString tabToken() const { return m_tabToken; }
    QPixmap grabFrame() const;

    // Keep Qt's in-memory permission state in sync with the Pacitum store (persistent types).
    static void syncQtPermissions(QWebEngineProfile* profile);
    static void installProfile(QWebEngineProfile* profile);

    // CI self-test hooks
    QStringList blockedPopupUrls() const { return m_blockedPopups; }
    int heldDownloads() const { return m_heldDownloads.size(); }
    static bool s_testAutoAnswer; static QString s_testAnswer;   // "", "allow", "once", "deny", "dismiss"

signals:
    void stateChanged();
    void siteSettingsRequested(const QString& origin);
    void mouseLockHint();
    void openUrlRequested(const QString& url);

private:
    struct Pending {
        enum Type { QtPerm, Device, Midi, Storage, Downloads, Desktop } type = QtPerm;
        QWebEnginePermission perm;
        Perm::Kind kind = Perm::Kind::Camera;
        bool both = false;                 // camera + microphone
        int reqId = 0;
        QJsonObject opts;
        QString origin;
        qint64 at = 0;
        bool quiet = false;
        bool osDenied = false;
    };
    void onPermission(QWebEnginePermission p);
    void onDesktopMedia(const QWebEngineDesktopMediaRequest& req);
    void enqueue(const Pending& p);
    void showHead(bool fromGesture);
    void answer(const Pending& p, const QString& choice);   // "always", "once", "deny", "dismiss", "timeout"
    void popHead();
    void reply(int id, const QJsonValue& value);
    void replyError(int id, const QString& name, const QString& message);
    void setInUse(Perm::Kind k, bool on);
    void onOriginChanged(const QString& from, const QString& to);
    void addBlocked(Perm::Kind k);
    void showBlockedPopover(Perm::Kind k);
    void showInUsePopover();
    void showPopupsBubble();
    void showOsDenied(Perm::Kind k);
    void ensureChannel();
    PermUi::Bubble* makeBubble(const QString& richQuestion, const QString& plain);
    void finishBubble();
    void expirePending();
    void releaseDownloads(bool allow);
    void serialOpen(int id, const QJsonObject& o);

    QPointer<QWidget> m_tab;
    QPointer<QWebEngineView> m_view;
    QPointer<QWebEnginePage> m_page;
    Perm::Store* m_store = nullptr;
    QWebChannel* m_channel = nullptr;
    PermissionBridge* m_bridge = nullptr;
    QString m_nonce, m_tabToken;
    QString m_origin;                      // origin of the committed document
    QList<Pending> m_queue;
    QPointer<PermUi::Bubble> m_bubble;
    QPointer<QWidget> m_popover;
    QSet<int> m_shownThisLoad;             // §1 "appears at most once per page load per permission"
    QHash<int, QString> m_session;         // kind -> origin: "Allow this time" grants
    QList<QWebEnginePermission> m_grantedMedia;   // stored to deny() on "Stop using…"
    bool m_camera = false, m_mic = false, m_screen = false, m_location = false;
    QDateTime m_captureSince;
    QSet<int> m_blocked;                   // kinds blocked on this page (§6 chip)
    QSet<int> m_osDenied;
    QStringList m_blockedPopups;
    qint64 m_lastInput = 0;
    // desktop capture
    QList<QWebEnginePermission> m_desktopPerms;
    qint64 m_desktopChosenAt = 0;
    bool m_desktopAllowed = false;
    QString m_pendingTabShare; qint64 m_pendingTabShareAt = 0;
    QSet<QString> m_activeTabShares;       // tokens this page may pull frames for
    QPointer<PermUi::ScreenSharePicker> m_picker;
    // downloads (§11): timestamps of recent downloads, held URLs
    QList<qint64> m_dlTimes;
    QList<QPair<QUrl, QString>> m_heldDownloads;
    int m_bypassDownloads = 0;
    // devices
    QPointer<BluetoothScanner> m_bt;
    QHash<QString, QSerialPort*> m_ports;
    QTimer* m_expiry = nullptr;
};
