#include "RpcClient.h"

#include <rpc.h>
#include <rpcdce.h>
#include <cstdlib>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include "RpcContract.h"

#pragma comment(lib, "Rpcrt4.lib")

namespace
{
    constexpr wchar_t kRpcProtocolSequence[] = L"ncalrpc";
    constexpr wchar_t kRpcEndpoint[] = L"TrayServiceControlEndpoint";

    std::wstring FormatRpcError(DWORD errorCode)
    {
        wchar_t* rawMessage = nullptr;
        const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER
            | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS;
        const DWORD result = FormatMessageW(
            flags,
            nullptr,
            errorCode,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPWSTR>(&rawMessage),
            0,
            nullptr);
        if (result == 0 || rawMessage == nullptr)
        {
            return L"(no text)";
        }

        std::wstring message = rawMessage;
        LocalFree(rawMessage);
        return message;
    }

    void LogRpcClientInfo(const wchar_t* message)
    {
        std::wstring text = L"[TrayApp][RPC] ";
        text += message != nullptr ? message : L"(no message)";
        text += L"\r\n";
        OutputDebugStringW(text.c_str());
    }

    void LogRpcClientStatus(const wchar_t* stepName, RPC_STATUS status)
    {
        std::wstring text = L"[TrayApp][RPC] ";
        text += stepName != nullptr ? stepName : L"(unknown step)";
        text += status == RPC_S_OK ? L" succeeded." : L" failed.";
        text += L" status=";
        text += std::to_wstring(status);
        text += L" message=";
        text += FormatRpcError(status);
        text += L"\r\n";
        OutputDebugStringW(text.c_str());
    }

    class RpcBinding
    {
    public:
        ~RpcBinding()
        {
            if (m_bindingHandle != nullptr)
            {
                RpcBindingFree(&m_bindingHandle);
                m_bindingHandle = nullptr;
            }

            if (m_stringBinding != nullptr)
            {
                RpcStringFreeW(&m_stringBinding);
                m_stringBinding = nullptr;
            }
        }

        bool Create()
        {
            LogRpcClientInfo(L"Creating RPC binding to TrayServiceControlEndpoint.");

            RPC_STATUS status = RpcStringBindingComposeW(
                nullptr,
                reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(kRpcProtocolSequence)),
                nullptr,
                reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(kRpcEndpoint)),
                nullptr,
                &m_stringBinding);
            LogRpcClientStatus(L"RpcStringBindingComposeW", status);
            if (status != RPC_S_OK)
            {
                return false;
            }

