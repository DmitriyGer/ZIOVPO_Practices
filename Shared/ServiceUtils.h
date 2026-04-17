#pragma once

#include <windows.h>

namespace ServiceUtils
{
    inline constexpr wchar_t kTrayServiceName[] = L"TrayService";

    bool QueryServiceStatus(const wchar_t* serviceName, SERVICE_STATUS_PROCESS& serviceStatus, DWORD* lastError = nullptr);
    bool WaitForServiceState(const wchar_t* serviceName, DWORD expectedState, DWORD timeoutMs);
    bool StartServiceAndWaitRunning(const wchar_t* serviceName, DWORD timeoutMs);
    bool EnsureServiceInstalled(const wchar_t* serviceName, const wchar_t* serviceBinaryPath, DWORD* lastError = nullptr);
}
