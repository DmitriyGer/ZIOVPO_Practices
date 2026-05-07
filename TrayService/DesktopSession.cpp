#include "DesktopSession.h"

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
            L"TrayServiceStopDesktop_%lu_%llu",
            GetCurrentProcessId(),
            static_cast<unsigned long long>(GetTickCount64()));
        return buffer;
    }
}

DesktopSession::~DesktopSession()
{
    RestoreDefaultDesktop();
    CloseDesktopHandle(m_isolatedDesktop);
    CloseDesktopHandle(m_defaultDesktop);
}

bool DesktopSession::Initialize()
{
    if (m_isolatedDesktop != nullptr)
    {
        return true;
    }

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

    return true;
}

bool DesktopSession::SwitchToIsolatedDesktop()
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

    m_isolationActive = true;
    return true;
}

bool DesktopSession::RestoreDefaultDesktop()
{
    if (!m_isolationActive)
    {
        return true;
    }

    if (m_defaultDesktop == nullptr)
    {
        m_isolationActive = false;
        return false;
    }

    if (!SwitchDesktop(m_defaultDesktop))
    {
        WinApiLog::LogLastError(L"SwitchDesktop(default)");
        return false;
    }

    m_isolationActive = false;
    return true;
}

void DesktopSession::CloseDesktopHandle(HDESK& desktopHandle)
{
    if (desktopHandle != nullptr)
    {
        if (!CloseDesktop(desktopHandle))
        {
            WinApiLog::LogLastError(L"CloseDesktop");
        }

        desktopHandle = nullptr;
    }
}