            status = RpcBindingFromStringBindingW(m_stringBinding, &m_bindingHandle);
            LogRpcClientStatus(L"RpcBindingFromStringBindingW", status);
            return status == RPC_S_OK;
        }

        RPC_BINDING_HANDLE Get() const
        {
            return m_bindingHandle;
        }

    private:
        RPC_WSTR m_stringBinding = nullptr;
        RPC_BINDING_HANDLE m_bindingHandle = nullptr;
    };

    bool RpcCallStopService(
        RPC_BINDING_HANDLE bindingHandle,
        TrayRpcStopResult* stopResult,
        RPC_STATUS* exceptionCode)
    {
        if (stopResult == nullptr || exceptionCode == nullptr)
        {
            return false;
        }

        *exceptionCode = RPC_S_OK;
        bool isSuccess = true;
        RpcTryExcept
        {
            *stopResult = ::StopService(bindingHandle);
        }
        RpcExcept(1)
        {
            *exceptionCode = RpcExceptionCode();
            isSuccess = false;
        }
        RpcEndExcept;

        return isSuccess;
    }

    bool RpcCallConfirmStopService(
        RPC_BINDING_HANDLE bindingHandle,
        TrayRpcStopResult* stopResult,
        RPC_STATUS* exceptionCode)
    {
        if (stopResult == nullptr || exceptionCode == nullptr)
        {
            return false;
        }

        *exceptionCode = RPC_S_OK;
        bool isSuccess = true;
        RpcTryExcept
        {
            *stopResult = ::ConfirmStopService(bindingHandle);
        }
        RpcExcept(1)
        {
            *exceptionCode = RpcExceptionCode();
            isSuccess = false;
        }
        RpcEndExcept;

        return isSuccess;
    }

    RpcClient::StopRequestResult FromRpcStopResult(TrayRpcStopResult stopResult)
    {
        switch (stopResult)
        {
        case TRAY_RPC_STOP_APPROVED:
            return RpcClient::StopRequestResult::Approved;
        case TRAY_RPC_STOP_REJECTED:
            return RpcClient::StopRequestResult::Rejected;
        case TRAY_RPC_STOP_CONFIRMATION_REQUIRED:
            return RpcClient::StopRequestResult::ConfirmationRequired;
        case TRAY_RPC_STOP_FAILED:
        default:
            return RpcClient::StopRequestResult::Failed;
        }
    }

    bool RpcCallGetAuthInfo(
        RPC_BINDING_HANDLE bindingHandle,
        TrayRpcAuthInfo* authInfo,
        TrayRpcStatusCode* statusCode)
    {
        bool isSuccess = true;
        RpcTryExcept
        {
            *statusCode = ::GetAuthInfo(bindingHandle, authInfo);
        }
        RpcExcept(1)
        {
            isSuccess = false;
        }
        RpcEndExcept;

        return isSuccess;
    }

    bool RpcCallLogin(
        RPC_BINDING_HANDLE bindingHandle,
        wchar_t* username,
        wchar_t* password,
        TrayRpcAuthInfo* authInfo,
        TrayRpcStatusCode* statusCode)
    {
        bool isSuccess = true;
        RpcTryExcept
        {
            *statusCode = ::Login(bindingHandle, username, password, authInfo);
        }
        RpcExcept(1)
        {
            isSuccess = false;
        }
        RpcEndExcept;

        return isSuccess;
    }

    bool RpcCallLogout(RPC_BINDING_HANDLE bindingHandle, TrayRpcStatusCode* statusCode)
    {
        bool isSuccess = true;
        RpcTryExcept
        {
            *statusCode = ::Logout(bindingHandle);
        }
        RpcExcept(1)
        {
            isSuccess = false;
        }
        RpcEndExcept;

        return isSuccess;
    }

    bool RpcCallGetLicenseState(
        RPC_BINDING_HANDLE bindingHandle,
        hyper productId,
        wchar_t* deviceMac,
        TrayRpcLicenseInfo* licenseInfo,
        TrayRpcStatusCode* statusCode)
    {
        bool isSuccess = true;
        RpcTryExcept
        {
            *statusCode = ::GetLicenseState(bindingHandle, productId, deviceMac, licenseInfo);
        }
        RpcExcept(1)
        {
            isSuccess = false;
        }
        RpcEndExcept;

        return isSuccess;
    }

    bool RpcCallActivateProduct(
        RPC_BINDING_HANDLE bindingHandle,
        wchar_t* activationKey,
        hyper productId,
        wchar_t* deviceName,
        wchar_t* deviceMac,
        TrayRpcLicenseInfo* licenseInfo,
        TrayRpcStatusCode* statusCode)
    {
        bool isSuccess = true;
        RpcTryExcept
        {
            *statusCode = ::ActivateProduct(
                bindingHandle,
                activationKey,
                productId,
                deviceName,
                deviceMac,
                licenseInfo);
        }
        RpcExcept(1)
        {
            isSuccess = false;
        }
        RpcEndExcept;

        return isSuccess;
    }

    bool RpcCallScanFile(
        RPC_BINDING_HANDLE bindingHandle,
        wchar_t* path,
        TrayRpcAvFileScanResult* scanResult,
        TrayRpcStatusCode* statusCode)
    {
        bool isSuccess = true;
        RpcTryExcept
        {
            *statusCode = ::ScanFile(bindingHandle, path, scanResult);
        }
        RpcExcept(1)
        {
            isSuccess = false;
        }
        RpcEndExcept;

        return isSuccess;
    }

    bool RpcCallScanDirectory(
        RPC_BINDING_HANDLE bindingHandle,
        wchar_t* path,
        TrayRpcAvDirectoryScanResult* scanResult,
        TrayRpcStatusCode* statusCode)
    {
        bool isSuccess = true;
        RpcTryExcept
        {
            *statusCode = ::ScanDirectory(bindingHandle, path, scanResult);
        }
        RpcExcept(1)
        {
            isSuccess = false;
        }
        RpcEndExcept;

        return isSuccess;
    }

    bool RpcCallGetAvDatabaseInfo(
        RPC_BINDING_HANDLE bindingHandle,
        TrayRpcAvDatabaseInfo* databaseInfo,
        TrayRpcStatusCode* statusCode)
    {
        bool isSuccess = true;
        RpcTryExcept
        {
            *statusCode = ::GetAvDatabaseInfo(bindingHandle, databaseInfo);
        }
        RpcExcept(1)
        {
            isSuccess = false;
        }
        RpcEndExcept;

        return isSuccess;
    }

    RpcClient::RpcStatusCode FromRpcStatus(TrayRpcStatusCode status)
    {
        switch (status)
        {
        case TRAY_RPC_OK:
            return RpcClient::RpcStatusCode::Ok;
        case TRAY_RPC_INVALID_ARGUMENT:
            return RpcClient::RpcStatusCode::InvalidArgument;
        case TRAY_RPC_NOT_AUTHENTICATED:
            return RpcClient::RpcStatusCode::NotAuthenticated;
        case TRAY_RPC_AUTH_FAILED:
            return RpcClient::RpcStatusCode::AuthFailed;
        case TRAY_RPC_NETWORK_ERROR:
            return RpcClient::RpcStatusCode::NetworkError;
        case TRAY_RPC_SERVER_ERROR:
            return RpcClient::RpcStatusCode::ServerError;
        case TRAY_RPC_NO_LICENSE:
            return RpcClient::RpcStatusCode::NoLicense;
        case TRAY_RPC_LICENSE_EXPIRED:
            return RpcClient::RpcStatusCode::LicenseExpired;
        case TRAY_RPC_LICENSE_BLOCKED:
            return RpcClient::RpcStatusCode::LicenseBlocked;
        default:
            return RpcClient::RpcStatusCode::ServerError;
        }
    }

    void FillAuthInfo(const TrayRpcAuthInfo& rpcAuthInfo, RpcClient::AuthInfo& authInfo)
    {
        authInfo = {};
        authInfo.authenticated = rpcAuthInfo.authenticated != 0;
        if (rpcAuthInfo.hasUserId != 0)
        {
            authInfo.userId = static_cast<long long>(rpcAuthInfo.userId);
        }

        authInfo.username = rpcAuthInfo.username;
    }

    void FillLicenseInfo(const TrayRpcLicenseInfo& rpcLicenseInfo, RpcClient::LicenseInfo& licenseInfo)
    {
        licenseInfo = {};
        licenseInfo.hasLicense = rpcLicenseInfo.hasLicense != 0;
        licenseInfo.blocked = rpcLicenseInfo.blocked != 0;
        licenseInfo.expired = rpcLicenseInfo.expired != 0;
        licenseInfo.errorCode = FromRpcStatus(rpcLicenseInfo.errorCode);

        if (rpcLicenseInfo.hasExpirationEpochSeconds != 0)
        {
            const auto expiration =
                std::chrono::system_clock::from_time_t(static_cast<time_t>(rpcLicenseInfo.expirationEpochSeconds));
            licenseInfo.expirationDateUtc = expiration;
        }
    }

    RpcClient::AvScanVerdict FromRpcAvVerdict(TrayRpcAvScanVerdict verdict)
    {
        switch (verdict)
        {
        case TRAY_RPC_AV_SCAN_INFECTED:
            return RpcClient::AvScanVerdict::Infected;
        case TRAY_RPC_AV_SCAN_ERROR:
            return RpcClient::AvScanVerdict::Error;
        case TRAY_RPC_AV_SCAN_CLEAN:
        default:
            return RpcClient::AvScanVerdict::Clean;
        }
    }

    RpcClient::AvObjectType FromRpcAvObjectType(TrayRpcAvObjectType objectType)
    {
        switch (objectType)
        {
        case TRAY_RPC_AV_OBJECT_PE:
            return RpcClient::AvObjectType::Pe;
        case TRAY_RPC_AV_OBJECT_SCRIPT_TEXT:
            return RpcClient::AvObjectType::ScriptText;
        case TRAY_RPC_AV_OBJECT_UNKNOWN:
        default:
            return RpcClient::AvObjectType::Unknown;
        }
    }

    RpcClient::AvDatabaseLoadStatus FromRpcAvDatabaseLoadStatus(TrayRpcAvDatabaseLoadStatus status)
    {
        return status == TRAY_RPC_AV_DATABASE_LOADED
            ? RpcClient::AvDatabaseLoadStatus::Loaded
            : RpcClient::AvDatabaseLoadStatus::NotLoaded;
    }

    void FillAvFileScanResult(
        const TrayRpcAvFileScanResult& rpcResult,
        RpcClient::AvFileScanResult& scanResult)
    {
        scanResult = {};
        scanResult.verdict = FromRpcAvVerdict(rpcResult.verdict);
        scanResult.path = rpcResult.path;
        scanResult.objectType = FromRpcAvObjectType(rpcResult.objectType);
        scanResult.detectionOffset = static_cast<unsigned long long>(rpcResult.detectionOffset);
        scanResult.recordId = rpcResult.recordId;
        scanResult.objectSignatureHex = rpcResult.objectSignatureHex;
        scanResult.message = rpcResult.message;
    }

    void FillAvDirectoryScanResult(
        const TrayRpcAvDirectoryScanResult& rpcResult,
        RpcClient::AvDirectoryScanResult& scanResult)
    {
        scanResult = {};
        scanResult.path = rpcResult.path;
        scanResult.totalScanned = static_cast<unsigned long long>(rpcResult.totalScanned);
        scanResult.infectedCount = static_cast<unsigned long long>(rpcResult.infectedCount);
        scanResult.errorCount = static_cast<unsigned long long>(rpcResult.errorCount);
        scanResult.truncated = rpcResult.truncated != 0;
        scanResult.message = rpcResult.message;

        const int resultCount = (std::max)(0, rpcResult.resultCount);
        scanResult.results.reserve(static_cast<size_t>(resultCount));
        for (int index = 0; index < resultCount; ++index)
        {
            RpcClient::AvFileScanResult fileResult = {};
            FillAvFileScanResult(rpcResult.results[index], fileResult);
            scanResult.results.push_back(std::move(fileResult));
        }
    }

    void FillAvDatabaseInfo(
        const TrayRpcAvDatabaseInfo& rpcInfo,
        RpcClient::AvDatabaseInfo& databaseInfo)
    {
        databaseInfo = {};
        databaseInfo.recordCount = static_cast<unsigned long long>(rpcInfo.recordCount);
        databaseInfo.loadStatus = FromRpcAvDatabaseLoadStatus(rpcInfo.loadStatus);
        if (rpcInfo.hasReleaseDate != 0)
        {
            databaseInfo.releaseDateUtc =
                std::chrono::system_clock::from_time_t(static_cast<time_t>(rpcInfo.releaseEpochSeconds));
        }
    }
}

