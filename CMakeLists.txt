cmake_minimum_required(VERSION 3.16)

project(StellarStore
    VERSION 1.0.0
    DESCRIPTION "Ultra-light software store for Linux"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------- Qt6
set(QT6_PREFIX_HINT "/opt/qt/6.8.3/gcc_64")
find_package(Qt6 6.8 QUIET COMPONENTS
    Widgets
    Network
    Concurrent
    Core
    PATHS "${QT6_PREFIX_HINT}")
if(NOT Qt6_FOUND)
    find_package(Qt6 6.4 REQUIRED COMPONENTS
        Widgets
        Network
        Concurrent
        Core)
endif()

find_package(AppStreamQt REQUIRED)

if(NOT TARGET AppStreamQt)
    message(FATAL_ERROR "AppStreamQt CMake target not found. Install libappstreamqt-dev.")
endif()

# ---------------------------------------------------------------- Sources
set(CORE_SOURCES
    src/core/ConfigManager.cpp
    src/core/AppStreamManager.cpp
    src/core/DesktopEntry.cpp
    src/core/GitHubFetcher.cpp
    src/core/IconCache.cpp
)

set(BACKEND_SOURCES
    src/backends/AbstractBackend.cpp
    src/backends/NativeBackend.cpp
    src/backends/FlatpakBackend.cpp
    src/backends/SnapBackend.cpp
)

set(WIDGET_SOURCES
    src/ui/Widgets/AppCardWidget.cpp
    src/ui/Widgets/AppDetailDialog.cpp
    src/ui/Widgets/FlowLayout.cpp
    src/ui/Widgets/ProcessDialog.cpp
)

set(VIEW_SOURCES
    src/ui/Views/HomeView.cpp
    src/ui/Views/CatalogView.cpp
    src/ui/Views/InstalledView.cpp
    src/ui/Views/LocalInstallerView.cpp
    src/ui/Views/SettingsView.cpp
    src/ui/Views/AboutView.cpp
)

set(MAINWINDOW_SOURCES
    src/ui/MainWindow.cpp
)

set(APP_SOURCES
    src/main.cpp
    ${CORE_SOURCES}
    ${BACKEND_SOURCES}
    ${WIDGET_SOURCES}
    ${VIEW_SOURCES}
    ${MAINWINDOW_SOURCES}
    icons.qrc
)

# ---------------------------------------------------------------- Executable
add_executable(StellarStore ${APP_SOURCES}
    # Headers (for IDE / AUTOMOC friendliness)
    src/core/ConfigManager.h
    src/core/AppStreamManager.h
    src/core/DesktopEntry.h
    src/core/GitHubFetcher.h
    src/core/IconCache.h
    src/backends/AbstractBackend.h
    src/backends/NativeBackend.h
    src/backends/FlatpakBackend.h
    src/backends/SnapBackend.h
    src/ui/MainWindow.h
    src/ui/Widgets/AppCardWidget.h
    src/ui/Widgets/AppDetailDialog.h
    src/ui/Widgets/FlowLayout.h
    src/ui/Widgets/ProcessDialog.h
    src/ui/Views/HomeView.h
    src/ui/Views/CatalogView.h
    src/ui/Views/InstalledView.h
    src/ui/Views/LocalInstallerView.h
    src/ui/Views/SettingsView.h
    src/ui/Views/AboutView.h
)

target_compile_definitions(StellarStore PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

target_include_directories(StellarStore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(StellarStore PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)

target_link_libraries(StellarStore PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    AppStreamQt
)

set_target_properties(StellarStore PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE FALSE
)

# ---------------------------------------------------------------- Install
include(GNUInstallDirs)
install(TARGETS StellarStore
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
