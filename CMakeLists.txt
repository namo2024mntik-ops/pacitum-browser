cmake_minimum_required(VERSION 3.16)
project(PacitumBrowser VERSION 1.5.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets Svg WebEngineWidgets WebChannel Sql Network Concurrent)

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
    src/Widgets.cpp
    src/ThemeManager.cpp
    src/PacitumScheme.cpp
    src/FirstRun.cpp
    src/AppSettings.cpp
    src/Updater.cpp
    src/StressTest.cpp
    src/ResizeTest.cpp
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
    include/Widgets.h
    include/ThemeManager.h
    include/PacitumScheme.h
    include/FirstRun.h
    include/AppSettings.h
    include/Updater.h
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
if(WIN32)
    target_link_libraries(PacitumBrowser PRIVATE user32 dwmapi shell32)
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
