#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace RpcClient
{
    enum class StopRequestResult
    {
        Approved = 0,
        Rejected = 1,
        Failed = 2,
        ConfirmationRequired = 3,
        TransportError = 100
    };

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

    enum class AvScanVerdict
    {
        Clean = 0,
        Infected = 1,
        Error = 2
    };

    enum class AvObjectType
    {
        Unknown = 0,
        Pe = 1,
        ScriptText = 2
    };

    enum class AvDatabaseLoadStatus
    {
        NotLoaded = 0,
        Loaded = 1
    };

    struct AvDatabaseInfo
    {
        std::optional<std::chrono::system_clock::time_point> releaseDateUtc;
        unsigned long long recordCount = 0;
        AvDatabaseLoadStatus loadStatus = AvDatabaseLoadStatus::NotLoaded;
    };

    struct AvFileScanResult
    {
        AvScanVerdict verdict = AvScanVerdict::Clean;
        std::wstring path;
        AvObjectType objectType = AvObjectType::Unknown;
        unsigned long long detectionOffset = 0;
        std::wstring recordId;
        std::wstring objectSignatureHex;
        std::wstring message;
    };

    struct AvDirectoryScanResult
    {
        std::wstring path;
        unsigned long long totalScanned = 0;
        unsigned long long infectedCount = 0;
        unsigned long long errorCount = 0;
        bool truncated = false;
        std::vector<AvFileScanResult> results;
        std::wstring message;
    };

    // Отправляет существующую команду остановки службы.
    StopRequestResult RequestServiceStop();
    StopRequestResult ConfirmServiceStop();

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

    // Scans one selected file through TrayService RPC.
    RpcStatusCode ScanFile(const std::wstring& path, AvFileScanResult& scanResult);

    // Recursively scans one selected directory through TrayService RPC.
    RpcStatusCode ScanDirectory(const std::wstring& path, AvDirectoryScanResult& scanResult);

    // Returns in-memory antivirus database metadata through TrayService RPC.
    RpcStatusCode GetAvDatabaseInfo(AvDatabaseInfo& databaseInfo);
}
