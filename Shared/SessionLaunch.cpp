#include "SessionLaunch.h"

#include <tlhelp32.h>
#include <userenv.h>
#include <wtsapi32.h>

#include <array>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "Userenv.lib")
#pragma comment(lib, "Wtsapi32.lib")

namespace
{
    struct ProcessEntry
    {
        DWORD processId = 0;
        HANDLE processHandle = nullptr;
    };

    std::wstring g_trayAppPath;
    std::wstring g_launchArguments;
    std::unordered_map<DWORD, ProcessEntry> g_sessionProcesses;
    std::mutex g_stateMutex;

    bool g_privilegesInitialized = false;
    bool g_privilegesEnabled = false;

    bool EnablePrivilegeOnProcessToken(HANDLE processToken, const wchar_t* privilegeName)
    {
        LUID privilegeLuid = {};
        if (!LookupPrivilegeValueW(nullptr, privilegeName, &privilegeLuid))
        {
            return false;
        }

        TOKEN_PRIVILEGES tokenPrivileges = {};
        tokenPrivileges.PrivilegeCount = 1;
        tokenPrivileges.Privileges[0].Luid = privilegeLuid;
        tokenPrivileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        if (!AdjustTokenPrivileges(processToken, FALSE, &tokenPrivileges, 0, nullptr, nullptr))
        {
            return false;
        }

        return GetLastError() == ERROR_SUCCESS;
    }

    bool EnsureLaunchPrivilegesEnabled()
    {
        if (g_privilegesInitialized)
        {
            return g_privilegesEnabled;
        }

        g_privilegesInitialized = true;

        HANDLE processToken = nullptr;
        if (!OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
            &processToken))
        {
            g_privilegesEnabled = false;
            return false;
        }

        const std::array<const wchar_t*, 3> requiredPrivileges =
        {
            SE_TCB_NAME,
            SE_ASSIGNPRIMARYTOKEN_NAME,
            SE_INCREASE_QUOTA_NAME
        };

        bool allEnabled = true;
        for (const wchar_t* privilegeName : requiredPrivileges)
        {
            if (!EnablePrivilegeOnProcessToken(processToken, privilegeName))
            {
                allEnabled = false;
            }
        }

