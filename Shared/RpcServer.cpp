#include "RpcServer.h"

#include <rpc.h>
#include <rpcdce.h>
#include <sddl.h>
#include <cstdlib>

#include <memory>
#include <mutex>
#include <string>

#include "RpcContract.h"
#include "ServiceApiState.h"

#pragma comment(lib, "Rpcrt4.lib")
#pragma comment(lib, "Advapi32.lib")

namespace
{
    constexpr wchar_t kRpcProtocolSequence[] = L"ncalrpc";
    constexpr wchar_t kRpcEndpoint[] = L"TrayServiceControlEndpoint";
    constexpr wchar_t kRpcEndpointSddl[] = L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;IU)";

    std::mutex g_callbackMutex;
    RpcServer::StopRequestCallback g_stopRequestCallback = nullptr;
    RpcServer::ConfirmStopCallback g_confirmStopCallback = nullptr;

    struct LocalFreeDeleter
    {
        void operator()(void* pointer) const noexcept
        {
            if (pointer != nullptr)
            {
                LocalFree(pointer);
            }
        }
    };

    using ScopedLocalMemory = std::unique_ptr<void, LocalFreeDeleter>;

    std::wstring FormatRpcError(DWORD errorCode)
    {
        wchar_t* rawMessage = nullptr;
        const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER
            | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS;
        const DWORD result = FormatMessageW(
            flags,
            nullptr,
            errorCode,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPWSTR>(&rawMessage),
            0,
            nullptr);
        if (result == 0 || rawMessage == nullptr)
        {
            return L"(no text)";
        }

        std::wstring message = rawMessage;
        LocalFree(rawMessage);
        return message;
    }

    void LogRpcInfo(const wchar_t* message)
    {
        std::wstring text = L"[TrayService][RPC] ";
        text += message != nullptr ? message : L"(no message)";
        text += L"\r\n";
        OutputDebugStringW(text.c_str());
    }

    void LogRpcStatus(const wchar_t* stepName, RPC_STATUS status)
    {
        std::wstring text = L"[TrayService][RPC] ";
        text += stepName != nullptr ? stepName : L"(unknown step)";
        text += status == RPC_S_OK ? L" succeeded." : L" returned status.";
        text += L" status=";
        text += std::to_wstring(status);
        text += L" message=";
        text += FormatRpcError(status);
        text += L"\r\n";
        OutputDebugStringW(text.c_str());
    }

    bool BuildEndpointSecurityDescriptor(ScopedLocalMemory& descriptor)
    {
        PSECURITY_DESCRIPTOR rawDescriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                kRpcEndpointSddl,
                SDDL_REVISION_1,
                &rawDescriptor,
                nullptr))
        {
            std::wstring text = L"[TrayService][RPC] ConvertStringSecurityDescriptorToSecurityDescriptorW failed."
                L" error=";
            text += std::to_wstring(GetLastError());
            text += L" message=";
            text += FormatRpcError(GetLastError());
            text += L"\r\n";
            OutputDebugStringW(text.c_str());
            return false;
        }

        descriptor.reset(rawDescriptor);
        LogRpcInfo(L"Built explicit ncalrpc endpoint security descriptor for SYSTEM, Administrators and INTERACTIVE users.");
        return true;
    }
}

bool RpcServer::Start(StopRequestCallback stopRequestCallback, ConfirmStopCallback confirmStopCallback)
{
    LogRpcInfo(L"RPC server start requested.");

    {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        g_stopRequestCallback = stopRequestCallback;
        g_confirmStopCallback = confirmStopCallback;
    }

    ScopedLocalMemory endpointSecurityDescriptor;
    if (!BuildEndpointSecurityDescriptor(endpointSecurityDescriptor))
    {
        return false;
    }

    RPC_STATUS status = RpcServerUseProtseqEpW(
        reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(kRpcProtocolSequence)),
        RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
        reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(kRpcEndpoint)),
        endpointSecurityDescriptor.get());
    LogRpcStatus(L"RpcServerUseProtseqEpW", status);
    if (status != RPC_S_OK && status != RPC_S_DUPLICATE_ENDPOINT)
    {
        return false;
    }

    status = RpcServerRegisterIfEx(
        TrayRpcControl_v1_0_s_ifspec,
        nullptr,
        nullptr,
        RPC_IF_ALLOW_LOCAL_ONLY,
        RPC_C_LISTEN_MAX_CALLS_DEFAULT,
        nullptr);
    LogRpcStatus(L"RpcServerRegisterIfEx", status);
    if (status != RPC_S_OK && status != RPC_S_TYPE_ALREADY_REGISTERED)
    {
        return false;
    }

    status = RpcServerListen(1, RPC_C_LISTEN_MAX_CALLS_DEFAULT, TRUE);
    LogRpcStatus(L"RpcServerListen", status);
    if (status != RPC_S_OK && status != RPC_S_ALREADY_LISTENING)
    {
        return false;
    }

    LogRpcInfo(L"RPC server started and listening on TrayServiceControlEndpoint.");
    return true;
}