RpcClient::StopRequestResult RpcClient::RequestServiceStop()
{
    RpcBinding binding;
    if (!binding.Create())
    {
        LogRpcClientInfo(L"StopService RPC failed before call because endpoint binding could not be created.");
        return StopRequestResult::TransportError;
    }

    TrayRpcStopResult stopResult = TRAY_RPC_STOP_FAILED;
    RPC_STATUS exceptionCode = RPC_S_OK;

    LogRpcClientInfo(L"Sending StopService RPC request.");
    if (!RpcCallStopService(binding.Get(), &stopResult, &exceptionCode))
    {
        LogRpcClientStatus(L"StopService RPC exception", exceptionCode);
        LogRpcClientInfo(L"StopService RPC transport failed.");
        return StopRequestResult::TransportError;
    }

    std::wstring serverResultText = L"StopService RPC call completed. serverResult=";
    serverResultText += std::to_wstring(static_cast<int>(stopResult));
    LogRpcClientInfo(serverResultText.c_str());

    const StopRequestResult clientResult = FromRpcStopResult(stopResult);
    std::wstring text = L"StopService RPC mapped client result=";
    text += std::to_wstring(static_cast<int>(clientResult));
    LogRpcClientInfo(text.c_str());
    return clientResult;
}

RpcClient::StopRequestResult RpcClient::ConfirmServiceStop()
{
    RpcBinding binding;
    if (!binding.Create())
    {
        LogRpcClientInfo(L"ConfirmStopService RPC failed before call because endpoint binding could not be created.");
        return StopRequestResult::TransportError;
    }

    TrayRpcStopResult stopResult = TRAY_RPC_STOP_FAILED;
    RPC_STATUS exceptionCode = RPC_S_OK;

    LogRpcClientInfo(L"Sending ConfirmStopService RPC request.");
    if (!RpcCallConfirmStopService(binding.Get(), &stopResult, &exceptionCode))
    {
        LogRpcClientStatus(L"ConfirmStopService RPC exception", exceptionCode);
        LogRpcClientInfo(L"ConfirmStopService RPC transport failed.");
        return StopRequestResult::TransportError;
    }

    std::wstring serverResultText = L"ConfirmStopService RPC call completed. serverResult=";
    serverResultText += std::to_wstring(static_cast<int>(stopResult));
    LogRpcClientInfo(serverResultText.c_str());

    const StopRequestResult clientResult = FromRpcStopResult(stopResult);
    std::wstring text = L"ConfirmStopService RPC mapped client result=";
    text += std::to_wstring(static_cast<int>(clientResult));
    LogRpcClientInfo(text.c_str());
    return clientResult;
}

