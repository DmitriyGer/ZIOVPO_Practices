cmake_minimum_required(VERSION 3.20)

project(TrayApp LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    TrayApp/TrayApp.cpp
    TrayApp/TrayIconManager.cpp
    TrayApp/SingleInstanceGuard.cpp
    TrayApp/UserDesktopSession.cpp
    TrayApp/UserIsolatedStopConfirmation.cpp
    TrayApp/WinApiLog.cpp
    Shared/ProcessUtils.cpp
    Shared/ServiceUtils.cpp
    TrayApp/pch.cpp
    TrayApp/TrayApp.rc
)

add_executable(TrayApp WIN32 ${SOURCES})

target_include_directories(TrayApp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/TrayApp
)

target_compile_definitions(TrayApp PRIVATE
    UNICODE
    _UNICODE
)

if(MSVC)
    set_source_files_properties(TrayApp/pch.cpp PROPERTIES
        COMPILE_FLAGS "/Ycpch.h"
    )
    set_source_files_properties(
        TrayApp/TrayApp.cpp
        TrayApp/TrayIconManager.cpp
        TrayApp/SingleInstanceGuard.cpp
        TrayApp/UserDesktopSession.cpp
        TrayApp/UserIsolatedStopConfirmation.cpp
        TrayApp/WinApiLog.cpp
        PROPERTIES
        COMPILE_FLAGS "/Yupch.h"
    )
endif()
