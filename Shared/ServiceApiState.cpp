#include "ServiceApiState.h"

#include "AntivirusDatabaseStore.h"
#include "AntivirusScanner.h"

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <filesystem>
#include <optional>
#include <utility>

namespace
{
    // constexpr wchar_t kApiBaseUrl[] = L"https://10.11.134.140:8443/";
    constexpr wchar_t kApiBaseUrl[] = L"https://192.168.1.56:8443/";
    constexpr size_t kMaxRpcDirectoryResults = 32;
    constexpr auto kAvDatabaseUpdateCheckInterval = std::chrono::minutes(30);
    constexpr auto kFixedDriveScanInterval = std::chrono::hours(24);
    constexpr bool kEnableScheduledFixedDriveScan = false;

    // Copies text into a fixed-size RPC wchar_t buffer.
    template <size_t Size>
    void CopyToRpcBuffer(const std::wstring& source, wchar_t (&destination)[Size])
    {
        destination[0] = L'\0';
        if (!source.empty())
        {
            wcsncpy_s(destination, Size, source.c_str(), _TRUNCATE);
        }
    }

    // Converts an internal scan status to the RPC verdict enum.
    TrayRpcAvScanVerdict ToRpcVerdict(Antivirus::AvScanStatus status)
    {
        switch (status)
        {
        case Antivirus::AvScanStatus::Detected:
            return TRAY_RPC_AV_SCAN_INFECTED;
        case Antivirus::AvScanStatus::Error:
            return TRAY_RPC_AV_SCAN_ERROR;
        case Antivirus::AvScanStatus::Clean:
        default:
            return TRAY_RPC_AV_SCAN_CLEAN;
        }
    }

    // Converts an internal object type to the RPC object type enum.
    TrayRpcAvObjectType ToRpcObjectType(Antivirus::AvObjectType objectType)
    {
        switch (objectType)
        {
        case Antivirus::AvObjectType::Pe:
            return TRAY_RPC_AV_OBJECT_PE;
        case Antivirus::AvObjectType::ScriptText:
            return TRAY_RPC_AV_OBJECT_SCRIPT_TEXT;
        case Antivirus::AvObjectType::Unknown:
        default:
            return TRAY_RPC_AV_OBJECT_UNKNOWN;
        }
    }

    // Converts an internal database load status to the RPC enum.
    TrayRpcAvDatabaseLoadStatus ToRpcDatabaseLoadStatus(Antivirus::AvDatabaseLoadStatus status)
    {
        return status == Antivirus::AvDatabaseLoadStatus::Loaded
            ? TRAY_RPC_AV_DATABASE_LOADED
            : TRAY_RPC_AV_DATABASE_NOT_LOADED;
    }

    // Converts database source metadata to text for RPC/UI.
    std::wstring AvDatabaseSourceToText(Antivirus::AvDatabaseFileSource source)
    {
        switch (source)
        {
        case Antivirus::AvDatabaseFileSource::Main:
            return L"main";
        case Antivirus::AvDatabaseFileSource::Backup:
            return L"backup";
        case Antivirus::AvDatabaseFileSource::Default:
            return L"default";
        case Antivirus::AvDatabaseFileSource::Updated:
            return L"updated";
        case Antivirus::AvDatabaseFileSource::None:
        default:
            return L"none";
        }
    }

    // Converts an internal file scan result to a fixed-size RPC result.
    void FillRpcFileScanResult(
        const Antivirus::AvFileScanResult& source,
        TrayRpcAvFileScanResult* destination)
    {
        if (destination == nullptr)
        {
            return;
        }

        *destination = {};
        destination->verdict = ToRpcVerdict(source.Status);
        CopyToRpcBuffer(source.Path, destination->path);
        destination->objectType = ToRpcObjectType(source.ObjectType);
        destination->detectionOffset = static_cast<hyper>(source.DetectionOffset);
        CopyToRpcBuffer(source.RecordId, destination->recordId);
        CopyToRpcBuffer(source.ObjectSignatureHex, destination->objectSignatureHex);
        CopyToRpcBuffer(source.Message, destination->message);
    }

