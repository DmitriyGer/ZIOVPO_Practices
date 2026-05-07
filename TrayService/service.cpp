#include <windows.h>
#include <wtsapi32.h>

#include <string>

#include "../Shared/RpcServer.h"
#include "../Shared/ServiceUtils.h"
#include "../Shared/SessionLaunch.h"

namespace
{
    SERVICE_STATUS g_serviceStatus = {};
    SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
    HANDLE g_stopRequestedEvent = nullptr;

    constexpr DWORD kGuiTerminationTimeoutMs = 5000;
    constexpr DWORD kLaunchRetryIntervalMs = 5000;

    void ReportServiceStatus(DWORD currentState, DWORD win32ExitCode, DWORD waitHint)
    {
        static DWORD checkPoint = 1;

        g_serviceStatus.dwCurrentState = currentState;
        g_serviceStatus.dwWin32ExitCode = win32ExitCode;
        g_serviceStatus.dwWaitHint = waitHint;

        if (currentState == SERVICE_RUNNING)
        {
            g_serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_SESSIONCHANGE;
        }
        else
        {
            g_serviceStatus.dwControlsAccepted = 0;
        }

        if (currentState == SERVICE_RUNNING || currentState == SERVICE_STOPPED)
        {
            g_serviceStatus.dwCheckPoint = 0;
        }
        else
        {
            g_serviceStatus.dwCheckPoint = checkPoint++;
        }

        SetServiceStatus(g_statusHandle, &g_serviceStatus);
    }

    void RequestServiceStopFromRpc()
    {
        if (g_stopRequestedEvent != nullptr)
        {
            SetEvent(g_stopRequestedEvent);
        }
    }

    std::wstring GetTrayAppPath()
    {
        wchar_t modulePath[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath)) == 0)
        {
            return {};
        }

        std::wstring trayAppPath = modulePath;
        const size_t separatorPos = trayAppPath.find_last_of(L"\\/");
        if (separatorPos == std::wstring::npos)
        {
            return {};
        }

        trayAppPath.erase(separatorPos + 1);
        trayAppPath += L"TrayApp.exe";
        return trayAppPath;
    }
}

DWORD WINAPI ServiceCtrlHandlerEx(
    DWORD control,
    DWORD eventType,
    LPVOID eventData,
    LPVOID context)
{
    UNREFERENCED_PARAMETER(context);

    switch (control)
    {
    case SERVICE_CONTROL_INTERROGATE:
        ReportServiceStatus(g_serviceStatus.dwCurrentState, NO_ERROR, 0);
        return NO_ERROR;

    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        return ERROR_CALL_NOT_IMPLEMENTED;

    case SERVICE_CONTROL_SESSIONCHANGE:
        if (eventType == WTS_SESSION_LOGON && eventData != nullptr)
        {
            const auto* notification = reinterpret_cast<const WTSSESSION_NOTIFICATION*>(eventData);
            SessionLaunch::LaunchInSession(notification->dwSessionId);
        }
        return NO_ERROR;

    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

void WINAPI ServiceMain(DWORD argc, LPWSTR* argv)
{
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    g_statusHandle = RegisterServiceCtrlHandlerExW(
        ServiceUtils::kTrayServiceName,
        ServiceCtrlHandlerEx,
        nullptr);
    if (g_statusHandle == nullptr)
    {
        return;
    }

    ZeroMemory(&g_serviceStatus, sizeof(g_serviceStatus));
    g_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_serviceStatus.dwServiceSpecificExitCode = 0;

    ReportServiceStatus(SERVICE_START_PENDING, NO_ERROR, 10000);

    g_stopRequestedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_stopRequestedEvent == nullptr)
    {
        ReportServiceStatus(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    const std::wstring trayAppPath = GetTrayAppPath();
    if (trayAppPath.empty())
    {
        CloseHandle(g_stopRequestedEvent);
        g_stopRequestedEvent = nullptr;
        ReportServiceStatus(SERVICE_STOPPED, ERROR_FILE_NOT_FOUND, 0);
        return;
    }

    if (!SessionLaunch::Initialize(trayAppPath, L"/background"))
    {
        CloseHandle(g_stopRequestedEvent);
        g_stopRequestedEvent = nullptr;
        ReportServiceStatus(SERVICE_STOPPED, ERROR_APP_INIT_FAILURE, 0);
        return;
    }

    if (!RpcServer::Start(RequestServiceStopFromRpc))
    {
        SessionLaunch::Shutdown();
        CloseHandle(g_stopRequestedEvent);
        g_stopRequestedEvent = nullptr;
        ReportServiceStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
        return;
    }

    ReportServiceStatus(SERVICE_RUNNING, NO_ERROR, 0);
    SessionLaunch::LaunchInAllSessions();

    for (;;)
    {
        const DWORD waitResult = WaitForSingleObject(g_stopRequestedEvent, kLaunchRetryIntervalMs);
        if (waitResult == WAIT_OBJECT_0)
        {
            break;
        }

        if (waitResult == WAIT_TIMEOUT)
        {
            SessionLaunch::LaunchInAllSessions();
            continue;
        }

        break;
    }

    ReportServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
    RpcServer::Stop();
    SessionLaunch::TerminateLaunchedProcesses(kGuiTerminationTimeoutMs);
    SessionLaunch::Shutdown();

    CloseHandle(g_stopRequestedEvent);
    g_stopRequestedEvent = nullptr;

    ReportServiceStatus(SERVICE_STOPPED, NO_ERROR, 0);
}

int wmain()
{
    SERVICE_TABLE_ENTRYW serviceTable[] =
    {
        { const_cast<LPWSTR>(ServiceUtils::kTrayServiceName), ServiceMain },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcherW(serviceTable))
    {
        return static_cast<int>(GetLastError());
    }

    return 0;
}