void RpcServer::Stop()
{
    LogRpcInfo(L"RPC server stop requested.");

    RPC_STATUS status = RpcMgmtStopServerListening(nullptr);
    LogRpcStatus(L"RpcMgmtStopServerListening", status);

    status = RpcServerUnregisterIf(TrayRpcControl_v1_0_s_ifspec, nullptr, FALSE);
    LogRpcStatus(L"RpcServerUnregisterIf", status);

    std::lock_guard<std::mutex> lock(g_callbackMutex);
    g_stopRequestCallback = nullptr;
    g_confirmStopCallback = nullptr;
}

extern "C" TrayRpcStopResult StopService(handle_t /*hBinding*/)
{
    LogRpcInfo(L"StopService RPC request received.");

    RpcServer::StopRequestCallback callback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        callback = g_stopRequestCallback;
    }

    if (callback == nullptr)
    {
        LogRpcInfo(L"StopService RPC request denied because stop callback is not registered.");
        return TRAY_RPC_STOP_FAILED;
    }

    const RpcServer::StopRequestResult result = callback();
    switch (result)
    {
    case RpcServer::StopRequestResult::Approved:
        LogRpcInfo(L"StopService RPC request approved by service callback.");
        return TRAY_RPC_STOP_APPROVED;
    case RpcServer::StopRequestResult::Rejected:
        LogRpcInfo(L"StopService RPC request rejected by service callback.");
        return TRAY_RPC_STOP_REJECTED;
    case RpcServer::StopRequestResult::ConfirmationRequired:
        LogRpcInfo(L"StopService RPC request requires confirmation in the caller user session.");
        return TRAY_RPC_STOP_CONFIRMATION_REQUIRED;
    default:
        LogRpcInfo(L"StopService RPC request failed inside service callback before approval.");
        return TRAY_RPC_STOP_FAILED;
    }
}

extern "C" TrayRpcStopResult ConfirmStopService(handle_t /*hBinding*/)
{
    LogRpcInfo(L"ConfirmStopService RPC request received.");

    RpcServer::ConfirmStopCallback callback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        callback = g_confirmStopCallback;
    }

    if (callback == nullptr)
    {
        LogRpcInfo(L"ConfirmStopService RPC request denied because confirm callback is not registered.");
        return TRAY_RPC_STOP_FAILED;
    }

    const RpcServer::StopRequestResult result = callback();
    switch (result)
    {
    case RpcServer::StopRequestResult::Approved:
        LogRpcInfo(L"ConfirmStopService RPC request approved by service callback.");
        return TRAY_RPC_STOP_APPROVED;
    case RpcServer::StopRequestResult::Rejected:
        LogRpcInfo(L"ConfirmStopService RPC request rejected by service callback.");
        return TRAY_RPC_STOP_REJECTED;
    default:
        LogRpcInfo(L"ConfirmStopService RPC request failed inside service callback.");
        return TRAY_RPC_STOP_FAILED;
    }
}

extern "C" TrayRpcStatusCode GetAuthInfo(
    handle_t /*hBinding*/,
    TrayRpcAuthInfo* authInfo)
{
    // Returns safe current authentication state.
    try
    {
        return ServiceApiState::Instance().GetAuthInfo(authInfo);
    }
    catch (...)
    {
        return TRAY_RPC_SERVER_ERROR;
    }
}

