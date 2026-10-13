#pragma once
#include <QMainWindow>
#include <QSplitter>
#include <QPushButton>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QLabel>
#include <QJsonObject>
#include <QWebEngineDownloadRequest>
#include <QStyle>
#include <QToolButton>

class PacitumTabWidget;
class BrowserTab;
class AddressBar;
class BookmarksBar;
class ThemeManager;
class HistoryManager;
class BookmarkManager;
class DownloadManager;
class SessionManager;
class ZoomController;
class AdBlocker;

class BrowserWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit BrowserWindow(QWidget* parent = nullptr);
    ~BrowserWindow() override;

protected:
    void closeEvent(QCloseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private slots:
    void newTab(const QString& url = {});
    void newPrivateTab();
    void closeTab(int index);
    void closeSplitTab(int index);
    void onTabChanged(int index);
    void navigateToUrl(const QString& url);
    void updateAddressBar(const QString& url);
    void updateTabTitle(const QString& title);
    void onTabLoading(bool loading);
    void goBack();
    void goForward();
    void reloadPage();
    void goHome();
    void toggleBookmark();
    void showDownloads();
    void showSettings();
    void showMenu();
    void toggleDarkMode();
    void toggleBookmarksBar();
    void toggleVerticalTabs();
    void toggleSplitView(int index = -1);
    void duplicateTab(int index);
    void muteTab(int index, bool muted);
    void pinTab(int index, bool pinned);
    void reopenClosedTab();
    void saveSessionDialog();
    void onDownloadRequested(QWebEngineDownloadRequest* dl);
    void copyCurrentUrl();
    void showPageInfo();
    void showHistory();
    void clearHistory();
    void showBookmarksDialog();
    void toggleFullscreen();
    void focusAddressBar();
    void zoomIn();
    void zoomOut();
    void zoomReset();
    void showFind();
    void handleEscape();
    void nextTab();
    void prevTab();
    void lastTab();

private:
    void setupUI();
    void createToolbar();
    void createMenuBar();
    void createStatusBar();
    void setupShortcuts();
    void setupAdBlocker();
    void setupDownloadHandler();
    void applyTheme();
    void applyToolbarTheme();
    void loadSettings();
    void saveSettings();
    void restoreOrNewTab();
    BrowserTab* activeTab() const;
    QJsonObject readSettings() const;
    QToolButton* makeToolBtn(const QString& tip);
    void refreshIcons();
    void updateTabIcon(BrowserTab* tab);
    void updateNavState();

    bool m_darkMode = false;
    QString m_themeSetting = QStringLiteral("System");
    QString m_accent;
    bool m_privateMode = false;
    bool m_splitActive = false;
    bool m_verticalTabs = false;
    QString m_searchEngine = QStringLiteral("Google");

    HistoryManager* m_history = nullptr;
    BookmarkManager* m_bookmarks = nullptr;
    DownloadManager* m_downloads = nullptr;
    ThemeManager* m_theme = nullptr;
    SessionManager* m_session = nullptr;
    ZoomController* m_zoom = nullptr;
    AdBlocker* m_adblock = nullptr;

    QWidget* m_toolbar = nullptr;
    QToolButton* m_backBtn = nullptr;
    QToolButton* m_forwardBtn = nullptr;
    QToolButton* m_reloadBtn = nullptr;
    QToolButton* m_homeBtn = nullptr;
    AddressBar* m_addressBar = nullptr;
    QToolButton* m_splitBtn = nullptr;
    QToolButton* m_profileBtn = nullptr;
    QWidget* m_tabStrip = nullptr;
    QLabel* m_privateBadge = nullptr;
    QToolButton* m_downloadsBtn = nullptr;
    QToolButton* m_menuBtn = nullptr;

    BookmarksBar* m_bookmarksBar = nullptr;
    QSplitter* m_splitter = nullptr;
    PacitumTabWidget* m_tabs = nullptr;
    PacitumTabWidget* m_splitTabs = nullptr;
    PacitumTabWidget* m_activeWidget = nullptr;

    QLabel* m_zoomIndicator = nullptr;
    QGraphicsOpacityEffect* m_toolbarOpacity = nullptr;
    QPropertyAnimation* m_toolbarAnim = nullptr;

    struct ClosedTab { QString url; QString title; };
    QVector<ClosedTab> m_recentlyClosed;
};
