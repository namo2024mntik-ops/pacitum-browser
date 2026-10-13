#pragma once
#include <QWidget>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QProgressBar>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <functional>
#include <QFrame>
#include <QUrl>
#include <QWebEngineFullScreenRequest>
class QLabel;
class QToolButton;

class FindBar;

class BrowserTab : public QWidget {
    Q_OBJECT
public:
    explicit BrowserTab(const QString& url = QString(),
                        bool privateMode = false,
                        const QString& searchEngine = QStringLiteral("Google"),
                        QWidget* parent = nullptr);

    void loadUrl(const QString& rawUrl);
    QString currentUrl() const { return m_url; }
    QString currentTitle() const { return m_title; }
    void goBack();
    void goForward();
    void reload();
    void stop();
    bool canGoBack() const;
    bool canGoForward() const;
    void setZoomFactor(double factor);
    double zoomFactor() const { return m_zoom; }
    void setMuted(bool muted);
    bool isMuted() const;
    QIcon icon() const { return m_view->icon(); }
    bool isLoading() const { return m_loading; }
    int loadProgressValue() const { return m_loadValue; }
    void applyWebSettings();
    void setSearchEngine(const QString& e) { m_searchEngine = e; }
    QWebEngineView* view() const { return m_view; }
    void fadeIn();
    // Session restore: remember the URL/title but only create the page load when first shown.
    void setLazyUrl(const QString& url, const QString& title);
    bool isLazy() const { return !m_pendingUrl.isEmpty(); }
    static std::function<BrowserTab*()> s_popupFactory;
    void showFindBar();
    void refreshTheme();   // re-tint native chrome inside the tab (info bar)
    void hideFindBar();

signals:
    void urlChanged(const QString& url);
    void titleChanged(const QString& title);
    void loadingChanged(bool loading);
    void iconChanged();
    void progressChanged(int progress);
    void loadDone(bool ok);
    void crashed();
    void fullScreenRequested(QWebEngineFullScreenRequest request);

private:
    int m_loadValue = 0;
private slots:
    void onUrlChanged(const QUrl& url);
    void onTitleChanged(const QString& title);
    void onLoadStarted();
    void onLoadProgress(int p);
    void onLoadFinished(bool ok);
    void onRenderCrashed(QWebEnginePage::RenderProcessTerminationStatus status, int code);

private:
    void buildSignInBar();
    void showSignInBar(const QUrl& target);
    void checkGoogleSignIn(const QUrl& url);
    class InfoBar* m_signInBar = nullptr;
    QUrl m_signInUrl;
    QString m_pendingUrl;
protected:
    void showEvent(QShowEvent* e) override;
private:
    static int s_idCounter;
    int m_tabId = 0;
    bool m_private = false;
    QString m_searchEngine;
    QString m_url;
    QString m_title = QStringLiteral("New Tab");
    double m_zoom = 1.0;
    bool m_loading = false;

    QWebEngineView* m_view = nullptr;
    QWebEngineProfile* m_profile = nullptr;
    QWebEnginePage* m_page = nullptr;
    QProgressBar* m_progress = nullptr;
    FindBar* m_findBar = nullptr;
    QGraphicsOpacityEffect* m_opacity = nullptr;
    QPropertyAnimation* m_anim = nullptr;
};
