#include "ServiceApiState.h"

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <optional>
#include <utility>

namespace
{
    constexpr wchar_t kApiBaseUrl[] = L"https://192.168.1.56:8443/";
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
}

// Освобождает состояние службы при завершении.
void ServiceApiState::Shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ClearAuthAndLicenseLocked();
    m_initialized = false;
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
        return;
    }

    ApiIntegration::LicenseState licenseState = m_session.GetLicenseState();
    if (!licenseState.hasLicense)
    {
        StopLicenseDependentTasksLocked();
        return;
    }

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
