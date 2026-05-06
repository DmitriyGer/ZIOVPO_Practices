#pragma once

#include <windows.h>

namespace WinApiLog
{
    void LogError(const wchar_t* operation, DWORD errorCode);
    void LogLastError(const wchar_t* operation);
}