    // Converts an internal directory scan result to a fixed-size RPC result.
    void FillRpcDirectoryScanResult(
        const Antivirus::AvDirectoryScanResult& source,
        TrayRpcAvDirectoryScanResult* destination)
    {
        if (destination == nullptr)
        {
            return;
        }

        *destination = {};
        CopyToRpcBuffer(source.Path, destination->path);
        destination->totalScanned = static_cast<hyper>(source.TotalScanned);
        destination->infectedCount = static_cast<hyper>(source.InfectedCount);
        destination->errorCount = static_cast<hyper>(source.ErrorCount);
        CopyToRpcBuffer(source.Message, destination->message);

        const size_t copyCount = (std::min)(source.Results.size(), kMaxRpcDirectoryResults);
        destination->resultCount = static_cast<int>(copyCount);
        destination->truncated = source.Results.size() > kMaxRpcDirectoryResults ? 1 : 0;
        for (size_t index = 0; index < copyCount; ++index)
        {
            FillRpcFileScanResult(source.Results[index], &destination->results[index]);
        }
    }

    // Converts database metadata to the RPC structure.
    void FillRpcAvDatabaseInfo(
        const Antivirus::AvDatabaseInfo& source,
        TrayRpcAvDatabaseInfo* destination)
    {
        if (destination == nullptr)
        {
            return;
        }

        *destination = {};
        destination->recordCount = static_cast<hyper>(source.RecordCount);
        destination->loadStatus = ToRpcDatabaseLoadStatus(source.LoadStatus);
        CopyToRpcBuffer(AvDatabaseSourceToText(source.Source), destination->source);
        CopyToRpcBuffer(source.LastUpdateStatus, destination->lastUpdateStatus);

        if (source.ReleaseDateUtc != std::chrono::system_clock::time_point{})
        {
            destination->hasReleaseDate = 1;
            destination->releaseEpochSeconds = static_cast<hyper>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    source.ReleaseDateUtc.time_since_epoch()).count());
        }

        if (source.LastSuccessfulLoadUtc != std::chrono::system_clock::time_point{})
        {
            destination->hasLastSuccessfulLoad = 1;
            destination->lastSuccessfulLoadEpochSeconds = static_cast<hyper>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    source.LastSuccessfulLoadUtc.time_since_epoch()).count());
        }
    }

    // Converts antivirus access status to a user-facing scan error message.
    std::wstring AntivirusAccessStatusToMessage(TrayRpcStatusCode status)
    {
        switch (status)
        {
        case TRAY_RPC_NO_LICENSE:
            return L"Active license is required for scanning.";
        case TRAY_RPC_LICENSE_EXPIRED:
            return L"License is expired. Scanning is unavailable.";
        case TRAY_RPC_LICENSE_BLOCKED:
            return L"License is blocked. Scanning is unavailable.";
        case TRAY_RPC_NOT_AUTHENTICATED:
            return L"Authentication is required for scanning.";
        default:
            return L"Antivirus scanning is unavailable.";
        }
    }
}

ServiceApiState::ServiceApiState()
    : m_apiClient(kApiBaseUrl)
{
}

// Возвращает singleton состояния API и сессии службы.
ServiceApiState& ServiceApiState::Instance()
{
    static ServiceApiState instance;
    return instance;
}

// Инициализирует состояние службы перед RPC-обработкой.
void ServiceApiState::Initialize()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_initialized = true;
    m_authenticated = false;
    m_userInfo = {};
    m_session.ClearAll();
    m_lastProductId = 0;
    m_lastDeviceName.clear();
    m_lastDeviceMac.clear();
    m_lastActivationKey.clear();
    m_licenseTasksRunning = false;
    LoadAntivirusDatabaseLocked();
    const auto now = std::chrono::system_clock::now();
    m_nextDatabaseUpdateCheckUtc = now + kAvDatabaseUpdateCheckInterval;
    m_nextFixedDriveScanUtc = now + kFixedDriveScanInterval;
}

// Освобождает состояние службы при завершении.
void ServiceApiState::Shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ClearAuthAndLicenseLocked();
    m_avDatabase.Clear();
    m_initialized = false;
    m_databaseUpdateRunning = false;
    m_fixedDriveScanRunning = false;
}

// Возвращает безопасную информацию о текущей аутентификации.
TrayRpcStatusCode ServiceApiState::GetAuthInfo(TrayRpcAuthInfo* authInfo)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    FillAuthInfoLocked(authInfo);
    return TRAY_RPC_OK;
}

