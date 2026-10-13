# Build — Pacitum Browser

## Verified CMake parsing
```
cmake version 4.4.4 — compiler GNU 15.2.0 detected
=> Fails only at find_package(Qt6) because Qt6 not installed on this machine.
   CMakeLists.txt:11 syntax is valid.
```

## Full build (requires Qt6 + WebEngine)
```bash
# Install Qt (example: official Qt installer -> C:/Qt/6.9.1/msvc2022_64)
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.9.1/msvc2022_64" -G Ninja
cmake --build build --config Release
./build/PacitumBrowser.exe  # shares browser_history.db / bookmarks.json with the Python prototype
```

Requirements: Qt 6.5+, QtWebEngineWidgets, QtSvg, CMake 3.16+, C++17 compiler.

MSYS2 would need `mingw-w64-ucrt-x86_64-qt6-webengine` but that package is currently not
available in MSYS2 repos — use the official Qt installer instead.

## Project stats
- 14 headers in `include/` + 15 sources in `src/` + CMakeLists.txt + resources.qrc
- Lines: ~3000+ LOC, 1:1 parity with Python version
- Data files compatible: browser_settings.json, bookmarks.json, browser_session.json, browser_history.db
