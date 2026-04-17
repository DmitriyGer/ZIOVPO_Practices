#pragma once

#include <windows.h>

#include <string>

namespace SessionLaunch
{
    bool Initialize(const std::wstring& trayAppPath, const std::wstring& launchArguments);
    void LaunchInAllSessions();
    void LaunchInSession(DWORD sessionId);
    void TerminateLaunchedProcesses(DWORD waitTimeoutMs);
    void Shutdown();
}