// Выполняет login и сохраняет секреты только в оперативной памяти.
TrayRpcStatusCode ServiceApiState::Login(const std::wstring& username, const std::wstring& password, TrayRpcAuthInfo* authInfo)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || username.empty() || password.empty())
    {
        FillAuthInfoLocked(authInfo);
        return TRAY_RPC_INVALID_ARGUMENT;
    }

    ApiIntegration::AuthTokens tokens = {};
    ApiIntegration::AuthError authError = {};
    if (!m_apiClient.Login(username, password, tokens, authError))
    {
        ClearAuthAndLicenseLocked();
        FillAuthInfoLocked(authInfo);
        return MapAuthApiError(authError);
    }

    m_session.SetAuthTokens(tokens);

    ApiIntegration::AuthUserInfo userInfo = {};
    ApiIntegration::ApiError userError = {};
    if (!m_apiClient.GetCurrentUser(tokens.accessToken, userInfo, userError))
    {
        ClearAuthAndLicenseLocked();
        FillAuthInfoLocked(authInfo);
        return MapAuthApiError(userError);
    }

    m_authenticated = true;
    m_userInfo = userInfo;
    m_session.ClearLicense();
    m_lastProductId = 0;
    m_lastDeviceName.clear();
    m_lastDeviceMac.clear();
    m_lastActivationKey.clear();
    m_licenseTasksRunning = false;

    FillAuthInfoLocked(authInfo);
    return TRAY_RPC_OK;
}

// Выполняет logout и очищает токены, ticket и signature из памяти.
TrayRpcStatusCode ServiceApiState::Logout()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ClearAuthAndLicenseLocked();
    return TRAY_RPC_OK;
}

// Проверяет лицензию через API и возвращает безопасное состояние.
TrayRpcStatusCode ServiceApiState::GetLicenseState(
    long long productId,
    const std::wstring& deviceMac,
    TrayRpcLicenseInfo* licenseInfo)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized)
    {
        FillLicenseInfoLocked(licenseInfo, TRAY_RPC_SERVER_ERROR);
        return TRAY_RPC_SERVER_ERROR;
    }

    if (!m_authenticated)
    {
        m_session.ClearLicense();
        StopLicenseDependentTasksLocked();
        FillLicenseInfoLocked(licenseInfo, TRAY_RPC_NOT_AUTHENTICATED);
        return TRAY_RPC_NOT_AUTHENTICATED;
    }

    long long effectiveProductId = productId > 0 ? productId : m_lastProductId;
    std::wstring effectiveDeviceMac = !deviceMac.empty() ? deviceMac : m_lastDeviceMac;
    if (effectiveProductId <= 0 || effectiveDeviceMac.empty())
    {
        m_session.ClearLicense();
        StopLicenseDependentTasksLocked();
        FillLicenseInfoLocked(licenseInfo, TRAY_RPC_NO_LICENSE);
        return TRAY_RPC_NO_LICENSE;
    }

    m_lastProductId = effectiveProductId;
    m_lastDeviceMac = effectiveDeviceMac;
    return FetchLicenseStateFromApiLocked(effectiveProductId, effectiveDeviceMac, licenseInfo);
}

