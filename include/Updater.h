#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include <QPointer>
#include <QDialog>

class QNetworkAccessManager;
#include <QNetworkReply>
class QTimer;
class QFile;
class QLabel;
class QProgressBar;
class QPushButton;
class QTextBrowser;

// Built-in updater (1.5.2).
//
// Feed: any URL that returns a GitHub "release" JSON object (tag_name, name, body, html_url,
// assets[{name, size, browser_download_url}]). Default:
//   https://api.github.com/repos/namo2024mntik-ops/pacitum-browser/releases/latest
// Overrides (first wins): env PACITUM_UPDATE_FEED, setting "update_feed_url".
// Flow: check -> compare versions -> prompt with release notes -> download the Setup .exe with
// progress -> verify size (and SHA256 when a "<installer>.sha256" or "SHA256SUMS" asset exists) ->
// quit and run the Inno installer with /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /RELAUNCH=1
// (same AppId: in-place upgrade) -> the installer starts the new version.
struct UpdateInfo {
    QString version;        // "1.5.3"
    QString tag;            // "v1.5.3"
    QString name;
    QString notes;          // markdown
    QString pageUrl;
    QUrl installerUrl;
    QString installerName;
    qint64 installerSize = -1;
    QUrl shaUrl;            // optional
    bool shaIsSums = false; // SHA256SUMS (many lines) vs single "<hash>  <file>"
    bool valid() const { return !version.isEmpty() && installerUrl.isValid(); }
};

class Updater : public QObject {
    Q_OBJECT
public:
    explicit Updater(QObject* parent = nullptr);
    static Updater* instance();

    static QUrl feedUrl();
    static int compareVersions(const QString& a, const QString& b);   // <0, 0, >0
    static QString currentVersion();

    void check(bool userInitiated);
    void download(const UpdateInfo& info);
    void cancelDownload();
    bool installAndRelaunch();          // Windows: starts the installer helper and quits the app
    const UpdateInfo& latest() const { return m_latest; }
    QString downloadedPath() const { return m_file; }
    bool busy() const { return m_reply != nullptr; }

    void setAutoCheck(bool on);         // periodic background check (setting "auto_check_updates")
    QString lastStatus() const { return m_status; }

signals:
    void checking();
    void updateAvailable(const UpdateInfo& info, bool userInitiated);
    void upToDate(bool userInitiated);
    void checkFailed(const QString& error, bool userInitiated);
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(bool ok, const QString& error);
    void statusChanged(const QString& status);

private:
    void setStatus(const QString& s);
    void fetchSha();
    void verifyAndFinish(const QByteArray& expectedHex);
    QNetworkAccessManager* m_net = nullptr;
    QPointer<QNetworkReply> m_reply;
    QTimer* m_timer = nullptr;
    UpdateInfo m_latest;
    UpdateInfo m_pending;
    QFile* m_out = nullptr;
    QString m_file;
    QString m_status;
};

// Prompt: version, release notes, progress bar, Update now / Later / Skip this version.
class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    UpdateDialog(const UpdateInfo& info, QWidget* parent = nullptr);
private:
    void startDownload();
    UpdateInfo m_info;
    QLabel* m_status = nullptr;
    QProgressBar* m_bar = nullptr;
    QPushButton* m_update = nullptr;
    QPushButton* m_later = nullptr;
    QPushButton* m_skip = nullptr;
    QTextBrowser* m_notes = nullptr;
};
