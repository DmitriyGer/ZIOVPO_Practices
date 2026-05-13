cmake_minimum_required(VERSION 3.20)

project(TrayApp LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ANTIVIRUS_SHARED_SOURCES
    Shared/AntivirusDatabase.cpp
    Shared/AntivirusEngine.cpp
    Shared/AntivirusScanner.cpp
    Shared/AntivirusTypes.cpp
    Shared/ByteStream.cpp
)

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

add_executable(AntivirusEngineTests
    Tests/AntivirusEngineTests.cpp
    ${ANTIVIRUS_SHARED_SOURCES}
    Shared/ApiClient.cpp
    Shared/InMemorySession.cpp
    Shared/ServiceApiState.cpp
)

target_include_directories(AntivirusEngineTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/TrayService/Generated
)

if(WIN32)
    target_link_libraries(AntivirusEngineTests PRIVATE Winhttp)
endif()

enable_testing()
add_test(NAME AntivirusEngineTests COMMAND AntivirusEngineTests)

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
