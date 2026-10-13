#pragma once
#include <QWebEngineUrlRequestInterceptor>
#include <QString>
class QWebEngineProfile;
class QUrl;
class QWebEngineSettings;

namespace Pacitum {
// Standard Chrome-like UA / Accept-Language / client hints and sane defaults for a profile.
void configureProfile(QWebEngineProfile* profile, bool persistent);
// Apply user-controlled web settings (JS, popups) to a settings object.
void applyWebSettings(QWebEngineSettings* s);

// Disk-backed main profile (Qt 6's defaultProfile() is off-the-record, so cookies would not persist).
QWebEngineProfile* mainProfile();
// Guest window process: mainProfile() is off-the-record. Must be set before the first mainProfile() call.
void setGuestMode(bool on);
bool guestMode();

// The unmodified QtWebEngine UA (captured before any change).
QString defaultUserAgent();
bool isGoogleSignInHost(const QString& host);
// Google's "Couldn't sign you in / browser may not be secure" rejection page.
bool isGoogleSignInBlocked(const QUrl& url);
// Per-page interceptor: sends the honest default UA to Google sign-in hosts.
class GoogleSignInInterceptor : public QWebEngineUrlRequestInterceptor {
public:
    explicit GoogleSignInInterceptor(QObject* parent = nullptr);
    void interceptRequest(QWebEngineUrlRequestInfo& info) override;
};

struct WebPrefs { bool javascript = true; bool popups = false; };
WebPrefs& webPrefs();
}
