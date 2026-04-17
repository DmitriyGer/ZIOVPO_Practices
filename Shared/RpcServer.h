#pragma once

namespace RpcServer
{
    using StopRequestCallback = void(*)();

    bool Start(StopRequestCallback callback);
    void Stop();
}
