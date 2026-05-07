#pragma once

namespace RpcServer
{
    enum class StopRequestResult
    {
        Approved = 0,
        Rejected = 1,
        Failed = 2,
        ConfirmationRequired = 3
    };

    using StopRequestCallback = StopRequestResult(*)();
    using ConfirmStopCallback = StopRequestResult(*)();

    bool Start(StopRequestCallback stopRequestCallback, ConfirmStopCallback confirmStopCallback);
    void Stop();
}
