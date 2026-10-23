cmake_minimum_required(VERSION 3.16)
project(PacitumBrowser VERSION 1.8.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Svg WebEngineWidgets WebChannel Sql Network Concurrent)
# 1.7.6 site permissions: Web Serial I/O (QSerialPort) and the Web Bluetooth chooser (QtBluetooth).
# Optional so a Qt without these modules still builds; the shims then report NotSupportedError.
find_package(Qt6 6.8 QUIET OPTIONAL_COMPONENTS SerialPort Bluetooth)

# Include dirs
include_directories(${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/third_party/qrcodegen)

# Sources
set(SOURCES
    src/main.cpp
    src/BrowserWindow.cpp
    src/BrowserTab.cpp
    src/TabWidget.cpp
    src/AddressBar.cpp
    src/BookmarkManager.cpp
    src/BookmarksBar.cpp
    src/HistoryManager.cpp
    src/DownloadManager.cpp
    src/FindBar.cpp
    src/AdBlocker.cpp
    src/SessionManager.cpp
    src/ZoomController.cpp
    src/SettingsPage.cpp
    src/WebProfile.cpp
    src/ProfileManager.cpp
    src/ProfileUi.cpp
    src/Widgets.cpp
    src/Motion.cpp
    src/ThemeManager.cpp
    src/PacitumScheme.cpp
    src/FirstRun.cpp
    src/AppSettings.cpp
    src/Updater.cpp
    src/StressTest.cpp
    src/PerfTest.cpp
    src/ResizeTest.cpp
    src/SitePermissions.cpp
    src/PermissionUi.cpp
    src/PermissionDevices.cpp
    src/PermissionController.cpp
    src/PermissionSettings.cpp
    src/PermissionTest.cpp
    src/SplitView.cpp
    src/ExtensionManager.cpp
    src/Tools.cpp
    src/TabOverview.cpp
    third_party/qrcodegen/qrcodegen.cpp
)

set(HEADERS
    include/BrowserWindow.h
    include/BrowserTab.h
    include/TabWidget.h
    include/AddressBar.h
    include/BookmarkManager.h
    include/BookmarksBar.h
    include/HistoryManager.h
    include/DownloadManager.h
    include/FindBar.h
    include/AdBlocker.h
    include/SessionManager.h
    include/ZoomController.h
    include/SettingsPage.h
    include/WebProfile.h
    include/ProfileManager.h
    include/ProfileUi.h
    include/Widgets.h
    include/Motion.h
    include/ThemeManager.h
    include/PacitumScheme.h
    include/FirstRun.h
    include/AppSettings.h
    include/Updater.h
    include/SitePermissions.h
    include/PermissionUi.h
    include/PermissionDevices.h
    include/PermissionController.h
    include/PermissionSettings.h
    include/SplitView.h
    include/ExtensionManager.h
    include/Tools.h
    include/TabOverview.h
)

# Resources
qt_add_resources(RESOURCES resources/resources.qrc)

# Executable
set(APP_RC "")
if(WIN32)
    # Embeds resources/pacitum.ico as the executable icon
    set(APP_RC ${CMAKE_SOURCE_DIR}/resources/app.rc)
endif()

set(APP_ICNS "")
if(APPLE)
    # macOS: a real .app bundle with the icon and the permission texts web pages need
    set(APP_ICNS ${CMAKE_SOURCE_DIR}/resources/macos/pacitum.icns)
    set_source_files_properties(${APP_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

add_executable(PacitumBrowser ${SOURCES} ${HEADERS} ${RESOURCES} ${APP_RC} ${APP_ICNS})
target_compile_definitions(PacitumBrowser PRIVATE PACITUM_VERSION="${PROJECT_VERSION}")

target_link_libraries(PacitumBrowser PRIVATE
    Qt6::Widgets
    Qt6::Svg
    Qt6::WebEngineWidgets
    Qt6::WebChannel
    Qt6::Sql
    Qt6::Network
    Qt6::Concurrent
)
if(TARGET Qt6::SerialPort)
    target_link_libraries(PacitumBrowser PRIVATE Qt6::SerialPort)
    target_compile_definitions(PacitumBrowser PRIVATE PACITUM_HAVE_SERIALPORT)
endif()
if(TARGET Qt6::Bluetooth)
    target_link_libraries(PacitumBrowser PRIVATE Qt6::Bluetooth)
    target_compile_definitions(PacitumBrowser PRIVATE PACITUM_HAVE_BLUETOOTH)
endif()
if(WIN32)
    target_link_libraries(PacitumBrowser PRIVATE user32 dwmapi shell32 psapi setupapi ole32)
endif()

# Platform specific
if(APPLE)
    set_target_properties(PacitumBrowser PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/resources/macos/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME "Pacitum Browser"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.pacitum.browser"
        MACOSX_BUNDLE_ICON_FILE pacitum.icns
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
endif()
if(WIN32)
    set_target_properties(PacitumBrowser PROPERTIES WIN32_EXECUTABLE TRUE)
    # Enable High DPI
    target_compile_definitions(PacitumBrowser PRIVATE QT_ENABLE_HIGHDPI_SCALING)
endif()

# Copy resources
if(EXISTS "${CMAKE_SOURCE_DIR}/resources")
    file(COPY "${CMAKE_SOURCE_DIR}/resources" DESTINATION "${CMAKE_BINARY_DIR}")
endif()

# Install
install(TARGETS PacitumBrowser BUNDLE DESTINATION . RUNTIME DESTINATION bin)
if(UNIX AND NOT APPLE)
    # Linux: menu entry + icon (the AppImage build in CI uses the same files)
    install(FILES resources/linux/pacitum-browser.desktop DESTINATION share/applications)
    install(FILES resources/linux/pacitum-browser.png DESTINATION share/icons/hicolor/256x256/apps)
endif()