// Активирует продукт через API и возвращает безопасное состояние лицензии.
TrayRpcStatusCode ServiceApiState::ActivateProduct(
    const std::wstring& activationKey,
    long long productId,
    const std::wstring& deviceName,
    const std::wstring& deviceMac,
    TrayRpcLicenseInfo* licenseInfo)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || activationKey.empty() || deviceName.empty() || deviceMac.empty() || productId <= 0)
    {
        FillLicenseInfoLocked(licenseInfo, TRAY_RPC_INVALID_ARGUMENT);
        return TRAY_RPC_INVALID_ARGUMENT;
    }

    if (!m_authenticated)
    {
        FillLicenseInfoLocked(licenseInfo, TRAY_RPC_NOT_AUTHENTICATED);
        return TRAY_RPC_NOT_AUTHENTICATED;
    }

    ApiIntegration::AuthTokens tokens = {};
    if (!m_session.TryGetAuthTokens(tokens))
    {
        ClearAuthAndLicenseLocked();
        FillLicenseInfoLocked(licenseInfo, TRAY_RPC_NOT_AUTHENTICATED);
        return TRAY_RPC_NOT_AUTHENTICATED;
    }

    ApiIntegration::LicenseTicket ticket = {};
    std::wstring signature;
    ApiIntegration::LicenseError error = {};
    bool isActivationSuccessful = m_apiClient.ActivateLicense(
        tokens.accessToken,
        activationKey,
        deviceName,
        deviceMac,
        ticket,
        signature,
        error);

    if (!isActivationSuccessful && error.code == ApiIntegration::ApiErrorCode::Unauthorized)
    {
        if (TryRefreshTokensLocked() && m_session.TryGetAuthTokens(tokens))
        {
            isActivationSuccessful = m_apiClient.ActivateLicense(
                tokens.accessToken,
                activationKey,
                deviceName,
                deviceMac,
                ticket,
                signature,
                error);
        }
    }

    if (isActivationSuccessful)
    {
        m_session.SetLicenseTicket(ticket, signature);
        m_lastProductId = productId;
        m_lastDeviceName = deviceName;
        m_lastDeviceMac = deviceMac;
        m_lastActivationKey = activationKey;

        const TrayRpcStatusCode licenseStatus = EnsureLicenseForAntivirusOperationLocked();
        StartLicenseDependentTasksIfAllowedLocked();
        LoadAntivirusDatabaseLocked();
        FillLicenseInfoLocked(licenseInfo, licenseStatus);
        return licenseStatus;
    }

    m_lastProductId = productId;
    m_lastDeviceName = deviceName;
    m_lastDeviceMac = deviceMac;
    m_lastActivationKey = activationKey;

    // Reconciles state when activation side effects may be committed but response processing failed.
    if (error.code == ApiIntegration::ApiErrorCode::Parse
        || error.code == ApiIntegration::ApiErrorCode::HttpStatus
        || error.code == ApiIntegration::ApiErrorCode::Unknown)
    {
        const TrayRpcStatusCode reconciledCode = FetchLicenseStateFromApiLocked(productId, deviceMac, licenseInfo);
        if (reconciledCode != TRAY_RPC_SERVER_ERROR && reconciledCode != TRAY_RPC_NETWORK_ERROR)
        {
            return reconciledCode;
        }
    }

    if (error.code == ApiIntegration::ApiErrorCode::Parse)
    {
        return FetchLicenseStateFromApiLocked(productId, deviceMac, licenseInfo);
    }

    if (MapLicenseApiError(error) == TRAY_RPC_NO_LICENSE)
    {
        m_session.ClearLicense();
        StopLicenseDependentTasksLocked();
    }

    const TrayRpcStatusCode code = MapLicenseApiError(error);
    FillLicenseInfoLocked(licenseInfo, code);
    return code;
}

// Выполняет периодическое обновление токенов и лицензионного тикета.
void ServiceApiState::Tick()
{
    bool shouldCheckDatabaseUpdate = false;
    bool shouldRunFixedDriveScan = false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_initialized)
        {
            return;
        }

        const auto now = std::chrono::system_clock::now();

        if (m_authenticated)
        {
            const auto nextAuthRefresh = m_session.GetNextAuthRefreshTimeUtc();
            if (nextAuthRefresh.has_value() && nextAuthRefresh.value() <= now)
            {
                if (!TryRefreshTokensLocked())
                {
                    ClearAuthAndLicenseLocked();
                    return;
                }
            }
        }

        if (!m_authenticated)
        {
            StopLicenseDependentTasksLocked();
        }
        else
        {
            ApiIntegration::LicenseState licenseState = m_session.GetLicenseState();
            if (!licenseState.hasLicense)
            {
                StopLicenseDependentTasksLocked();
            }
            else
            {
                if (licenseState.nextRefreshAtUtc.has_value() && licenseState.nextRefreshAtUtc.value() <= now)
                {
                    if (!TryRefreshLicenseTicketLocked())
                    {
                        m_session.ClearLicense();
                        StopLicenseDependentTasksLocked();
                        return;
                    }

                    licenseState = m_session.GetLicenseState();
                }

                if (EnsureLicenseForAntivirusOperationLocked() == TRAY_RPC_OK)
                {
                    StartLicenseDependentTasksIfAllowedLocked();
                }
            }
        }

        if (m_nextDatabaseUpdateCheckUtc == std::chrono::system_clock::time_point{}
            || m_nextDatabaseUpdateCheckUtc <= now)
        {
            shouldCheckDatabaseUpdate = !m_databaseUpdateRunning;
            m_databaseUpdateRunning = shouldCheckDatabaseUpdate;
            m_nextDatabaseUpdateCheckUtc = now + kAvDatabaseUpdateCheckInterval;
        }

        if (kEnableScheduledFixedDriveScan
            && (m_nextFixedDriveScanUtc == std::chrono::system_clock::time_point{}
                || m_nextFixedDriveScanUtc <= now))
        {
            shouldRunFixedDriveScan = !m_fixedDriveScanRunning;
            m_fixedDriveScanRunning = shouldRunFixedDriveScan;
            m_nextFixedDriveScanUtc = now + kFixedDriveScanInterval;
        }
    }

    if (shouldCheckDatabaseUpdate)
    {
        TryInstallPendingAntivirusDatabase();
        std::lock_guard<std::mutex> lock(m_mutex);
        m_databaseUpdateRunning = false;
    }

    if (shouldRunFixedDriveScan)
    {
        RunScheduledFixedDriveScan();
        std::lock_guard<std::mutex> lock(m_mutex);
        m_fixedDriveScanRunning = false;
    }
}

