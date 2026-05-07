#include "pch.h"

#include "UserDesktopSession.h"

#include "WinApiLog.h"

#include <cwchar>

namespace
{
    constexpr ACCESS_MASK kDesktopAccess =
        DESKTOP_CREATEWINDOW
        | DESKTOP_READOBJECTS
        | DESKTOP_WRITEOBJECTS
        | DESKTOP_SWITCHDESKTOP;

    std::wstring BuildIsolatedDesktopName()
    {
        wchar_t buffer[128] = {};
        swprintf_s(
            buffer,
            ARRAYSIZE(buffer),
            L"TrayAppStopDesktop_%lu_%llu",
            GetCurrentProcessId(),
            static_cast<unsigned long long>(GetTickCount64()));
        return buffer;
    }

    void LogDesktopSuccess(const wchar_t* operation)
    {
        std::wstring text = operation != nullptr ? operation : L"(unknown operation)";
        text += L" succeeded.";
        WinApiLog::LogInfo(text.c_str());
    }
}

UserDesktopSession::~UserDesktopSession()
{
    RestoreDefaultDesktop();
    CloseDesktopHandle(m_isolatedDesktop);
    CloseDesktopHandle(m_defaultDesktop);
}

bool UserDesktopSession::Initialize()
{
    if (m_isolatedDesktop != nullptr)
    {
        return true;
    }

    LogSessionContext();

    m_defaultDesktop = OpenInputDesktop(
        0,
        FALSE,
        DESKTOP_READOBJECTS | DESKTOP_SWITCHDESKTOP);
    if (m_defaultDesktop == nullptr)
    {
        WinApiLog::LogLastError(L"OpenInputDesktop");

        m_defaultDesktop = OpenDesktopW(
            L"Default",
            0,
            FALSE,
            DESKTOP_READOBJECTS | DESKTOP_SWITCHDESKTOP);
        if (m_defaultDesktop == nullptr)
        {
            WinApiLog::LogLastError(L"OpenDesktopW(Default)");
            return false;
        }

        LogDesktopSuccess(L"OpenDesktopW(Default)");
    }
    else
    {
        LogDesktopSuccess(L"OpenInputDesktop");
    }

    m_isolatedDesktopName = BuildIsolatedDesktopName();
    m_isolatedDesktop = CreateDesktopW(
        m_isolatedDesktopName.c_str(),
        nullptr,
        nullptr,
        0,
        kDesktopAccess,
        nullptr);
    if (m_isolatedDesktop == nullptr)
    {
        WinApiLog::LogLastError(L"CreateDesktopW");
        CloseDesktopHandle(m_defaultDesktop);
        return false;
    }

    std::wstring text = L"CreateDesktopW succeeded. desktopName=";
    text += m_isolatedDesktopName;
    WinApiLog::LogInfo(text.c_str());
    return true;
}

bool UserDesktopSession::SwitchToIsolatedDesktop()
{
    if (m_isolatedDesktop == nullptr && !Initialize())
    {
        return false;
    }

    if (!SwitchDesktop(m_isolatedDesktop))
    {
        WinApiLog::LogLastError(L"SwitchDesktop(isolated)");
        return false;
    }

    LogDesktopSuccess(L"SwitchDesktop(isolated)");
    m_isolationActive = true;
    return true;
}

bool UserDesktopSession::RestoreDefaultDesktop()
{
    if (!m_isolationActive)
    {
        WinApiLog::LogInfo(L"Desktop restore skipped because isolation is not active.");
        return true;
    }

    if (m_defaultDesktop == nullptr)
    {
        WinApiLog::LogInfo(L"Desktop restore failed because the default desktop handle is not available.");
        m_isolationActive = false;
        return false;
    }

    if (!SwitchDesktop(m_defaultDesktop))
    {
        WinApiLog::LogLastError(L"SwitchDesktop(default)");
        return false;
    }

    LogDesktopSuccess(L"SwitchDesktop(default)");
    m_isolationActive = false;
    return true;
}

void UserDesktopSession::CloseDesktopHandle(HDESK& desktopHandle)
{
    if (desktopHandle == nullptr)
    {
        return;
    }

    if (!CloseDesktop(desktopHandle))
    {
        WinApiLog::LogLastError(L"CloseDesktop");
    }
    else
    {
        LogDesktopSuccess(L"CloseDesktop");
    }

    desktopHandle = nullptr;
}

void UserDesktopSession::LogSessionContext() const
{
    DWORD currentSessionId = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSessionId))
    {
        WinApiLog::LogLastError(L"ProcessIdToSessionId(TrayApp)");
        return;
    }

    const DWORD activeConsoleSessionId = WTSGetActiveConsoleSessionId();
    std::wstring text = L"TrayApp session context. processSessionId=";
    text += std::to_wstring(currentSessionId);
    text += L" activeConsoleSessionId=";
    if (activeConsoleSessionId == 0xFFFFFFFF)
    {
        text += L"INVALID";
    }
    else
    {
        text += std::to_wstring(activeConsoleSessionId);
    }

    WinApiLog::LogInfo(text.c_str());
}
