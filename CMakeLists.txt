cmake_minimum_required(VERSION 3.16)
project(SafariBrowser VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets WebEngineWidgets WebChannel Sql Network)

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
    src/SettingsDialog.cpp
    src/ThemeManager.cpp
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
    include/SettingsDialog.h
    include/ThemeManager.h
)

# Resources
qt_add_resources(RESOURCES resources/resources.qrc)

# Executable
add_executable(SafariBrowser ${SOURCES} ${HEADERS} ${RESOURCES})

target_link_libraries(SafariBrowser PRIVATE
    Qt6::Widgets
    Qt6::WebEngineWidgets
    Qt6::WebChannel
    Qt6::Sql
    Qt6::Network
)

# Platform specific
if(WIN32)
    set_target_properties(SafariBrowser PROPERTIES WIN32_EXECUTABLE TRUE)
    # Enable High DPI
    target_compile_definitions(SafariBrowser PRIVATE QT_ENABLE_HIGHDPI_SCALING)
endif()

# Copy resources
if(EXISTS "${CMAKE_SOURCE_DIR}/resources")
    file(COPY "${CMAKE_SOURCE_DIR}/resources" DESTINATION "${CMAKE_BINARY_DIR}")
endif()

# Install
install(TARGETS SafariBrowser DESTINATION bin)
