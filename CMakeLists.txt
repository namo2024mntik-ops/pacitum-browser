cmake_minimum_required(VERSION 3.16)
project(PacitumBrowser VERSION 1.7.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets Svg WebEngineWidgets WebChannel Sql Network Concurrent)
# 1.7.5 site permissions: Web Serial I/O (QSerialPort) and the Web Bluetooth chooser (QtBluetooth).
# Optional so a Qt without these modules still builds; the shims then report NotSupportedError.
find_package(Qt6 QUIET OPTIONAL_COMPONENTS SerialPort Bluetooth)

# Include dirs
include_directories(${CMAKE_SOURCE_DIR}/include)

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
)

# Resources
qt_add_resources(RESOURCES resources/resources.qrc)

# Executable
set(APP_RC "")
if(WIN32)
    # Embeds resources/pacitum.ico as the executable icon
    set(APP_RC ${CMAKE_SOURCE_DIR}/resources/app.rc)
endif()

add_executable(PacitumBrowser ${SOURCES} ${HEADERS} ${RESOURCES} ${APP_RC})
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
install(TARGETS PacitumBrowser DESTINATION bin)
