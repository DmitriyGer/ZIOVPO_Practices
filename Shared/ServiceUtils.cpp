#include "ServiceUtils.h"

#include <memory>
#include <string>
#include <type_traits>

namespace
{
    struct ScHandleCloser
    {
        void operator()(SC_HANDLE handle) const
        {
            if (handle != nullptr)
            {
                CloseServiceHandle(handle);
            }
        }
    };

    using ScopedScHandle = std::unique_ptr<std::remove_pointer_t<SC_HANDLE>, ScHandleCloser>;

    bool QueryServiceStatusInternal(SC_HANDLE serviceHandle, SERVICE_STATUS_PROCESS& serviceStatus)
    {
        DWORD bytesNeeded = 0;
        ZeroMemory(&serviceStatus, sizeof(serviceStatus));

        return QueryServiceStatusEx(
            serviceHandle,
            SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&serviceStatus),
            sizeof(serviceStatus),
            &bytesNeeded) != FALSE;
    }

    bool WaitForServiceStateInternal(SC_HANDLE serviceHandle, DWORD expectedState, DWORD timeoutMs)
    {
        const ULONGLONG startTickCount = GetTickCount64();
        SERVICE_STATUS_PROCESS serviceStatus = {};

        for (;;)
        {
            if (!QueryServiceStatusInternal(serviceHandle, serviceStatus))
            {
                return false;
            }

            if (serviceStatus.dwCurrentState == expectedState)
            {
                return true;
            }

            if ((GetTickCount64() - startTickCount) >= timeoutMs)
            {
                return false;
            }

            DWORD waitDelayMs = serviceStatus.dwWaitHint;
            if (waitDelayMs < 200)
            {
                waitDelayMs = 200;
            }
            else if (waitDelayMs > 1000)
            {
                waitDelayMs = 1000;
            }

            Sleep(waitDelayMs);
        }
    }
}

bool ServiceUtils::QueryServiceStatus(const wchar_t* serviceName, SERVICE_STATUS_PROCESS& serviceStatus, DWORD* lastError)
{
    if (lastError != nullptr)
    {
        *lastError = ERROR_SUCCESS;
    }

    if (serviceName == nullptr || serviceName[0] == L'\0')
    {
        if (lastError != nullptr)
        {
            *lastError = ERROR_INVALID_PARAMETER;
        }
        return false;
    }

    ScopedScHandle scmHandle(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scmHandle)
    {
        if (lastError != nullptr)
        {
            *lastError = GetLastError();
        }
        return false;
    }

    ScopedScHandle serviceHandle(OpenServiceW(scmHandle.get(), serviceName, SERVICE_QUERY_STATUS));
    if (!serviceHandle)
    {
        if (lastError != nullptr)
        {
            *lastError = GetLastError();
        }
        return false;
    }

    const bool isQuerySuccessful = QueryServiceStatusInternal(serviceHandle.get(), serviceStatus);
    if (!isQuerySuccessful && lastError != nullptr)
    {
        *lastError = GetLastError();
    }

    return isQuerySuccessful;
}

bool ServiceUtils::WaitForServiceState(const wchar_t* serviceName, DWORD expectedState, DWORD timeoutMs)
{
    if (serviceName == nullptr || serviceName[0] == L'\0')
    {
        return false;
    }

    ScopedScHandle scmHandle(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scmHandle)
    {
        return false;
    }

    ScopedScHandle serviceHandle(OpenServiceW(scmHandle.get(), serviceName, SERVICE_QUERY_STATUS));
    if (!serviceHandle)
    {
        return false;
    }

    return WaitForServiceStateInternal(serviceHandle.get(), expectedState, timeoutMs);
}

bool ServiceUtils::StartServiceAndWaitRunning(const wchar_t* serviceName, DWORD timeoutMs)
{
    if (serviceName == nullptr || serviceName[0] == L'\0')
    {
        return false;
    }

    ScopedScHandle scmHandle(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scmHandle)
    {
        return false;
    }

    ScopedScHandle serviceHandle(OpenServiceW(scmHandle.get(), serviceName, SERVICE_QUERY_STATUS | SERVICE_START));
    if (!serviceHandle)
    {
        return false;
    }

    SERVICE_STATUS_PROCESS serviceStatus = {};
    if (!QueryServiceStatusInternal(serviceHandle.get(), serviceStatus))
    {
        return false;
    }

    if (serviceStatus.dwCurrentState == SERVICE_RUNNING)
    {
        return true;
    }

    if (!StartServiceW(serviceHandle.get(), 0, nullptr))
    {
        const DWORD startError = GetLastError();
        if (startError != ERROR_SERVICE_ALREADY_RUNNING)
        {
            if (!QueryServiceStatusInternal(serviceHandle.get(), serviceStatus))
            {
                return false;
            }

            if (serviceStatus.dwCurrentState != SERVICE_START_PENDING)
            {
                return false;
            }
        }
    }

    return WaitForServiceStateInternal(serviceHandle.get(), SERVICE_RUNNING, timeoutMs);
}

bool ServiceUtils::EnsureServiceInstalled(const wchar_t* serviceName, const wchar_t* serviceBinaryPath, DWORD* lastError)
{
    if (lastError != nullptr)
    {
        *lastError = ERROR_SUCCESS;
    }

    if (serviceName == nullptr || serviceName[0] == L'\0'
        || serviceBinaryPath == nullptr || serviceBinaryPath[0] == L'\0')
    {
        if (lastError != nullptr)
        {
            *lastError = ERROR_INVALID_PARAMETER;
        }

        return false;
    }

    const DWORD fileAttributes = GetFileAttributesW(serviceBinaryPath);
    if (fileAttributes == INVALID_FILE_ATTRIBUTES || (fileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        if (lastError != nullptr)
        {
            *lastError = ERROR_FILE_NOT_FOUND;
        }

        return false;
    }

    ScopedScHandle scmHandle(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE));
    if (!scmHandle)
    {
        if (lastError != nullptr)
        {
            *lastError = GetLastError();
        }

        return false;
    }

    ScopedScHandle serviceHandle(OpenServiceW(scmHandle.get(), serviceName, SERVICE_QUERY_STATUS));
    if (serviceHandle)
    {
        return true;
    }

    const DWORD openServiceError = GetLastError();
    if (openServiceError != ERROR_SERVICE_DOES_NOT_EXIST)
    {
        if (lastError != nullptr)
        {
            *lastError = openServiceError;
        }

        return false;
    }

    std::wstring serviceBinaryPathForScm = L"\"";
    serviceBinaryPathForScm += serviceBinaryPath;
    serviceBinaryPathForScm += L"\"";

    serviceHandle.reset(CreateServiceW(
        scmHandle.get(),
        serviceName,
        serviceName,
        SERVICE_QUERY_STATUS | SERVICE_START,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        serviceBinaryPathForScm.c_str(),
        nullptr,
        nullptr,
        nullptr,
        L"LocalSystem",
        nullptr));
    if (!serviceHandle)
    {
        if (lastError != nullptr)
        {
            *lastError = GetLastError();
        }

        return false;
    }

    return true;
}
