# Change set: ad blocker rewrite + first unit tests

New files
- include/FilterEngine.h, src/FilterEngine.cpp  -- rule parser/matcher (QtCore only)
- tests/CMakeLists.txt, tests/test_filterengine.cpp -- 14 Qt Test cases
- ADBLOCK.md -- behavior, syntax, how to add lists

Changed files (full copies of your v1.7.8 files + small edits; see changes.patch)
- include/AdBlocker.h, src/AdBlocker.cpp -- same public API, now uses FilterEngine, atomics for the IO thread,
  `loadFilterDirectory()`, `blockedCount()`, `ruleCount()`
- src/BrowserWindow.cpp -- setupAdBlocker() loads the filters folder (1 call)
- CMakeLists.txt -- adds FilterEngine to SOURCES/HEADERS, option PACITUM_BUILD_TESTS

Verified here (Ubuntu, Qt 6.4.2): tests 14/14 pass; AdBlocker.cpp + FilterEngine.cpp compile with -Wall -Wextra
against the Qt WebEngine headers.
NOT verified here: a full PacitumBrowser build on Windows/MSVC with Qt 6.8+ and running the app --
please build and browse a few ad-heavy sites. Version number was not bumped (still 1.7.8).
