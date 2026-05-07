#include "pch.h"

#include "WinApiLog.h"

#include <string>

namespace
{
    std::wstring FormatErrorMessage(DWORD errorCode)
    {
        wchar_t* systemText = nullptr;
        const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER
            | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS;
        const DWORD result = FormatMessageW(
            flags,
            nullptr,
            errorCode,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPWSTR>(&systemText),
            0,
            nullptr);
        if (result == 0 || systemText == nullptr)
        {
            return L"(no text)";
        }

        std::wstring message = systemText;
        LocalFree(systemText);
        return message;
    }
}

void WinApiLog::LogInfo(const wchar_t* message)
{
    std::wstring text = L"[TrayApp][Desktop] ";
    text += message != nullptr ? message : L"(no message)";
    text += L"\r\n";
    OutputDebugStringW(text.c_str());
}

void WinApiLog::LogError(const wchar_t* operation, DWORD errorCode)
{
    std::wstring text = L"[TrayApp][Desktop] ";
    text += operation != nullptr ? operation : L"(unknown operation)";
    text += L" failed. error=";
    text += std::to_wstring(errorCode);
    text += L" message=";
    text += FormatErrorMessage(errorCode);
    text += L"\r\n";
    OutputDebugStringW(text.c_str());
}

void WinApiLog::LogLastError(const wchar_t* operation)
{
    LogError(operation, GetLastError());
}
