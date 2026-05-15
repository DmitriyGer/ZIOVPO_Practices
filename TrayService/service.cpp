#include <windows.h>
#include <wtsapi32.h>

#include <mutex>
#include <string>

#include "../Shared/RpcServer.h"
#include "../Shared/ProcessProtection.h"
#include "../Shared/ServiceApiState.h"
#include "../Shared/ServiceUtils.h"
#include "../Shared/SessionLaunch.h"
#include "WinApiLog.h"

namespace
{
    SERVICE_STATUS g_serviceStatus = {};
    SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
    HANDLE g_stopRequestedEvent = nullptr;
    std::mutex g_stopConfirmationFlowMutex;

    constexpr DWORD kGuiTerminationTimeoutMs = 5000;
    constexpr DWORD kLaunchRetryIntervalMs = 5000;
    constexpr bool kAllowAdministratorsToTerminateProcesses = false;
    constexpr bool kDenyServiceStopForAdministrators = false;
    constexpr bool kRestrictServiceSecurityWritesForAdministrators = false;

    void LogHardeningContinueWarning(const wchar_t* stepName)
    {
        std::wstring text = L"[TrayService][WARNING] ";
        text += stepName != nullptr ? stepName : L"(unknown hardening step)";
        text += L" failed during startup. TrayService will continue startup and attempt to reach SERVICE_RUNNING.\r\n";
        OutputDebugStringW(text.c_str());
    }

    void LogRpcStopFlow(const wchar_t* message)
    {
        std::wstring text = L"[TrayService][RPC] ";
        text += message != nullptr ? message : L"(no message)";
        text += L"\r\n";
        OutputDebugStringW(text.c_str());
    }

    void LogServiceAndConsoleSessions()
    {
        DWORD serviceSessionId = 0;
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &serviceSessionId))
        {
            WinApiLog::LogLastError(L"ProcessIdToSessionId(TrayService)");
            return;
        }

        const DWORD activeConsoleSessionId = WTSGetActiveConsoleSessionId();
        std::wstring text = L"TrayService session context. serviceSessionId=";
        text += std::to_wstring(serviceSessionId);
        text += L" activeConsoleSessionId=";
        if (activeConsoleSessionId == 0xFFFFFFFF)
        {
            text += L"INVALID";
        }
        else
        {
            text += std::to_wstring(activeConsoleSessionId);
        }

        LogRpcStopFlow(text.c_str());

        if (serviceSessionId == 0)
        {
            LogRpcStopFlow(
                L"TrayService is running in Session 0. Service-side desktop confirmation cannot be shown to the interactive user because of Session 0 isolation.");
        }
    }

    void ReportServiceStatus(DWORD currentState, DWORD win32ExitCode, DWORD waitHint)
    {
        static DWORD checkPoint = 1;

        g_serviceStatus.dwCurrentState = currentState;
        g_serviceStatus.dwWin32ExitCode = win32ExitCode;
        g_serviceStatus.dwWaitHint = waitHint;

       if (currentState == SERVICE_RUNNING)
        {
            g_serviceStatus.dwControlsAccepted =
                SERVICE_ACCEPT_STOP |
                SERVICE_ACCEPT_SHUTDOWN |
                SERVICE_ACCEPT_SESSIONCHANGE;
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

    RpcServer::StopRequestResult RequestServiceStopFromRpc()
    {
        std::lock_guard<std::mutex> lock(g_stopConfirmationFlowMutex);

        LogRpcStopFlow(L"StopService callback entered.");
        LogServiceAndConsoleSessions();
        LogRpcStopFlow(
            L"Stop confirmation will be delegated to TrayApp in the caller user session instead of being shown from TrayService.");
        return RpcServer::StopRequestResult::ConfirmationRequired;
    }

    RpcServer::StopRequestResult ConfirmServiceStopFromRpc()
    {
        std::lock_guard<std::mutex> lock(g_stopConfirmationFlowMutex);

        LogRpcStopFlow(L"ConfirmStopService callback entered.");
        if (g_stopRequestedEvent == nullptr)
        {
            LogRpcStopFlow(L"ConfirmStopService failed because the stop event handle is not available.");
            return RpcServer::StopRequestResult::Failed;
        }

        if (!SetEvent(g_stopRequestedEvent))
        {
            WinApiLog::LogLastError(L"SetEvent(g_stopRequestedEvent)");
            return RpcServer::StopRequestResult::Failed;
        }

        LogRpcStopFlow(L"ConfirmStopService approved service shutdown after user-session confirmation.");
        return RpcServer::StopRequestResult::Approved;
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
        if (g_stopRequestedEvent == nullptr)
        {
            return ERROR_INVALID_HANDLE;
        }

        ReportServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
        return SetEvent(g_stopRequestedEvent) ? NO_ERROR : GetLastError();

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

    ProcessProtection::ProtectionPolicy protectionPolicy = {};
    protectionPolicy.administratorsTerminationPolicy =
        kAllowAdministratorsToTerminateProcesses
        ? ProcessProtection::AdministratorsTerminationPolicy::AllowTerminate
        : ProcessProtection::AdministratorsTerminationPolicy::DenyTerminate;
    protectionPolicy.denyServiceStopForAdministrators = kDenyServiceStopForAdministrators;
    protectionPolicy.restrictServiceSecurityWriteForAdministrators =
        kRestrictServiceSecurityWritesForAdministrators;
    ProcessProtection::SetProtectionPolicy(protectionPolicy);

    const bool processSecurityHardened = ProcessProtection::HardenProcessSecurity();
    const bool serviceSecurityHardened = ProcessProtection::HardenServiceSecurity();
    if (!processSecurityHardened || !serviceSecurityHardened)
    {
        if (!processSecurityHardened)
        {
            LogHardeningContinueWarning(L"HardenProcessSecurity");
        }

        if (!serviceSecurityHardened)
        {
            LogHardeningContinueWarning(L"HardenServiceSecurity");
        }
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

    if (!RpcServer::Start(RequestServiceStopFromRpc, ConfirmServiceStopFromRpc))
    {
        SessionLaunch::Shutdown();
        CloseHandle(g_stopRequestedEvent);
        g_stopRequestedEvent = nullptr;
        ReportServiceStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
        return;
    }

    ServiceApiState::Instance().Initialize();

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
            ServiceApiState::Instance().Tick();
            SessionLaunch::LaunchInAllSessions();
            continue;
        }

        break;
    }

    ReportServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
    RpcServer::Stop();
    ServiceApiState::Instance().Shutdown();
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