RpcClient::RpcStatusCode RpcClient::GetCurrentAuthInfo(AuthInfo& authInfo)
{
    // Вызывает RPC-метод чтения текущей аутентификации.
    RpcBinding binding;
    if (!binding.Create())
    {
        authInfo = {};
        return RpcStatusCode::TransportError;
    }

    TrayRpcAuthInfo rpcAuthInfo = {};
    TrayRpcStatusCode rpcStatus = TRAY_RPC_SERVER_ERROR;
    if (!RpcCallGetAuthInfo(binding.Get(), &rpcAuthInfo, &rpcStatus))
    {
        authInfo = {};
        return RpcStatusCode::TransportError;
    }

    FillAuthInfo(rpcAuthInfo, authInfo);
    return FromRpcStatus(rpcStatus);
}

RpcClient::RpcStatusCode RpcClient::Login(const std::wstring& username, const std::wstring& password, AuthInfo& authInfo)
{
    // Вызывает RPC-метод login.
    RpcBinding binding;
    if (!binding.Create())
    {
        authInfo = {};
        return RpcStatusCode::TransportError;
    }

    TrayRpcAuthInfo rpcAuthInfo = {};
    TrayRpcStatusCode rpcStatus = TRAY_RPC_SERVER_ERROR;
    if (!RpcCallLogin(
            binding.Get(),
            const_cast<wchar_t*>(username.c_str()),
            const_cast<wchar_t*>(password.c_str()),
            &rpcAuthInfo,
            &rpcStatus))
    {
        authInfo = {};
        return RpcStatusCode::TransportError;
    }

    FillAuthInfo(rpcAuthInfo, authInfo);
    return FromRpcStatus(rpcStatus);
}

