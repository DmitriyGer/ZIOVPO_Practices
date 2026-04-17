#include "RpcClient.h"

#include <rpc.h>
#include <rpcdce.h>
#include <cstdlib>

#include "RpcContract.h"

#pragma comment(lib, "Rpcrt4.lib")

bool RpcClient::RequestServiceStop()
{
    constexpr wchar_t kRpcProtocolSequence[] = L"ncalrpc";
    constexpr wchar_t kRpcEndpoint[] = L"TrayServiceControlEndpoint";

    RPC_WSTR stringBinding = nullptr;
    RPC_BINDING_HANDLE bindingHandle = nullptr;
    bool isSuccess = true;

    RPC_STATUS status = RpcStringBindingComposeW(
        nullptr,
        reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(kRpcProtocolSequence)),
        nullptr,
        reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(kRpcEndpoint)),
        nullptr,
        &stringBinding);
    if (status != RPC_S_OK)
    {
        return false;
    }

    status = RpcBindingFromStringBindingW(stringBinding, &bindingHandle);
    if (status != RPC_S_OK)
    {
        RpcStringFreeW(&stringBinding);
        return false;
    }

    RpcTryExcept
    {
        ::StopService(bindingHandle);
    }
    RpcExcept(1)
    {
        isSuccess = false;
    }
    RpcEndExcept;

    RpcBindingFree(&bindingHandle);
    RpcStringFreeW(&stringBinding);

    return isSuccess;
}

extern "C" void* __RPC_USER midl_user_allocate(size_t size)
{
    return malloc(size);
}

extern "C" void __RPC_USER midl_user_free(void* pointer)
{
    free(pointer);
}
