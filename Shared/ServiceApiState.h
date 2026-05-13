#pragma once

#include "AntivirusDatabase.h"
#include "ApiClient.h"
#include "InMemorySession.h"
#include "RpcContract.h"

#include <mutex>
#include <string>

class ServiceApiState
{
public:
    // Возвращает singleton состояния API и сессии службы.
    static ServiceApiState& Instance();

    // Инициализирует состояние службы перед RPC-обработкой.
    void Initialize();

    // Освобождает состояние службы при завершении.
    void Shutdown();

    // Возвращает безопасную информацию о текущей аутентификации.
    TrayRpcStatusCode GetAuthInfo(TrayRpcAuthInfo* authInfo);

    // Выполняет login и сохраняет секреты только в оперативной памяти.
    TrayRpcStatusCode Login(const std::wstring& username, const std::wstring& password, TrayRpcAuthInfo* authInfo);

    // Выполняет logout и очищает токены, ticket и signature из памяти.
    TrayRpcStatusCode Logout();

    // Проверяет лицензию через API и возвращает безопасное состояние.
    TrayRpcStatusCode GetLicenseState(
        long long productId,
        const std::wstring& deviceMac,
        TrayRpcLicenseInfo* licenseInfo);

    // Активирует продукт через API и возвращает безопасное состояние лицензии.
    TrayRpcStatusCode ActivateProduct(
        const std::wstring& activationKey,
        long long productId,
        const std::wstring& deviceName,
        const std::wstring& deviceMac,
        TrayRpcLicenseInfo* licenseInfo);

    // Выполняет периодическое обновление токенов и лицензионного тикета.
    void Tick();

    // Returns current in-memory antivirus database metadata.
    Antivirus::AvDatabaseInfo GetAntivirusDatabaseInfo();

    // Scans one selected file through the antivirus engine.
    TrayRpcStatusCode ScanFile(const std::wstring& path, TrayRpcAvFileScanResult* scanResult);

    // Recursively scans one selected directory through the antivirus engine.
    TrayRpcStatusCode ScanDirectory(const std::wstring& path, TrayRpcAvDirectoryScanResult* scanResult);

    // Returns antivirus database metadata for RPC callers.
    TrayRpcStatusCode GetAvDatabaseInfo(TrayRpcAvDatabaseInfo* databaseInfo);

private:
    ServiceApiState();

    // Loads demo antivirus records into the in-memory database.
    void LoadAntivirusDatabaseLocked();

    // Возвращает безопасное состояние текущей лицензии без обращения к клиенту.
    TrayRpcStatusCode EnsureLicenseForAntivirusOperationLocked();

    // Останавливает фоновые задачи, зависящие от лицензии.
    void StopLicenseDependentTasksLocked();

    // Запускает фоновые задачи только при валидной лицензии.
    void StartLicenseDependentTasksIfAllowedLocked();

    // Очищает аутентификацию и лицензию после критической ошибки refresh.
    void ClearAuthAndLicenseLocked();

    // Пытается обновить токены по refresh token.
    bool TryRefreshTokensLocked();

    // Пытается обновить тикет лицензии по сохраненному контексту.
    bool TryRefreshLicenseTicketLocked();

    // Выполняет check endpoint и сохраняет ticket/signature в памяти.
    TrayRpcStatusCode FetchLicenseStateFromApiLocked(
        long long productId,
        const std::wstring& deviceMac,
        TrayRpcLicenseInfo* licenseInfo);

    // Заполняет безопасный ответ по текущему состоянию пользователя.
    void FillAuthInfoLocked(TrayRpcAuthInfo* authInfo) const;

    // Заполняет безопасный ответ по текущему состоянию лицензии.
    void FillLicenseInfoLocked(TrayRpcLicenseInfo* licenseInfo, TrayRpcStatusCode errorCode) const;

    // Преобразует ошибку API в код RPC для аутентификации.
    static TrayRpcStatusCode MapAuthApiError(const ApiIntegration::ApiError& error);

    // Преобразует ошибку API в код RPC для лицензии.
    static TrayRpcStatusCode MapLicenseApiError(const ApiIntegration::ApiError& error);

    // Копирует строку в фиксированный RPC-буфер.
    static void CopyToRpcTextBuffer(const std::wstring& source, wchar_t* destination, size_t destinationLength);

    std::mutex m_mutex;
    ApiIntegration::ApiClient m_apiClient;
    ApiIntegration::InMemorySession m_session;
    Antivirus::InMemoryAvDatabase m_avDatabase;

    bool m_initialized = false;
    bool m_authenticated = false;
    ApiIntegration::AuthUserInfo m_userInfo;

    long long m_lastProductId = 0;
    std::wstring m_lastDeviceName;
    std::wstring m_lastDeviceMac;
    std::wstring m_lastActivationKey;

    bool m_licenseTasksRunning = false;
};
