#pragma once

#include "ApiModels.h"

#include <string>
#include <vector>

namespace ApiIntegration
{
    class ApiClient
    {
    public:
        // Инициализирует API-клиент по базовому HTTPS URL.
        explicit ApiClient(const std::wstring& baseUrl);

        // Проверяет, что базовый URL успешно распознан.
        bool IsConfigured() const;

        // Выполняет login и возвращает пару токенов.
        bool Login(
            const std::wstring& username,
            const std::wstring& password,
            AuthTokens& tokens,
            AuthError& error) const;

        // Выполняет refresh и возвращает новую пару токенов.
        bool RefreshTokens(
            const std::wstring& refreshToken,
            AuthTokens& tokens,
            AuthError& error) const;

        // Возвращает данные текущего пользователя.
        bool GetCurrentUser(
            const std::wstring& accessToken,
            AuthUserInfo& userInfo,
            ApiError& error) const;

        // Проверяет лицензию и возвращает ticket с подписью.
        bool CheckLicense(
            const std::wstring& accessToken,
            long long productId,
            const std::wstring& deviceMac,
            LicenseTicket& ticket,
            std::wstring& signature,
            LicenseError& error) const;

        // Активирует лицензию и возвращает ticket с подписью.
        bool ActivateLicense(
            const std::wstring& accessToken,
            const std::wstring& activationKey,
            const std::wstring& deviceName,
            const std::wstring& deviceMac,
            LicenseTicket& ticket,
            std::wstring& signature,
            LicenseError& error) const;

        // Продлевает лицензию и возвращает ticket с подписью.
        bool RenewLicense(
            const std::wstring& accessToken,
            const std::wstring& activationKey,
            const std::wstring& deviceMac,
            LicenseTicket& ticket,
            std::wstring& signature,
            LicenseError& error) const;

        struct BinaryResponse
        {
            unsigned long statusCode = 0;
            unsigned long transportError = 0;
            std::string contentType;
            std::vector<uint8_t> body;
        };

        // Downloads one binary endpoint using the supplied bearer token.
        bool DownloadBinary(
            const std::wstring& method,
            const std::wstring& endpointPath,
            const std::string& requestBodyUtf8,
            const std::wstring& acceptHeader,
            const std::wstring* bearerToken,
            BinaryResponse& response) const;

        // Performs a lightweight authenticated reachability check against the service.
        bool IsServiceReachable(const std::wstring* bearerToken) const;

    private:
        struct HttpResponse
        {
            unsigned long statusCode = 0;
            unsigned long transportError = 0;
            std::string contentType;
            std::string body;
        };

        // Выполняет один HTTP-запрос и возвращает статус и тело ответа.
        bool SendJsonRequest(
            const std::wstring& method,
            const std::wstring& endpointPath,
            const std::string& requestBodyUtf8,
            const std::wstring* bearerToken,
            HttpResponse& response) const;

        // Performs one HTTP request with a caller-selected Accept header.
        bool SendHttpRequest(
            const std::wstring& method,
            const std::wstring& endpointPath,
            const std::string& requestBodyUtf8,
            const std::wstring& acceptHeader,
            const std::wstring* bearerToken,
            HttpResponse& response) const;

        // Формирует итоговый путь запроса с учетом base URL.
        std::wstring BuildRequestPath(const std::wstring& endpointPath) const;

        // Разбирает базовый URL и заполняет параметры подключения.
        bool ParseBaseUrl(const std::wstring& baseUrl);

        std::wstring m_host;
        unsigned short m_port = 443;
        std::wstring m_basePath = L"/";
        bool m_useHttps = true;
        bool m_isConfigured = false;
    };
}