// Returns current in-memory antivirus database metadata.
Antivirus::AvDatabaseInfo ServiceApiState::GetAntivirusDatabaseInfo()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_avDatabase.GetInfo();
}

// Scans one selected file through the antivirus engine.
TrayRpcStatusCode ServiceApiState::ScanFile(const std::wstring& path, TrayRpcAvFileScanResult* scanResult)
{
    if (scanResult == nullptr || path.empty())
    {
        return TRAY_RPC_INVALID_ARGUMENT;
    }

    Antivirus::InMemoryAvDatabase databaseSnapshot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_initialized)
        {
            Antivirus::AvFileScanResult errorResult = {};
            errorResult.Path = path;
            errorResult.Status = Antivirus::AvScanStatus::Error;
            errorResult.Message = L"TrayService is not initialized.";
            FillRpcFileScanResult(errorResult, scanResult);
            return TRAY_RPC_SERVER_ERROR;
        }

        databaseSnapshot = m_avDatabase;

        const TrayRpcStatusCode licenseStatus = EnsureLicenseForAntivirusOperationLocked();
        if (licenseStatus != TRAY_RPC_OK)
        {
            Antivirus::AvFileScanResult errorResult = {};
            errorResult.Path = path;
            errorResult.Status = Antivirus::AvScanStatus::Error;
            errorResult.Message = AntivirusAccessStatusToMessage(licenseStatus);
            FillRpcFileScanResult(errorResult, scanResult);
            return licenseStatus;
        }
    }

    Antivirus::AntivirusScanner scanner(databaseSnapshot);
    const Antivirus::AvFileScanResult result = scanner.ScanFile(std::filesystem::path(path));
    FillRpcFileScanResult(result, scanResult);
    return TRAY_RPC_OK;
}

// Recursively scans one selected directory through the antivirus engine.
TrayRpcStatusCode ServiceApiState::ScanDirectory(
    const std::wstring& path,
    TrayRpcAvDirectoryScanResult* scanResult)
{
    if (scanResult == nullptr || path.empty())
    {
        return TRAY_RPC_INVALID_ARGUMENT;
    }

    Antivirus::InMemoryAvDatabase databaseSnapshot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_initialized)
        {
            Antivirus::AvDirectoryScanResult errorResult = {};
            errorResult.Path = path;
            errorResult.Message = L"TrayService is not initialized.";
            ++errorResult.ErrorCount;
            FillRpcDirectoryScanResult(errorResult, scanResult);
            return TRAY_RPC_SERVER_ERROR;
        }

        databaseSnapshot = m_avDatabase;

        const TrayRpcStatusCode licenseStatus = EnsureLicenseForAntivirusOperationLocked();
        if (licenseStatus != TRAY_RPC_OK)
        {
            Antivirus::AvDirectoryScanResult errorResult = {};
            errorResult.Path = path;
            errorResult.Message = AntivirusAccessStatusToMessage(licenseStatus);
            ++errorResult.ErrorCount;
            FillRpcDirectoryScanResult(errorResult, scanResult);
            return licenseStatus;
        }
    }

    Antivirus::AntivirusScanner scanner(databaseSnapshot);
    const Antivirus::AvDirectoryScanResult result = scanner.ScanDirectory(std::filesystem::path(path));
    FillRpcDirectoryScanResult(result, scanResult);
    return TRAY_RPC_OK;
}

