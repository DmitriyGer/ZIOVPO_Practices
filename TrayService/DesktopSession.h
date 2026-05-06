#pragma once

#include <windows.h>

#include <string>

// Manages temporary isolated desktop lifetime and desktop switching.
class DesktopSession final
{
public:
    DesktopSession() = default;
    ~DesktopSession();

    DesktopSession(const DesktopSession&) = delete;
    DesktopSession& operator=(const DesktopSession&) = delete;

    bool Initialize();
    bool SwitchToIsolatedDesktop();
    bool RestoreDefaultDesktop();

    HDESK IsolatedDesktop() const { return m_isolatedDesktop; }
    const std::wstring& IsolatedDesktopName() const { return m_isolatedDesktopName; }

private:
    void CloseDesktopHandle(HDESK& desktopHandle);

private:
    HDESK m_defaultDesktop = nullptr;
    HDESK m_isolatedDesktop = nullptr;
    std::wstring m_isolatedDesktopName;
    bool m_isolationActive = false;
};
