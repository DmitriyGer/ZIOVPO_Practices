#include "RpcServer.h"

#include <rpc.h>
#include <rpcdce.h>
#include <cstdlib>

#include <mutex>

#include "RpcContract.h"

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

extern "C" void* __RPC_USER midl_user_allocate(size_t size)
{
    return malloc(size);
}

extern "C" void __RPC_USER midl_user_free(void* pointer)
{
    free(pointer);
}
