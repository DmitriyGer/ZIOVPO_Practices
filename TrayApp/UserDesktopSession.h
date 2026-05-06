#pragma once

#include <windows.h>

#include <string>

// Manages a temporary isolated desktop in the current TrayApp user session.
class UserDesktopSession final
{
public:
    UserDesktopSession() = default;
    ~UserDesktopSession();

    UserDesktopSession(const UserDesktopSession&) = delete;
    UserDesktopSession& operator=(const UserDesktopSession&) = delete;

    bool Initialize();
    bool SwitchToIsolatedDesktop();
    bool RestoreDefaultDesktop();

    HDESK IsolatedDesktop() const { return m_isolatedDesktop; }
    const std::wstring& IsolatedDesktopName() const { return m_isolatedDesktopName; }

private:
    void CloseDesktopHandle(HDESK& desktopHandle);
    void LogSessionContext() const;

private:
    HDESK m_defaultDesktop = nullptr;
    HDESK m_isolatedDesktop = nullptr;
    std::wstring m_isolatedDesktopName;
    bool m_isolationActive = false;
};