// Returns antivirus database metadata for RPC callers.
TrayRpcStatusCode ServiceApiState::GetAvDatabaseInfo(TrayRpcAvDatabaseInfo* databaseInfo)
{
    if (databaseInfo == nullptr)
    {
        return TRAY_RPC_INVALID_ARGUMENT;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    FillRpcAvDatabaseInfo(m_avDatabase.GetInfo(), databaseInfo);
    return TRAY_RPC_OK;
}

// Loads demo antivirus records into the in-memory database.
void ServiceApiState::LoadAntivirusDatabaseLocked()
{
    const Antivirus::DemoHmacSha256SignatureVerifier demoVerifier;
    const Antivirus::AvDatabaseStoragePaths paths = Antivirus::AvDatabaseStore::GetDefaultStoragePaths();
    const Antivirus::AvDatabaseLoadResult loadResult =
        Antivirus::AvDatabaseStore::LoadWithFallback(paths, m_avDatabase, demoVerifier, demoVerifier);
    if (!loadResult.Loaded)
    {
        m_avDatabase.LoadDemoRecords();
        m_avDatabase.SetLoadMetadata(
            Antivirus::AvDatabaseFileSource::Default,
            std::chrono::system_clock::now(),
            L"Fallback in-memory demo database loaded.");
        return;
    }

    SetAntivirusDatabaseStatusLocked(loadResult.Message);
}

bool ServiceApiState::TryInstallPendingAntivirusDatabase()
{
    const Antivirus::DemoHmacSha256SignatureVerifier demoVerifier;
    const Antivirus::AvDatabaseStoragePaths paths = Antivirus::AvDatabaseStore::GetDefaultStoragePaths();

    std::error_code error;
    if (!std::filesystem::exists(paths.IncomingDatabasePath, error))
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        SetAntivirusDatabaseStatusLocked(L"No pending database update. Network binary API is not imported by this client yet.");
        return false;
    }

    Antivirus::InMemoryAvDatabase updatedDatabase;
    const Antivirus::AvDatabaseLoadResult updateResult =
        Antivirus::AvDatabaseStore::InstallUpdateFromFile(
            paths,
            paths.IncomingDatabasePath,
            updatedDatabase,
            demoVerifier,
            demoVerifier);

    std::lock_guard<std::mutex> lock(m_mutex);
    if (updateResult.Loaded)
    {
        m_avDatabase = updatedDatabase;
        SetAntivirusDatabaseStatusLocked(updateResult.Message);
        return updateResult.Source == Antivirus::AvDatabaseFileSource::Updated;
    }

    SetAntivirusDatabaseStatusLocked(updateResult.Message);
    return false;
}

void ServiceApiState::RunScheduledFixedDriveScan()
{
    Antivirus::InMemoryAvDatabase databaseSnapshot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        databaseSnapshot = m_avDatabase;
    }

    Antivirus::AntivirusScanner scanner(databaseSnapshot);
    const Antivirus::AvDirectoryScanResult scanResult = scanner.ScanFixedDrives();
    std::wstring text = L"Scheduled fixed-drive scan completed. scanned=";
    text += std::to_wstring(scanResult.TotalScanned);
    text += L" infected=";
    text += std::to_wstring(scanResult.InfectedCount);
    text += L" errors=";
    text += std::to_wstring(scanResult.ErrorCount);
    OutputDebugStringW((text + L"\r\n").c_str());
}

void ServiceApiState::SetAntivirusDatabaseStatusLocked(const std::wstring& status)
{
    const Antivirus::AvDatabaseInfo info = m_avDatabase.GetInfo();
    m_avDatabase.SetLoadMetadata(
        info.Source,
        info.LastSuccessfulLoadUtc != std::chrono::system_clock::time_point{}
            ? info.LastSuccessfulLoadUtc
            : std::chrono::system_clock::now(),
        status);
}

