#pragma once

enum class StopConfirmationResult
{
    Approved = 0,
    Rejected = 1,
    Failed = 2
};

StopConfirmationResult ShowStopConfirmationOnIsolatedDesktop();