RpcClient::RpcStatusCode RpcClient::Logout()
{
    // Вызывает RPC-метод logout.
    RpcBinding binding;
    if (!binding.Create())
    {
        return RpcStatusCode::TransportError;
    }

    TrayRpcStatusCode rpcStatus = TRAY_RPC_SERVER_ERROR;
    if (!RpcCallLogout(binding.Get(), &rpcStatus))
    {
        return RpcStatusCode::TransportError;
    }

    return FromRpcStatus(rpcStatus);
}

RpcClient::RpcStatusCode RpcClient::GetLicenseState(
    long long productId,
    const std::wstring& deviceMac,
    LicenseInfo& licenseInfo)
{
    // Вызывает RPC-метод получения состояния лицензии.
    RpcBinding binding;
    if (!binding.Create())
    {
        licenseInfo = {};
        return RpcStatusCode::TransportError;
    }

    TrayRpcLicenseInfo rpcLicenseInfo = {};
    TrayRpcStatusCode rpcStatus = TRAY_RPC_SERVER_ERROR;
    if (!RpcCallGetLicenseState(
            binding.Get(),
            static_cast<hyper>(productId),
            const_cast<wchar_t*>(deviceMac.c_str()),
            &rpcLicenseInfo,
            &rpcStatus))
    {
        licenseInfo = {};
        return RpcStatusCode::TransportError;
    }

    FillLicenseInfo(rpcLicenseInfo, licenseInfo);
    return FromRpcStatus(rpcStatus);
}

