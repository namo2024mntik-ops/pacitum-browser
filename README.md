![Pacitum_Titile](สกรีนช็อต%202026-10-13%20152618.png)
# Pacitum Browser (Qt 6 WebEngine)

A desktop web browser written in C++17 with Qt 6 WebEngine.

Version 1.7.8: omnibox inline hints select only the untyped suffix.

Version 1.7.7: reader mode (Ctrl+Shift+R) and Translate page (Google Translate, language submenu in the ⋮ menu).

Version 1.2.0: merged Chrome-style title bar (frameless window, tabs in caption row, custom min/max/close, Aero Snap via WM_NCHITTEST), animations, full-page Settings (`pacitum://settings`), standard Chrome user agent, persistent disk profile, and in-place upgrading installer. User data is stored outside the install folder and migrated from older installs.

Version 1.1.0 introduced the Pacitum design system: graphite/paper surfaces, a teal accent, pill omnibox, tab strip above the toolbar, tinted SVG icons, and the built-in `pacitum://newtab` page.

## Architecture

| Component | Files | Responsibility |
|---|---|---|
| Application entry | `src/main.cpp` | Startup, profile selection, single-instance handoff |
| Browser window and tabs | `BrowserWindow`, `BrowserTab`, `TabWidget` | Window UI, navigation, tab lifecycle |
| Browser features | `AddressBar`, `BookmarkManager`, `HistoryManager`, `DownloadManager`, `FindBar`, `AdBlocker`, `SessionManager` | Address entry, bookmarks, history, downloads, find, filtering, session restore |
| Profiles and permissions | `ProfileManager`, `WebProfile`, `PermissionController`, `SitePermissions` | Profile isolation, web settings, site permissions |
| UI and internal pages | `ThemeManager`, `Widgets`, `SettingsPage`, `PacitumScheme` | Themes, controls, settings, and `pacitum://` pages |

## Prerequisites

- Qt 6.4 or newer with Qt Widgets, Svg, WebEngineWidgets, WebChannel, Sql, Network, and Concurrent
- CMake 3.16 or newer
- C++17 compiler (MSVC 2019+, MinGW 13+, or GCC 11+)
- Ninja or another supported CMake generator

SerialPort and Bluetooth support are optional. CMake enables them when the corresponding Qt modules are installed.

On Windows with MSVC, install Qt with the modules above, CMake, and Ninja. The GitHub Actions workflow uses Qt 6.8 with MSVC 2022.

On MSYS2 UCRT64:

```bash
pacman -S mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-svg \
          mingw-w64-ucrt-x86_64-qt6-webengine mingw-w64-ucrt-x86_64-qt6-webchannel \
          mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
```

On Ubuntu:

```bash
sudo apt install qt6-base-dev qt6-svg-dev qt6-webengine-dev qt6-webchannel-dev cmake ninja-build
```

## Build

For a Qt installation in a custom location, set `CMAKE_PREFIX_PATH` to its installation prefix:

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="C:/Qt/6.9.1/msvc2022_64"
cmake --build build --config Release
```

For system-installed Qt on Linux or MSYS2, omit `CMAKE_PREFIX_PATH`:

```bash
cmake -S . -B build -G Ninja
cmake --build build --config Release
```

Run `build/PacitumBrowser` on Linux or `build/PacitumBrowser.exe` on Windows.

In Qt Creator, open `CMakeLists.txt`, configure the project, and run it.

## Features

- Pacitum UI with tab strip, toolbar, bookmarks bar, pill omnibox, themes, and accent colors
- Tabs with drag and drop, duplicate, split view, pin and mute controls, and vertical mode (Ctrl+Shift+E)
- Navigation, SQLite history, JSON bookmarks, downloads, find (Ctrl+F), and zoom from 25% to 500%
- Ad blocking, private tabs, session restore, and Do Not Track
- Hardware acceleration, GPU rasterization, and a 250 MB HTTP cache
- Spellcheck when Qt WebEngine dictionaries are installed; crashed web renderers are automatically reloaded

Made by AI (Grok bot + opencode-BaseApp)

## Settings and user data

Settings and browser data use the filenames `browser_settings.json`, `bookmarks.json`, `browser_session.json`, and `browser_history.db`. They are stored in the active profile's data directory under the application's local data directory. Older installs are migrated into the default profile. Guest browsing uses temporary storage.

## Windows installer

GitHub Actions (`.github/workflows/windows.yml`) builds `PacitumBrowser.exe` with Qt 6.8 MSVC, bundles it with `windeployqt`, and produces `installer/PacitumBrowser-Setup-<ver>.exe` via Inno Setup (`installer.iss`). The app icon (`resources/pacitum.ico`) is embedded through `resources/app.rc`.
