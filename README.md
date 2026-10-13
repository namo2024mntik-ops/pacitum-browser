![Pacitum_Titile](สกรีนช็อต%202026-10-13%20152618.png)
# Pacitum Browser (Qt6 WebEngine)

A calm, fast web browser written in C++17 on Qt 6 WebEngine.
Version 1.2.0: merged Chrome-style title bar (frameless window, tabs in caption row, custom min/max/close, Aero Snap via WM_NCHITTEST), animations (tab width, hover, tab fade, loading spinner, menu fade, NTP), full-page Settings (`pacitum://settings`), standard Chrome user agent + persistent disk profile, in-place upgrading installer. User data now lives in `%LOCALAPPDATA%\Pacitum\Pacitum Browser` (migrated automatically from the install folder).

Version 1.1.0 implemented the Pacitum design system: graphite/paper surfaces, single teal accent, pill omnibox,
tab strip above the toolbar, design SVG icon set tinted at runtime, and a built-in `pacitum://newtab` page.

## Architecture (1:1 with Python)
| Python (`safari_browser/*.py`) | C++ (`safari_browser_cpp/include|src/*.cpp`) |
|---|---|
| `main.py` | `src/main.cpp` |
| `browser_window.py` | `BrowserWindow.h/cpp` |
| `browser_tab.py` | `BrowserTab.h/cpp` |
| `tab_widget.py` | `TabWidget.h/cpp` (PacitumTabWidget + PacitumTabBar) |
| `address_bar.py` | `AddressBar.h/cpp` |
| `bookmark_manager.py` | `BookmarkManager.h/cpp` |
| `bookmarks_bar.py` | `BookmarksBar.h/cpp` |
| `history_manager.py` | `HistoryManager.h/cpp` (SQLite via QtSql) |
| `download_manager.py` | `DownloadManager.h/cpp` |
| `find_bar.py` | `FindBar.h/cpp` |
| `ad_blocker.py` | `AdBlocker.h/cpp` (QWebEngineUrlRequestInterceptor) |
| `session_manager.py` | `SessionManager.h/cpp` |
| `zoom_controller.py` | `ZoomController.h/cpp` |
| `settings_dialog.py` | `SettingsPage.h/cpp` (full-page settings tab) |
| — | `WebProfile.h/cpp` (UA, Accept-Language, persistent profile) · `Widgets.h/cpp` (animated buttons, switches, spinner) |
| `theme_manager.py` | `ThemeManager.h/cpp` (Pacitum design tokens → QSS, SVG icon tinting) |
| — | `PacitumScheme.h/cpp` (`pacitum://newtab` internal page) |

## Prerequisites
- Qt 6.4+ with **QtWebEngineWidgets** (e.g. Qt 6.9.1)
- CMake 3.16+, Ninja or MSVC/MinGW
- C++17 compiler (MSVC 2019+, MinGW 13+, GCC 11+)

On Windows with MSVC:
```
winget install Qt.Qt.6.9
winget install Kitware.CMake
winget install Ninja-build.Ninja
```

On MSYS2 UCRT64 (mingw):
```
pacman -S mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-webengine \
          mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
```

On Ubuntu:
```
sudo apt install qt6-base-dev qt6-svg-dev qt6-webengine-dev cmake ninja-build
```

## Build
```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.9.1/msvc2022_64" -G Ninja
cmake --build build --config Release
./build/PacitumBrowser        # or build/PacitumBrowser.exe on Windows
```

Qt Creator: Open `CMakeLists.txt` → Configure → Run.

## Features (parity with Python)
- Pacitum UI: tab strip (40) → toolbar (44) → bookmarks bar (32); pill omnibox with lock/warning/search icon and star; ⋮ main menu with zoom row; System/Light/Dark themes + accent colors
- Tabs: drag, close others/right, duplicate, split view (two PacitumTabWidgets in QSplitter), pin/mute, vertical mode (Ctrl+Shift+E)
- Navigation, history (SQLite), bookmarks (JSON), downloads (QWebEngineDownloadRequest), find (Ctrl+F), zoom 25-500%
- Ad blocker (host + path interceptor), private mode (NoPersistentCookies), session restore, Do Not Track
- Hardware acceleration, GPU raster, 250MB disk cache, spellcheck, crash auto-reload

Made by Ai (Grok bot + opencode-BaseApp)

## Settings
Stored in `browser_settings.json`, `bookmarks.json`, `browser_session.json`, `browser_history.db` (identical filenames to Python so data migrates).

## Windows installer
GitHub Actions (`.github/workflows/windows.yml`) builds `PacitumBrowser.exe` with Qt 6.8 MSVC, bundles it with windeployqt and produces `installer/PacitumBrowser-Setup-<ver>.exe` via Inno Setup (`installer.iss`). The app icon (`resources/pacitum.ico`, 16–256 px) is embedded through `resources/app.rc`.