RpcClient::RpcStatusCode RpcClient::ActivateProduct(
    const std::wstring& activationKey,
    long long productId,
    const std::wstring& deviceName,
    const std::wstring& deviceMac,
    LicenseInfo& licenseInfo)
{
    // Вызывает RPC-метод активации продукта.
    RpcBinding binding;
    if (!binding.Create())
    {
        licenseInfo = {};
        return RpcStatusCode::TransportError;
    }

    TrayRpcLicenseInfo rpcLicenseInfo = {};
    TrayRpcStatusCode rpcStatus = TRAY_RPC_SERVER_ERROR;
    if (!RpcCallActivateProduct(
            binding.Get(),
            const_cast<wchar_t*>(activationKey.c_str()),
            static_cast<hyper>(productId),
            const_cast<wchar_t*>(deviceName.c_str()),
            const_cast<wchar_t*>(deviceMac.c_str()),
            &rpcLicenseInfo,
            &rpcStatus))
    {
        licenseInfo = {};
        return RpcStatusCode::TransportError;
    }

    FillLicenseInfo(rpcLicenseInfo, licenseInfo);
    return FromRpcStatus(rpcStatus);
}

RpcClient::RpcStatusCode RpcClient::ScanFile(const std::wstring& path, AvFileScanResult& scanResult)
{
    // Calls RPC method for scanning one selected file.
    RpcBinding binding;
    if (!binding.Create())
    {
        scanResult = {};
        return RpcStatusCode::TransportError;
    }

    TrayRpcAvFileScanResult rpcResult = {};
    TrayRpcStatusCode rpcStatus = TRAY_RPC_SERVER_ERROR;
    if (!RpcCallScanFile(binding.Get(), const_cast<wchar_t*>(path.c_str()), &rpcResult, &rpcStatus))
    {
        scanResult = {};
        return RpcStatusCode::TransportError;
    }

    FillAvFileScanResult(rpcResult, scanResult);
    return FromRpcStatus(rpcStatus);
}

RpcClient::RpcStatusCode RpcClient::ScanDirectory(
    const std::wstring& path,
    AvDirectoryScanResult& scanResult)
{
    // Calls RPC method for recursively scanning one selected directory.
    RpcBinding binding;
    if (!binding.Create())
    {
        scanResult = {};
        return RpcStatusCode::TransportError;
    }

    TrayRpcAvDirectoryScanResult rpcResult = {};
    TrayRpcStatusCode rpcStatus = TRAY_RPC_SERVER_ERROR;
    if (!RpcCallScanDirectory(binding.Get(), const_cast<wchar_t*>(path.c_str()), &rpcResult, &rpcStatus))
    {
        scanResult = {};
        return RpcStatusCode::TransportError;
    }

    FillAvDirectoryScanResult(rpcResult, scanResult);
    return FromRpcStatus(rpcStatus);
}

RpcClient::RpcStatusCode RpcClient::GetAvDatabaseInfo(AvDatabaseInfo& databaseInfo)
{
    // Calls RPC method for reading in-memory antivirus database metadata.
    RpcBinding binding;
    if (!binding.Create())
    {
        databaseInfo = {};
        return RpcStatusCode::TransportError;
    }

    TrayRpcAvDatabaseInfo rpcInfo = {};
    TrayRpcStatusCode rpcStatus = TRAY_RPC_SERVER_ERROR;
    if (!RpcCallGetAvDatabaseInfo(binding.Get(), &rpcInfo, &rpcStatus))
    {
        databaseInfo = {};
        return RpcStatusCode::TransportError;
    }

    FillAvDatabaseInfo(rpcInfo, databaseInfo);
    return FromRpcStatus(rpcStatus);
}

extern "C" void* __RPC_USER midl_user_allocate(size_t size)
{
    return malloc(size);
}

extern "C" void __RPC_USER midl_user_free(void* pointer)
{
    free(pointer);
}

