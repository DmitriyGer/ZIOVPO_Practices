#pragma once

#include <windows.h>

#include <string>

namespace ProcessUtils
{
    bool GetParentProcessId(DWORD processId, DWORD& parentProcessId);
    bool GetProcessNameById(DWORD processId, std::wstring& processName);
    bool IsCurrentProcessParentNamed(const wchar_t* expectedProcessName);
}
