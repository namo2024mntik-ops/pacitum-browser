# Safari Browser — C++ Port (Qt6 WebEngine)

Faithful C++17 rework of the Python/PyQt6 Safari Browser (`D:\gyu\safari_browser`).

## Architecture (1:1 with Python)
| Python (`safari_browser/*.py`) | C++ (`safari_browser_cpp/include|src/*.cpp`) |
|---|---|
| `main.py` | `src/main.cpp` |
| `browser_window.py` | `BrowserWindow.h/cpp` |
| `browser_tab.py` | `BrowserTab.h/cpp` |
| `tab_widget.py` | `TabWidget.h/cpp` (SafariTabWidget + SafariTabBar) |
| `address_bar.py` | `AddressBar.h/cpp` |
| `bookmark_manager.py` | `BookmarkManager.h/cpp` |
| `bookmarks_bar.py` | `BookmarksBar.h/cpp` |
| `history_manager.py` | `HistoryManager.h/cpp` (SQLite via QtSql) |
| `download_manager.py` | `DownloadManager.h/cpp` |
| `find_bar.py` | `FindBar.h/cpp` |
| `ad_blocker.py` | `AdBlocker.h/cpp` (QWebEngineUrlRequestInterceptor) |
| `session_manager.py` | `SessionManager.h/cpp` |
| `zoom_controller.py` | `ZoomController.h/cpp` |
| `settings_dialog.py` | `SettingsDialog.h/cpp` |
| `theme_manager.py` | `ThemeManager.h/cpp` |

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
sudo apt install qt6-base-dev qt6-webengine-dev cmake ninja-build
```

## Build
```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.9.1/msvc2022_64" -G Ninja
cmake --build build --config Release
./build/SafariBrowser        # or build/SafariBrowser.exe on Windows
```

Qt Creator: Open `CMakeLists.txt` → Configure → Run.

## Features (parity with Python)
- Safari-style UI: rounded address bar with 🔒/⚠️, rounded tabs, toolbar fade animation, Light/Dark themes
- Tabs: drag, close others/right, duplicate, split view (two SafariTabWidgets in QSplitter), pin/mute, vertical mode (Ctrl+Shift+E)
- Navigation, history (SQLite), bookmarks (JSON), downloads (QWebEngineDownloadRequest), find (Ctrl+F), zoom 25-500%
- Ad blocker (host + path interceptor), private mode (NoPersistentCookies), session restore, Do Not Track
- Hardware acceleration, GPU raster, 250MB disk cache, spellcheck, crash auto-reload

## Settings
Stored in `browser_settings.json`, `bookmarks.json`, `browser_session.json`, `browser_history.db` (identical filenames to Python so data migrates).
