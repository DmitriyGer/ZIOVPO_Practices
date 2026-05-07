#include "RpcServer.h"

#include <rpc.h>
#include <rpcdce.h>
#include <cstdlib>

#include <mutex>
#include <string>

#include "RpcContract.h"
#include "ServiceApiState.h"

#pragma comment(lib, "Rpcrt4.lib")

namespace
{
    constexpr wchar_t kRpcProtocolSequence[] = L"ncalrpc";
    constexpr wchar_t kRpcEndpoint[] = L"TrayServiceControlEndpoint";

    std::mutex g_callbackMutex;
    RpcServer::StopRequestCallback g_stopRequestCallback = nullptr;
}

bool RpcServer::Start(StopRequestCallback callback)
{
    {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        g_stopRequestCallback = callback;
    }

    RPC_STATUS status = RpcServerUseProtseqEpW(
        reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(kRpcProtocolSequence)),
        RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
        reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(kRpcEndpoint)),
        nullptr);
    if (status != RPC_S_OK && status != RPC_S_DUPLICATE_ENDPOINT)
    {
        return false;
    }

    status = RpcServerRegisterIf(TrayRpcControl_v1_0_s_ifspec, nullptr, nullptr);
    if (status != RPC_S_OK && status != RPC_S_TYPE_ALREADY_REGISTERED)
    {
        return false;
    }

    status = RpcServerListen(1, RPC_C_LISTEN_MAX_CALLS_DEFAULT, TRUE);
    return status == RPC_S_OK || status == RPC_S_ALREADY_LISTENING;
}

void RpcServer::Stop()
{
    RpcMgmtStopServerListening(nullptr);
    RpcServerUnregisterIf(TrayRpcControl_v1_0_s_ifspec, nullptr, FALSE);

    std::lock_guard<std::mutex> lock(g_callbackMutex);
    g_stopRequestCallback = nullptr;
}

extern "C" void StopService(handle_t /*hBinding*/)
{
    RpcServer::StopRequestCallback callback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        callback = g_stopRequestCallback;
    }

    if (callback != nullptr)
    {
        callback();
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

extern "C" void* __RPC_USER midl_user_allocate(size_t size)
{
    return malloc(size);
}

extern "C" void __RPC_USER midl_user_free(void* pointer)
{
    free(pointer);
}
