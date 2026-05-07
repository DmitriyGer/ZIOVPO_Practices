#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace RpcClient
{
    enum class RpcStatusCode
    {
        Ok = 0,
        InvalidArgument = 1,
        NotAuthenticated = 2,
        AuthFailed = 3,
        NetworkError = 4,
        ServerError = 5,
        NoLicense = 6,
        LicenseExpired = 7,
        LicenseBlocked = 8,
        TransportError = 100
    };

    struct AuthInfo
    {
        bool authenticated = false;
        std::optional<long long> userId;
        std::wstring username;
    };

    struct LicenseInfo
    {
        bool hasLicense = false;
        bool blocked = false;
        bool expired = false;
        std::optional<std::chrono::system_clock::time_point> expirationDateUtc;
        RpcStatusCode errorCode = RpcStatusCode::Ok;
    };

    // Отправляет существующую команду остановки службы.
    bool RequestServiceStop();

    // Возвращает безопасную информацию о текущей аутентификации.
    RpcStatusCode GetCurrentAuthInfo(AuthInfo& authInfo);

    // Выполняет login через RPC и возвращает безопасный auth-state.
    RpcStatusCode Login(const std::wstring& username, const std::wstring& password, AuthInfo& authInfo);

    // Выполняет logout через RPC.
    RpcStatusCode Logout();

    // Запрашивает безопасный статус лицензии через RPC.
    RpcStatusCode GetLicenseState(long long productId, const std::wstring& deviceMac, LicenseInfo& licenseInfo);

    // Выполняет активацию продукта через RPC.
    RpcStatusCode ActivateProduct(
        const std::wstring& activationKey,
        long long productId,
        const std::wstring& deviceName,
        const std::wstring& deviceMac,
        LicenseInfo& licenseInfo);
}