        CloseHandle(processToken);
        g_privilegesEnabled = allEnabled;
        return g_privilegesEnabled;
    }

    void CloseProcessEntry(ProcessEntry& entry)
    {
        if (entry.processHandle != nullptr)
        {
            CloseHandle(entry.processHandle);
            entry.processHandle = nullptr;
        }

        entry.processId = 0;
    }

    bool IsProcessRunning(const ProcessEntry& entry)
    {
        if (entry.processHandle == nullptr)
        {
            return false;
        }

        const DWORD waitResult = WaitForSingleObject(entry.processHandle, 0);
        return waitResult == WAIT_TIMEOUT;
    }

    void CleanupExitedProcessesLocked()
    {
        for (auto it = g_sessionProcesses.begin(); it != g_sessionProcesses.end();)
        {
            if (!IsProcessRunning(it->second))
            {
                CloseProcessEntry(it->second);
                it = g_sessionProcesses.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    bool StringsEqualIgnoreCase(const wchar_t* left, const wchar_t* right)
    {
        if (left == nullptr || right == nullptr)
        {
            return false;
        }

        return CompareStringOrdinal(left, -1, right, -1, TRUE) == CSTR_EQUAL;
    }

    std::wstring ExtractFileName(const std::wstring& fullPath)
    {
        const size_t separatorPos = fullPath.find_last_of(L"\\/");
        if (separatorPos == std::wstring::npos)
        {
            return fullPath;
        }

        return fullPath.substr(separatorPos + 1);
    }

    bool IsTrayAppProcessPath(HANDLE processHandle, const std::wstring& trayAppPath)
    {
        if (processHandle == nullptr || trayAppPath.empty())
        {
            return false;
        }

        std::wstring processPath(32768, L'\0');
        DWORD processPathSize = static_cast<DWORD>(processPath.size());
        if (!QueryFullProcessImageNameW(processHandle, 0, processPath.data(), &processPathSize))
        {
            return false;
        }

        processPath.resize(processPathSize);
        return StringsEqualIgnoreCase(processPath.c_str(), trayAppPath.c_str());
    }

    void CollectUntrackedTrayAppProcesses(
        const std::wstring& trayAppPath,
        const std::wstring& trayAppFileName,
        const std::unordered_set<DWORD>& trackedProcessIds,
        std::vector<ProcessEntry>& processEntries)
    {
        if (trayAppPath.empty() || trayAppFileName.empty())
        {
            return;
        }

        HANDLE snapshotHandle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshotHandle == INVALID_HANDLE_VALUE)
        {
            return;
        }

        PROCESSENTRY32W processEntry = {};
        processEntry.dwSize = sizeof(processEntry);

        if (!Process32FirstW(snapshotHandle, &processEntry))
        {
            CloseHandle(snapshotHandle);
            return;
        }

        do
        {
            const DWORD processId = processEntry.th32ProcessID;
            if (processId == 0
                || processId == GetCurrentProcessId()
                || trackedProcessIds.find(processId) != trackedProcessIds.end())
            {
                continue;
            }

            if (!StringsEqualIgnoreCase(processEntry.szExeFile, trayAppFileName.c_str()))
            {
                continue;
            }

            DWORD sessionId = 0;
            if (!ProcessIdToSessionId(processId, &sessionId) || sessionId == 0)
            {
                continue;
            }

            HANDLE processHandle = OpenProcess(
                PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE,
                processId);
            if (processHandle == nullptr)
            {
                continue;
            }

            if (!IsTrayAppProcessPath(processHandle, trayAppPath))
            {
                CloseHandle(processHandle);
                continue;
            }

            ProcessEntry additionalEntry = {};
            additionalEntry.processId = processId;
            additionalEntry.processHandle = processHandle;
            processEntries.push_back(additionalEntry);
        } while (Process32NextW(snapshotHandle, &processEntry));

        CloseHandle(snapshotHandle);
    }

    void TerminateProcessEntry(ProcessEntry& processEntry, DWORD waitTimeoutMs)
    {
        if (processEntry.processHandle == nullptr)
        {
            return;
        }

        if (WaitForSingleObject(processEntry.processHandle, 0) == WAIT_TIMEOUT)
        {
            if (TerminateProcess(processEntry.processHandle, 0))
            {
                WaitForSingleObject(processEntry.processHandle, waitTimeoutMs);
            }
        }

        CloseHandle(processEntry.processHandle);
        processEntry.processHandle = nullptr;
        processEntry.processId = 0;
    }

    bool LaunchTrayAppInSession(DWORD sessionId, ProcessEntry& processEntry)
    {
        if (!EnsureLaunchPrivilegesEnabled())
        {
            return false;
        }

        HANDLE userToken = nullptr;
        if (!WTSQueryUserToken(sessionId, &userToken))
        {
            return false;
        }

        HANDLE primaryToken = nullptr;
        const BOOL duplicateResult = DuplicateTokenEx(
            userToken,
            TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
            nullptr,
            SecurityImpersonation,
            TokenPrimary,
            &primaryToken);
        CloseHandle(userToken);
        if (!duplicateResult)
        {
            return false;
        }

        LPVOID environment = nullptr;
        if (!CreateEnvironmentBlock(&environment, primaryToken, FALSE))
        {
            environment = nullptr;
        }

        STARTUPINFOW startupInfo = {};
        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startupInfo.wShowWindow = SW_HIDE;
        startupInfo.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default");

        std::wstring commandLine = L"\"";
        commandLine += g_trayAppPath;
        commandLine += L"\"";
        if (!g_launchArguments.empty())
        {
            commandLine += L" ";
            commandLine += g_launchArguments;
        }

        PROCESS_INFORMATION processInfo = {};
        const BOOL createResult = CreateProcessAsUserW(
            primaryToken,
            g_trayAppPath.c_str(),
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP,
            environment,
            nullptr,
            &startupInfo,
            &processInfo);

        if (environment != nullptr)
        {
            DestroyEnvironmentBlock(environment);
        }

        CloseHandle(primaryToken);

        if (!createResult)
        {
            return false;
        }

        CloseHandle(processInfo.hThread);
        processEntry.processId = processInfo.dwProcessId;
        processEntry.processHandle = processInfo.hProcess;
        return true;
    }
}

bool SessionLaunch::Initialize(const std::wstring& trayAppPath, const std::wstring& launchArguments)
{
    if (trayAppPath.empty())
    {
        return false;
    }

    const DWORD attributes = GetFileAttributesW(trayAppPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_trayAppPath = trayAppPath;
    g_launchArguments = launchArguments;
    return true;
}

void SessionLaunch::LaunchInAllSessions()
{
    PWTS_SESSION_INFOW sessionInfo = nullptr;
    DWORD sessionCount = 0;
    if (!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessionInfo, &sessionCount))
    {
        return;
    }

    std::vector<DWORD> sessionIds;
    sessionIds.reserve(sessionCount);

    for (DWORD index = 0; index < sessionCount; ++index)
    {
        if (sessionInfo[index].SessionId != 0)
        {
            sessionIds.push_back(sessionInfo[index].SessionId);
        }
    }

    WTSFreeMemory(sessionInfo);

    for (DWORD sessionId : sessionIds)
    {
        LaunchInSession(sessionId);
    }
}

void SessionLaunch::LaunchInSession(DWORD sessionId)
{
    if (sessionId == 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (g_trayAppPath.empty())
    {
        return;
    }

    CleanupExitedProcessesLocked();

    const auto existing = g_sessionProcesses.find(sessionId);
    if (existing != g_sessionProcesses.end() && IsProcessRunning(existing->second))
    {
        return;
    }

    if (existing != g_sessionProcesses.end())
    {
        CloseProcessEntry(existing->second);
        g_sessionProcesses.erase(existing);
    }

    ProcessEntry newEntry = {};
    if (LaunchTrayAppInSession(sessionId, newEntry))
    {
        g_sessionProcesses.emplace(sessionId, newEntry);
    }
}

void SessionLaunch::TerminateLaunchedProcesses(DWORD waitTimeoutMs)
{
    std::vector<ProcessEntry> launchedProcesses;
    std::unordered_set<DWORD> trackedProcessIds;
    std::wstring trayAppPath;
    std::wstring trayAppFileName;

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        CleanupExitedProcessesLocked();

        launchedProcesses.reserve(g_sessionProcesses.size());
        trackedProcessIds.reserve(g_sessionProcesses.size());
        trayAppPath = g_trayAppPath;
        trayAppFileName = ExtractFileName(g_trayAppPath);
        for (auto& entry : g_sessionProcesses)
        {
            launchedProcesses.push_back(entry.second);
            if (entry.second.processId != 0)
            {
                trackedProcessIds.insert(entry.second.processId);
            }

            entry.second.processHandle = nullptr;
            entry.second.processId = 0;
        }

        g_sessionProcesses.clear();
    }

    CollectUntrackedTrayAppProcesses(
        trayAppPath,
        trayAppFileName,
        trackedProcessIds,
        launchedProcesses);

    for (auto& process : launchedProcesses)
    {
        TerminateProcessEntry(process, waitTimeoutMs);
    }
}

void SessionLaunch::Shutdown()
{
    std::lock_guard<std::mutex> lock(g_stateMutex);
    for (auto& entry : g_sessionProcesses)
    {
        CloseProcessEntry(entry.second);
    }

    g_sessionProcesses.clear();
    g_trayAppPath.clear();
    g_launchArguments.clear();
    g_privilegesInitialized = false;
    g_privilegesEnabled = false;
}