// Возвращает безопасное состояние текущей лицензии без обращения к клиенту.
TrayRpcStatusCode ServiceApiState::EnsureLicenseForAntivirusOperationLocked()
{
    const ApiIntegration::LicenseState state = m_session.GetLicenseState();
    if (!state.hasLicense)
    {
        StopLicenseDependentTasksLocked();
        return TRAY_RPC_NO_LICENSE;
    }

    if (state.expired)
    {
        StopLicenseDependentTasksLocked();
        return TRAY_RPC_LICENSE_EXPIRED;
    }

    if (state.blocked)
    {
        StopLicenseDependentTasksLocked();
        return TRAY_RPC_LICENSE_BLOCKED;
    }

    return TRAY_RPC_OK;
}

// Останавливает фоновые задачи, зависящие от лицензии.
void ServiceApiState::StopLicenseDependentTasksLocked()
{
    m_licenseTasksRunning = false;
}

// Запускает фоновые задачи только при валидной лицензии.
void ServiceApiState::StartLicenseDependentTasksIfAllowedLocked()
{
    if (EnsureLicenseForAntivirusOperationLocked() == TRAY_RPC_OK)
    {
        m_licenseTasksRunning = true;
    }
}

// Очищает аутентификацию и лицензию после критической ошибки refresh.
void ServiceApiState::ClearAuthAndLicenseLocked()
{
    m_authenticated = false;
    m_userInfo = {};
    m_session.ClearAll();
    m_lastProductId = 0;
    m_lastDeviceName.clear();
    m_lastDeviceMac.clear();
    m_lastActivationKey.clear();
    StopLicenseDependentTasksLocked();
}

// Пытается обновить токены по refresh token.
bool ServiceApiState::TryRefreshTokensLocked()
{
    ApiIntegration::AuthTokens currentTokens = {};
    if (!m_session.TryGetAuthTokens(currentTokens))
    {
        return false;
    }

    ApiIntegration::AuthTokens refreshedTokens = {};
    ApiIntegration::AuthError refreshError = {};
    if (!m_apiClient.RefreshTokens(currentTokens.refreshToken, refreshedTokens, refreshError))
    {
        return false;
    }

    m_session.SetAuthTokens(refreshedTokens);
    return true;
}

// Пытается обновить тикет лицензии по сохраненному контексту.
bool ServiceApiState::TryRefreshLicenseTicketLocked()
{
    if (m_lastProductId <= 0 || m_lastDeviceMac.empty())
    {
        return false;
    }

    ApiIntegration::AuthTokens tokens = {};
    if (!m_session.TryGetAuthTokens(tokens))
    {
        return false;
    }

    ApiIntegration::LicenseTicket ticket = {};
    std::wstring signature;
    ApiIntegration::LicenseError error = {};

    if (m_apiClient.CheckLicense(tokens.accessToken, m_lastProductId, m_lastDeviceMac, ticket, signature, error))
    {
        m_session.SetLicenseTicket(ticket, signature);
        return true;
    }

    if (error.code == ApiIntegration::ApiErrorCode::Unauthorized)
    {
        if (!TryRefreshTokensLocked() || !m_session.TryGetAuthTokens(tokens))
        {
            return false;
        }

        if (m_apiClient.CheckLicense(tokens.accessToken, m_lastProductId, m_lastDeviceMac, ticket, signature, error))
        {
            m_session.SetLicenseTicket(ticket, signature);
            return true;
        }
    }

    return false;
}

// Выполняет check endpoint и сохраняет ticket/signature в памяти.
TrayRpcStatusCode ServiceApiState::FetchLicenseStateFromApiLocked(
    long long productId,
    const std::wstring& deviceMac,
    TrayRpcLicenseInfo* licenseInfo)
{
    ApiIntegration::AuthTokens tokens = {};
    if (!m_session.TryGetAuthTokens(tokens))
    {
        ClearAuthAndLicenseLocked();
        FillLicenseInfoLocked(licenseInfo, TRAY_RPC_NOT_AUTHENTICATED);
        return TRAY_RPC_NOT_AUTHENTICATED;
    }

    ApiIntegration::LicenseTicket ticket = {};
    std::wstring signature;
    ApiIntegration::LicenseError error = {};
    bool isSuccess = m_apiClient.CheckLicense(tokens.accessToken, productId, deviceMac, ticket, signature, error);
    if (!isSuccess && error.code == ApiIntegration::ApiErrorCode::Unauthorized)
    {
        if (TryRefreshTokensLocked() && m_session.TryGetAuthTokens(tokens))
        {
            isSuccess = m_apiClient.CheckLicense(tokens.accessToken, productId, deviceMac, ticket, signature, error);
        }
    }

    if (!isSuccess)
    {
        const TrayRpcStatusCode code = MapLicenseApiError(error);
        m_session.ClearLicense();
        StopLicenseDependentTasksLocked();
        FillLicenseInfoLocked(licenseInfo, code);
        return code;
    }

    m_session.SetLicenseTicket(ticket, signature);
    const TrayRpcStatusCode code = EnsureLicenseForAntivirusOperationLocked();
    StartLicenseDependentTasksIfAllowedLocked();
    FillLicenseInfoLocked(licenseInfo, code);
    return code;
}