extern "C" TrayRpcStatusCode Login(
    handle_t /*hBinding*/,
    wchar_t* username,
    wchar_t* password,
    TrayRpcAuthInfo* authInfo)
{
    // Executes login and keeps tokens only in service memory.
    const std::wstring userNameValue = username != nullptr ? username : L"";
    const std::wstring passwordValue = password != nullptr ? password : L"";

    try
    {
        return ServiceApiState::Instance().Login(userNameValue, passwordValue, authInfo);
    }
    catch (...)
    {
        return TRAY_RPC_SERVER_ERROR;
    }
}

extern "C" TrayRpcStatusCode Logout(handle_t /*hBinding*/)
{
    // Executes logout and clears all in-memory secrets.
    try
    {
        return ServiceApiState::Instance().Logout();
    }
    catch (...)
    {
        return TRAY_RPC_SERVER_ERROR;
    }
}

extern "C" TrayRpcStatusCode GetLicenseState(
    handle_t /*hBinding*/,
    hyper productId,
    wchar_t* deviceMac,
    TrayRpcLicenseInfo* licenseInfo)
{
    // Returns safe current license state.
    const std::wstring deviceMacValue = deviceMac != nullptr ? deviceMac : L"";

    try
    {
        return ServiceApiState::Instance().GetLicenseState(
            static_cast<long long>(productId),
            deviceMacValue,
            licenseInfo);
    }
    catch (...)
    {
        return TRAY_RPC_SERVER_ERROR;
    }
}

extern "C" TrayRpcStatusCode ActivateProduct(
    handle_t /*hBinding*/,
    wchar_t* activationKey,
    hyper productId,
    wchar_t* deviceName,
    wchar_t* deviceMac,
    TrayRpcLicenseInfo* licenseInfo)
{
    // Executes product activation and returns safe license state.
    const std::wstring activationKeyValue = activationKey != nullptr ? activationKey : L"";
    const std::wstring deviceNameValue = deviceName != nullptr ? deviceName : L"";
    const std::wstring deviceMacValue = deviceMac != nullptr ? deviceMac : L"";

    try
    {
        return ServiceApiState::Instance().ActivateProduct(
            activationKeyValue,
            static_cast<long long>(productId),
            deviceNameValue,
            deviceMacValue,
            licenseInfo);
    }
    catch (...)
    {
        return TRAY_RPC_SERVER_ERROR;
    }
}

extern "C" TrayRpcStatusCode ScanFile(
    handle_t /*hBinding*/,
    wchar_t* path,
    TrayRpcAvFileScanResult* scanResult)
{
    // Scans one selected file through the service antivirus engine.
    const std::wstring pathValue = path != nullptr ? path : L"";

    try
    {
        return ServiceApiState::Instance().ScanFile(pathValue, scanResult);
    }
    catch (...)
    {
        return TRAY_RPC_SERVER_ERROR;
    }
}

extern "C" TrayRpcStatusCode ScanDirectory(
    handle_t /*hBinding*/,
    wchar_t* path,
    TrayRpcAvDirectoryScanResult* scanResult)
{
    // Recursively scans one selected directory through the service antivirus engine.
    const std::wstring pathValue = path != nullptr ? path : L"";

    try
    {
        return ServiceApiState::Instance().ScanDirectory(pathValue, scanResult);
    }
    catch (...)
    {
        return TRAY_RPC_SERVER_ERROR;
    }
}

extern "C" TrayRpcStatusCode ScanFixedDrives(
    handle_t /*hBinding*/,
    TrayRpcAvDirectoryScanResult* scanResult)
{
    // Scans all fixed local drives through the service antivirus engine.
    try
    {
        return ServiceApiState::Instance().ScanFixedDrives(scanResult);
    }
    catch (...)
    {
        return TRAY_RPC_SERVER_ERROR;
    }
}

extern "C" TrayRpcStatusCode GetAvDatabaseInfo(
    handle_t /*hBinding*/,
    TrayRpcAvDatabaseInfo* databaseInfo)
{
    // Returns service-side in-memory antivirus database metadata.
    try
    {
        return ServiceApiState::Instance().GetAvDatabaseInfo(databaseInfo);
    }
    catch (...)
    {
        return TRAY_RPC_SERVER_ERROR;
    }
}

extern "C" void* __RPC_USER midl_user_allocate(size_t size)
{
    return malloc(size);
}

extern "C" void __RPC_USER midl_user_free(void* pointer)
{
    free(pointer);
}
