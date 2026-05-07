#pragma once

#include <windows.h>

namespace WinApiLog
{
    void LogInfo(const wchar_t* message);
    void LogError(const wchar_t* operation, DWORD errorCode);
    void LogLastError(const wchar_t* operation);
}
