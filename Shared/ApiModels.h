#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace ApiIntegration
{
    enum class ApiErrorCode
    {
        None,
        Network,
        HttpStatus,
        Unauthorized,
        Forbidden,
        NotFound,
        Conflict,
        BadRequest,
        Parse,
        InvalidInput,
        Unknown
    };

    struct ApiError
    {
        ApiErrorCode code = ApiErrorCode::None;
        unsigned long httpStatus = 0;
        std::wstring error;
        std::wstring message;
    };

    using AuthError = ApiError;
    using LicenseError = ApiError;

    struct AuthTokens
    {
        std::wstring accessToken;
        std::wstring refreshToken;
        std::optional<std::chrono::system_clock::time_point> accessExpiresAtUtc;
        std::optional<std::chrono::system_clock::time_point> refreshExpiresAtUtc;
        std::optional<long long> sessionId;
    };

    struct AuthUserInfo
    {
        std::optional<long long> id;
        std::wstring username;
        std::wstring role;
    };

    struct LicenseTicket
    {
        std::optional<std::chrono::system_clock::time_point> serverDateUtc;
        std::optional<long long> ticketTtlSeconds;
        std::optional<std::chrono::system_clock::time_point> activationDateUtc;
        std::optional<std::chrono::system_clock::time_point> expirationDateUtc;
        std::optional<long long> userId;
        std::optional<long long> deviceId;
        bool blocked = false;
    };

    struct LicenseState
    {
        bool hasLicense = false;
        bool blocked = false;
        bool expired = false;
        std::optional<std::chrono::system_clock::time_point> expirationDateUtc;
        std::optional<std::chrono::system_clock::time_point> nextRefreshAtUtc;
        std::wstring errorMessage;
    };
}
