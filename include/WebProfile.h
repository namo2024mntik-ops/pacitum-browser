#pragma once
class QWebEngineProfile;
class QWebEngineSettings;

namespace Pacitum {
// Standard Chrome-like UA / Accept-Language / client hints and sane defaults for a profile.
void configureProfile(QWebEngineProfile* profile, bool persistent);
// Apply user-controlled web settings (JS, popups) to a settings object.
void applyWebSettings(QWebEngineSettings* s);

// Disk-backed main profile (Qt 6's defaultProfile() is off-the-record, so cookies would not persist).
QWebEngineProfile* mainProfile();

struct WebPrefs { bool javascript = true; bool popups = false; };
WebPrefs& webPrefs();
}
