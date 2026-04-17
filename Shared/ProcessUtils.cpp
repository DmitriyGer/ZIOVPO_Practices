#include "ProcessUtils.h"

#include <tlhelp32.h>

namespace
{
    std::wstring ExtractFileName(const std::wstring& fullPath)
    {
        const size_t separatorPos = fullPath.find_last_of(L"\\/");
        if (separatorPos == std::wstring::npos)
        {
            return fullPath;
        }

        return fullPath.substr(separatorPos + 1);
    }
    bool FindProcessEntryById(DWORD processId, PROCESSENTRY32W& processEntry)
    {
        if (processId == 0)
        {
            return false;
        }

        const HANDLE snapshotHandle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshotHandle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        PROCESSENTRY32W currentEntry = {};
        currentEntry.dwSize = sizeof(currentEntry);

        bool found = false;
        if (Process32FirstW(snapshotHandle, &currentEntry))
        {
            do
            {
                if (currentEntry.th32ProcessID == processId)
                {
                    processEntry = currentEntry;
                    found = true;
                    break;
                }
            } while (Process32NextW(snapshotHandle, &currentEntry));
        }

        CloseHandle(snapshotHandle);
        return found;
    }
}

bool ProcessUtils::GetParentProcessId(DWORD processId, DWORD& parentProcessId)
{
    parentProcessId = 0;

    PROCESSENTRY32W processEntry = {};
    if (!FindProcessEntryById(processId, processEntry))
    {
        return false;
    }

    parentProcessId = processEntry.th32ParentProcessID;
    return true;
}

bool ProcessUtils::GetProcessNameById(DWORD processId, std::wstring& processName)
{
    processName.clear();

    PROCESSENTRY32W processEntry = {};
    if (!FindProcessEntryById(processId, processEntry))
    {
        return false;
    }

    processName = ExtractFileName(processEntry.szExeFile);
    return !processName.empty();
}

bool ProcessUtils::IsCurrentProcessParentNamed(const wchar_t* expectedProcessName)
{
    if (expectedProcessName == nullptr || expectedProcessName[0] == L'\0')
    {
        return false;
    }

    DWORD parentProcessId = 0;
    if (!GetParentProcessId(GetCurrentProcessId(), parentProcessId))
    {
        return false;
    }

    std::wstring parentProcessName;
    if (!GetProcessNameById(parentProcessId, parentProcessName))
    {
        return false;
    }

    return CompareStringOrdinal(
        parentProcessName.c_str(),
        -1,
        expectedProcessName,
        -1,
        TRUE) == CSTR_EQUAL;
}