// Заполняет безопасный ответ по текущему состоянию пользователя.
void ServiceApiState::FillAuthInfoLocked(TrayRpcAuthInfo* authInfo) const
{
    if (authInfo == nullptr)
    {
        return;
    }

    authInfo->authenticated = m_authenticated ? 1 : 0;
    authInfo->hasUserId = m_authenticated && m_userInfo.id.has_value() ? 1 : 0;
    authInfo->userId = m_userInfo.id.value_or(0);
    CopyToRpcTextBuffer(m_authenticated ? m_userInfo.username : std::wstring(), authInfo->username, 128);
}

// Заполняет безопасный ответ по текущему состоянию лицензии.
void ServiceApiState::FillLicenseInfoLocked(TrayRpcLicenseInfo* licenseInfo, TrayRpcStatusCode errorCode) const
{
    if (licenseInfo == nullptr)
    {
        return;
    }

    const ApiIntegration::LicenseState state = m_session.GetLicenseState();
    licenseInfo->hasLicense = state.hasLicense ? 1 : 0;
    licenseInfo->blocked = state.blocked ? 1 : 0;
    licenseInfo->expired = state.expired ? 1 : 0;
    licenseInfo->hasExpirationEpochSeconds = state.expirationDateUtc.has_value() ? 1 : 0;
    licenseInfo->expirationEpochSeconds = 0;
    if (state.expirationDateUtc.has_value())
    {
        const auto epochSeconds = std::chrono::duration_cast<std::chrono::seconds>(
            state.expirationDateUtc.value().time_since_epoch()).count();
        licenseInfo->expirationEpochSeconds = epochSeconds;
    }

    licenseInfo->errorCode = errorCode;
}

// Преобразует ошибку API в код RPC для аутентификации.
TrayRpcStatusCode ServiceApiState::MapAuthApiError(const ApiIntegration::ApiError& error)
{
    switch (error.code)
    {
    case ApiIntegration::ApiErrorCode::Unauthorized:
    case ApiIntegration::ApiErrorCode::Forbidden:
        return TRAY_RPC_AUTH_FAILED;
    case ApiIntegration::ApiErrorCode::Network:
        return TRAY_RPC_NETWORK_ERROR;
    case ApiIntegration::ApiErrorCode::InvalidInput:
        return TRAY_RPC_INVALID_ARGUMENT;
    default:
        return TRAY_RPC_SERVER_ERROR;
    }
}

// Преобразует ошибку API в код RPC для лицензии.
TrayRpcStatusCode ServiceApiState::MapLicenseApiError(const ApiIntegration::ApiError& error)
{
    switch (error.code)
    {
    case ApiIntegration::ApiErrorCode::NotFound:
        return TRAY_RPC_NO_LICENSE;
    case ApiIntegration::ApiErrorCode::Unauthorized:
    case ApiIntegration::ApiErrorCode::Forbidden:
        return TRAY_RPC_NOT_AUTHENTICATED;
    case ApiIntegration::ApiErrorCode::Network:
        return TRAY_RPC_NETWORK_ERROR;
    case ApiIntegration::ApiErrorCode::InvalidInput:
        return TRAY_RPC_INVALID_ARGUMENT;
    default:
        return TRAY_RPC_SERVER_ERROR;
    }
}

// Копирует строку в фиксированный RPC-буфер.
void ServiceApiState::CopyToRpcTextBuffer(const std::wstring& source, wchar_t* destination, size_t destinationLength)
{
    if (destination == nullptr || destinationLength == 0)
    {
        return;
    }

    destination[0] = L'\0';
    if (source.empty())
    {
        return;
    }

    wcsncpy_s(destination, destinationLength, source.c_str(), _TRUNCATE);
}
