#include "ApiClient.h"

#include <windows.h>
#include <winhttp.h>

#include <cctype>
#include <chrono>
#include <ctime>
#include <regex>
#include <sstream>
#include <string>

#pragma comment(lib, "Winhttp.lib")

namespace
{
    struct ScopedHandle
    {
        HINTERNET handle = nullptr;

        ~ScopedHandle()
        {
            if (handle != nullptr)
            {
                WinHttpCloseHandle(handle);
                handle = nullptr;
            }
        }

        ScopedHandle() = default;
        ScopedHandle(const ScopedHandle&) = delete;
        ScopedHandle& operator=(const ScopedHandle&) = delete;
    };

    std::string WideToUtf8(const std::wstring& value)
    {
        if (value.empty())
        {
            return {};
        }

        const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0)
        {
            return {};
        }

        std::string utf8(size, '\0');
        if (WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), utf8.data(), size, nullptr, nullptr) <= 0)
        {
            return {};
        }

        return utf8;
    }

    std::wstring Utf8ToWide(const std::string& value)
    {
        if (value.empty())
        {
            return {};
        }

        const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
        if (size <= 0)
        {
            return {};
        }

        std::wstring wide(size, L'\0');
        if (MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), wide.data(), size) <= 0)
        {
            return {};
        }

        return wide;
    }

    std::string EscapeJson(const std::wstring& value)
    {
        const std::string utf8 = WideToUtf8(value);
        std::string escaped;
        escaped.reserve(utf8.size() + 8);
        for (unsigned char ch : utf8)
        {
            switch (ch)
            {
            case '\"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(static_cast<char>(ch));
                break;
            }
        }

        return escaped;
    }

    size_t SkipJsonWhitespace(const std::string& json, size_t position)
    {
        while (position < json.size()
            && std::isspace(static_cast<unsigned char>(json[position])) != 0)
        {
            ++position;
        }

        return position;
    }

    bool FindJsonFieldValueStart(const std::string& json, const char* field, size_t& valueStart)
    {
        if (field == nullptr || field[0] == '\0')
        {
            return false;
        }

        const std::string fieldKey = std::string("\"") + field + "\"";
        size_t searchPosition = 0;
        while (searchPosition < json.size())
        {
            const size_t keyPosition = json.find(fieldKey, searchPosition);
            if (keyPosition == std::string::npos)
            {
                return false;
            }

            size_t position = SkipJsonWhitespace(json, keyPosition + fieldKey.size());
            if (position >= json.size() || json[position] != ':')
            {
                searchPosition = keyPosition + 1;
                continue;
            }

            position = SkipJsonWhitespace(json, position + 1);
            if (position >= json.size())
            {
                return false;
            }

            valueStart = position;
            return true;
        }

        return false;
    }

    bool ExtractString(const std::string& json, const char* field, std::string& value)
    {
        size_t valueStart = 0;
        if (!FindJsonFieldValueStart(json, field, valueStart) || json[valueStart] != '\"')
        {
            return false;
        }

        value.clear();
        bool escaped = false;
        for (size_t position = valueStart + 1; position < json.size(); ++position)
        {
            const char current = json[position];
            if (escaped)
            {
                switch (current)
                {
                case '\"':
                    value.push_back('\"');
                    break;
                case '\\':
                    value.push_back('\\');
                    break;
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                default:
                    value.push_back(current);
                    break;
                }

                escaped = false;
                continue;
            }

            if (current == '\\')
            {
                escaped = true;
                continue;
            }

            if (current == '\"')
            {
                return true;
            }

            value.push_back(current);
        }

        value.clear();
        return false;
    }

    bool ExtractInt64(const std::string& json, const char* field, long long& value)
    {
        size_t valueStart = 0;
        if (!FindJsonFieldValueStart(json, field, valueStart))
        {
            return false;
        }

        size_t valueEnd = valueStart;
        if (json[valueEnd] == '-')
        {
            ++valueEnd;
        }

        const size_t digitsStart = valueEnd;
        while (valueEnd < json.size()
            && std::isdigit(static_cast<unsigned char>(json[valueEnd])) != 0)
        {
            ++valueEnd;
        }

        if (digitsStart == valueEnd)
        {
            return false;
        }

        try
        {
            value = std::stoll(json.substr(valueStart, valueEnd - valueStart));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ExtractBool(const std::string& json, const char* field, bool& value)
    {
        size_t valueStart = 0;
        if (!FindJsonFieldValueStart(json, field, valueStart))
        {
            return false;
        }

        if (json.compare(valueStart, 4, "true") == 0)
        {
            value = true;
            return true;
        }

        if (json.compare(valueStart, 5, "false") == 0)
        {
            value = false;
            return true;
        }

        return false;
    }

    bool ExtractObject(const std::string& json, const char* field, std::string& objectJson)
    {
        const std::string key = std::string("\"") + field + "\"";
        const size_t keyPos = json.find(key);
        if (keyPos == std::string::npos)
        {
            return false;
        }

        const size_t colonPos = json.find(':', keyPos + key.size());
        if (colonPos == std::string::npos)
        {
            return false;
        }

        const size_t objectStart = json.find('{', colonPos + 1);
        if (objectStart == std::string::npos)
        {
            return false;
        }

        int depth = 0;
        bool inString = false;
        bool escaped = false;
        for (size_t i = objectStart; i < json.size(); ++i)
        {
            const char ch = json[i];
            if (inString)
            {
                if (escaped)
                {
                    escaped = false;
                    continue;
                }

                if (ch == '\\')
                {
                    escaped = true;
                    continue;
                }

                if (ch == '\"')
                {
                    inString = false;
                }
                continue;
            }

            if (ch == '\"')
            {
                inString = true;
                continue;
            }

            if (ch == '{')
            {
                ++depth;
                continue;
            }

            if (ch == '}')
            {
                --depth;
                if (depth == 0)
                {
                    objectJson = json.substr(objectStart, i - objectStart + 1);
                    return true;
                }
            }
        }

        return false;
    }

    bool ParseIso8601(const std::string& value, std::chrono::system_clock::time_point& outTime)
    {
        static const std::regex pattern(R"(^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(?:\.\d+)?(Z|([+\-])(\d{2}):(\d{2}))$)");
        std::smatch match;
        if (!std::regex_match(value, match, pattern))
        {
            return false;
        }

        std::tm localTm = {};
        localTm.tm_year = std::stoi(match[1].str()) - 1900;
        localTm.tm_mon = std::stoi(match[2].str()) - 1;
        localTm.tm_mday = std::stoi(match[3].str());
        localTm.tm_hour = std::stoi(match[4].str());
        localTm.tm_min = std::stoi(match[5].str());
        localTm.tm_sec = std::stoi(match[6].str());

        const time_t localAsUtc = _mkgmtime(&localTm);
        if (localAsUtc == static_cast<time_t>(-1))
        {
            return false;
        }

        long long offsetSeconds = 0;
        if (match[7].str() != "Z")
        {
            const int sign = match[8].str() == "-" ? -1 : 1;
            const int hours = std::stoi(match[9].str());
            const int minutes = std::stoi(match[10].str());
            offsetSeconds = static_cast<long long>(sign) * ((hours * 3600) + (minutes * 60));
        }

        outTime = std::chrono::system_clock::from_time_t(localAsUtc - static_cast<time_t>(offsetSeconds));
        return true;
    }

    ApiIntegration::ApiError BuildHttpError(unsigned long statusCode, const std::string& body)
    {
        ApiIntegration::ApiError error = {};
        error.httpStatus = statusCode;

        switch (statusCode)
        {
        case 400:
            error.code = ApiIntegration::ApiErrorCode::BadRequest;
            break;
        case 401:
            error.code = ApiIntegration::ApiErrorCode::Unauthorized;
            break;
        case 403:
            error.code = ApiIntegration::ApiErrorCode::Forbidden;
            break;
        case 404:
            error.code = ApiIntegration::ApiErrorCode::NotFound;
            break;
        case 409:
            error.code = ApiIntegration::ApiErrorCode::Conflict;
            break;
        default:
            error.code = ApiIntegration::ApiErrorCode::HttpStatus;
            break;
        }

        std::string errorLabel;
        std::string message;
        if (ExtractString(body, "error", errorLabel))
        {
            error.error = Utf8ToWide(errorLabel);
        }
        if (ExtractString(body, "message", message))
        {
            error.message = Utf8ToWide(message);
        }

        if (error.message.empty())
        {
            std::wstringstream ss;
            ss << L"HTTP status " << statusCode;
            error.message = ss.str();
        }

        return error;
    }

    ApiIntegration::ApiError BuildNetworkError(unsigned long transportError)
    {
        ApiIntegration::ApiError error = {};
        error.code = ApiIntegration::ApiErrorCode::Network;
        std::wstringstream ss;
        ss << L"Network error. WinHTTP code: " << transportError;
        error.message = ss.str();
        return error;
    }

    bool ParseAuthTokens(const std::string& json, ApiIntegration::AuthTokens& tokens)
    {
        std::string accessToken;
        std::string refreshToken;
        if (!ExtractString(json, "accessToken", accessToken) || !ExtractString(json, "refreshToken", refreshToken))
        {
            return false;
        }

        tokens = {};
        tokens.accessToken = Utf8ToWide(accessToken);
        tokens.refreshToken = Utf8ToWide(refreshToken);

        std::string accessExpiresAt;
        if (ExtractString(json, "accessExpiresAt", accessExpiresAt))
        {
            std::chrono::system_clock::time_point parsed = {};
            if (ParseIso8601(accessExpiresAt, parsed))
            {
                tokens.accessExpiresAtUtc = parsed;
            }
        }

        std::string refreshExpiresAt;
        if (ExtractString(json, "refreshExpiresAt", refreshExpiresAt))
        {
            std::chrono::system_clock::time_point parsed = {};
            if (ParseIso8601(refreshExpiresAt, parsed))
            {
                tokens.refreshExpiresAtUtc = parsed;
            }
        }

        long long sessionId = 0;
        if (ExtractInt64(json, "sessionId", sessionId))
        {
            tokens.sessionId = sessionId;
        }

        return true;
    }

    bool ParseAuthUserInfo(const std::string& json, ApiIntegration::AuthUserInfo& userInfo)
    {
        std::string username;
        std::string role;
        if (!ExtractString(json, "username", username) || !ExtractString(json, "role", role))
        {
            return false;
        }

        userInfo = {};
        userInfo.username = Utf8ToWide(username);
        userInfo.role = Utf8ToWide(role);

        long long userId = 0;
        if (ExtractInt64(json, "id", userId))
        {
            userInfo.id = userId;
        }

        return true;
    }

    bool ParseLicenseTicket(const std::string& json, ApiIntegration::LicenseTicket& ticket, std::wstring& signature)
    {
        std::string ticketJson;
        if (!ExtractObject(json, "ticket", ticketJson))
        {
            return false;
        }

        std::string signatureUtf8;
        if (ExtractString(json, "signature", signatureUtf8))
        {
            signature = Utf8ToWide(signatureUtf8);
        }
        else
        {
            signature.clear();
        }

        ticket = {};
        std::string dateValue;
        if (ExtractString(ticketJson, "serverDate", dateValue))
        {
            std::chrono::system_clock::time_point parsed = {};
            if (ParseIso8601(dateValue, parsed))
            {
                ticket.serverDateUtc = parsed;
            }
        }

        long long ttlSeconds = 0;
        if (ExtractInt64(ticketJson, "ticketTtlSeconds", ttlSeconds))
        {
            ticket.ticketTtlSeconds = ttlSeconds;
        }

        if (ExtractString(ticketJson, "activationDate", dateValue))
        {
            std::chrono::system_clock::time_point parsed = {};
            if (ParseIso8601(dateValue, parsed))
            {
                ticket.activationDateUtc = parsed;
            }
        }

        if (ExtractString(ticketJson, "expirationDate", dateValue))
        {
            std::chrono::system_clock::time_point parsed = {};
            if (ParseIso8601(dateValue, parsed))
            {
                ticket.expirationDateUtc = parsed;
            }
        }

        long long userId = 0;
        if (ExtractInt64(ticketJson, "userId", userId))
        {
            ticket.userId = userId;
        }

        long long deviceId = 0;
        if (ExtractInt64(ticketJson, "deviceId", deviceId))
        {
            ticket.deviceId = deviceId;
        }

        bool blocked = false;
        if (ExtractBool(ticketJson, "blocked", blocked))
        {
            ticket.blocked = blocked;
        }

        return true;
    }
}

namespace ApiIntegration
{
    // Инициализирует API-клиент по базовому HTTPS URL.
    ApiClient::ApiClient(const std::wstring& baseUrl)
    {
        m_isConfigured = ParseBaseUrl(baseUrl);
    }

    // Проверяет, что базовый URL успешно распознан.
    bool ApiClient::IsConfigured() const
    {
        return m_isConfigured;
    }

    // Выполняет login и возвращает пару токенов.
    bool ApiClient::Login(const std::wstring& username, const std::wstring& password, AuthTokens& tokens, AuthError& error) const
    {
        error = {};
        const std::string body = "{\"username\":\"" + EscapeJson(username) + "\",\"password\":\"" + EscapeJson(password) + "\"}";
        HttpResponse response = {};
        if (!SendJsonRequest(L"POST", L"/api/auth/login", body, nullptr, response))
        {
            error = BuildNetworkError(response.transportError);
            return false;
        }

        if (response.statusCode != 200)
        {
            error = BuildHttpError(response.statusCode, response.body);
            return false;
        }

        if (!ParseAuthTokens(response.body, tokens))
        {
            error.code = ApiErrorCode::Parse;
            error.message = L"Could not parse login response";
            return false;
        }

        return true;
    }

    // Выполняет refresh и возвращает новую пару токенов.
    bool ApiClient::RefreshTokens(const std::wstring& refreshToken, AuthTokens& tokens, AuthError& error) const
    {
        error = {};
        const std::string body = "{\"refreshToken\":\"" + EscapeJson(refreshToken) + "\"}";
        HttpResponse response = {};
        if (!SendJsonRequest(L"POST", L"/api/auth/refresh", body, nullptr, response))
        {
            error = BuildNetworkError(response.transportError);
            return false;
        }

        if (response.statusCode != 200)
        {
            error = BuildHttpError(response.statusCode, response.body);
            return false;
        }

        if (!ParseAuthTokens(response.body, tokens))
        {
            error.code = ApiErrorCode::Parse;
            error.message = L"Could not parse refresh response";
            return false;
        }

        return true;
    }

    // Возвращает данные текущего пользователя.
    bool ApiClient::GetCurrentUser(const std::wstring& accessToken, AuthUserInfo& userInfo, ApiError& error) const
    {
        error = {};
        HttpResponse response = {};
        if (!SendJsonRequest(L"GET", L"/api/users/me", {}, &accessToken, response))
        {
            error = BuildNetworkError(response.transportError);
            return false;
        }

        if (response.statusCode != 200)
        {
            error = BuildHttpError(response.statusCode, response.body);
            return false;
        }

        if (!ParseAuthUserInfo(response.body, userInfo))
        {
            error.code = ApiErrorCode::Parse;
            error.message = L"Could not parse user info";
            return false;
        }

        return true;
    }

    // Проверяет лицензию и возвращает ticket с подписью.
    bool ApiClient::CheckLicense(const std::wstring& accessToken, long long productId, const std::wstring& deviceMac, LicenseTicket& ticket, std::wstring& signature, LicenseError& error) const
    {
        error = {};
        const std::string body = "{\"productId\":" + std::to_string(productId) + ",\"deviceMac\":\"" + EscapeJson(deviceMac) + "\"}";
        HttpResponse response = {};
        if (!SendJsonRequest(L"POST", L"/api/licenses/check", body, &accessToken, response))
        {
            error = BuildNetworkError(response.transportError);
            return false;
        }

        if (response.statusCode != 200)
        {
            error = BuildHttpError(response.statusCode, response.body);
            return false;
        }

        if (!ParseLicenseTicket(response.body, ticket, signature))
        {
            error.code = ApiErrorCode::Parse;
            error.message = L"Could not parse license check response";
            return false;
        }

        return true;
    }

    // Активирует лицензию и возвращает ticket с подписью.
    bool ApiClient::ActivateLicense(const std::wstring& accessToken, const std::wstring& activationKey, const std::wstring& deviceName, const std::wstring& deviceMac, LicenseTicket& ticket, std::wstring& signature, LicenseError& error) const
    {
        error = {};
        const std::string body = "{\"activationKey\":\"" + EscapeJson(activationKey)
            + "\",\"deviceName\":\"" + EscapeJson(deviceName)
            + "\",\"deviceMac\":\"" + EscapeJson(deviceMac) + "\"}";
        HttpResponse response = {};
        if (!SendJsonRequest(L"POST", L"/api/licenses/activate", body, &accessToken, response))
        {
            error = BuildNetworkError(response.transportError);
            return false;
        }

        if (response.statusCode != 200)
        {
            error = BuildHttpError(response.statusCode, response.body);
            return false;
        }

        if (!ParseLicenseTicket(response.body, ticket, signature))
        {
            error.code = ApiErrorCode::Parse;
            error.message = L"Could not parse license activation response";
            return false;
        }

        return true;
    }

    // Продлевает лицензию и возвращает ticket с подписью.
    bool ApiClient::RenewLicense(const std::wstring& accessToken, const std::wstring& activationKey, const std::wstring& deviceMac, LicenseTicket& ticket, std::wstring& signature, LicenseError& error) const
    {
        error = {};
        const std::string body = "{\"activationKey\":\"" + EscapeJson(activationKey) + "\",\"deviceMac\":\"" + EscapeJson(deviceMac) + "\"}";
        HttpResponse response = {};
        if (!SendJsonRequest(L"POST", L"/api/licenses/renew", body, &accessToken, response))
        {
            error = BuildNetworkError(response.transportError);
            return false;
        }

        if (response.statusCode != 200)
        {
            error = BuildHttpError(response.statusCode, response.body);
            return false;
        }

        if (!ParseLicenseTicket(response.body, ticket, signature))
        {
            error.code = ApiErrorCode::Parse;
            error.message = L"Could not parse license renew response";
            return false;
        }

        return true;
    }

    // Выполняет один HTTP-запрос и возвращает статус и тело ответа.
    bool ApiClient::DownloadBinary(
        const std::wstring& method,
        const std::wstring& endpointPath,
        const std::string& requestBodyUtf8,
        const std::wstring& acceptHeader,
        const std::wstring* bearerToken,
        BinaryResponse& response) const
    {
        HttpResponse httpResponse = {};
        if (!SendHttpRequest(method, endpointPath, requestBodyUtf8, acceptHeader, bearerToken, httpResponse))
        {
            response = {};
            response.transportError = httpResponse.transportError;
            return false;
        }

        response.statusCode = httpResponse.statusCode;
        response.transportError = httpResponse.transportError;
        response.contentType = httpResponse.contentType;
        response.body.assign(httpResponse.body.begin(), httpResponse.body.end());
        return true;
    }

    bool ApiClient::IsServiceReachable(const std::wstring* bearerToken) const
    {
        HttpResponse response = {};
        return SendHttpRequest(L"GET", L"/api/system/ping", {}, L"application/json", bearerToken, response)
            && response.statusCode < 500;
    }

    bool ApiClient::SendJsonRequest(const std::wstring& method, const std::wstring& endpointPath, const std::string& requestBodyUtf8, const std::wstring* bearerToken, HttpResponse& response) const
    {
        return SendHttpRequest(method, endpointPath, requestBodyUtf8, L"application/json", bearerToken, response);
    }

    bool ApiClient::SendHttpRequest(
        const std::wstring& method,
        const std::wstring& endpointPath,
        const std::string& requestBodyUtf8,
        const std::wstring& acceptHeader,
        const std::wstring* bearerToken,
        HttpResponse& response) const
    {
        response = {};
        if (!m_isConfigured)
        {
            response.transportError = ERROR_INVALID_PARAMETER;
            return false;
        }

        ScopedHandle session;
        session.handle = WinHttpOpen(L"TrayServiceApiClient/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (session.handle == nullptr)
        {
            response.transportError = GetLastError();
            return false;
        }

        ScopedHandle connection;
        connection.handle = WinHttpConnect(session.handle, m_host.c_str(), m_port, 0);
        if (connection.handle == nullptr)
        {
            response.transportError = GetLastError();
            return false;
        }

        const DWORD flags = m_useHttps ? WINHTTP_FLAG_SECURE : 0;
        const std::wstring path = BuildRequestPath(endpointPath);

        ScopedHandle request;
        request.handle = WinHttpOpenRequest(connection.handle, method.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (request.handle == nullptr)
        {
            response.transportError = GetLastError();
            return false;
        }

        if (m_useHttps)
        {
            DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA
                | SECURITY_FLAG_IGNORE_CERT_CN_INVALID
                | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
                | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
            WinHttpSetOption(request.handle, WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags));
        }

        std::wstring headers = L"Accept: ";
        headers += acceptHeader.empty() ? L"*/*" : acceptHeader;
        headers += L"\r\n";
        if (!requestBodyUtf8.empty())
        {
            headers += L"Content-Type: application/json\r\n";
        }

        if (bearerToken != nullptr && !bearerToken->empty())
        {
            headers += L"Authorization: Bearer ";
            headers += *bearerToken;
            headers += L"\r\n";
        }

        if (!WinHttpAddRequestHeaders(request.handle, headers.c_str(), static_cast<DWORD>(headers.size()), WINHTTP_ADDREQ_FLAG_ADD))
        {
            response.transportError = GetLastError();
            return false;
        }

        LPVOID bodyPointer = WINHTTP_NO_REQUEST_DATA;
        DWORD bodyLength = 0;
        if (!requestBodyUtf8.empty())
        {
            bodyPointer = const_cast<char*>(requestBodyUtf8.data());
            bodyLength = static_cast<DWORD>(requestBodyUtf8.size());
        }

        if (!WinHttpSendRequest(request.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, bodyPointer, bodyLength, bodyLength, 0))
        {
            response.transportError = GetLastError();
            return false;
        }

        if (!WinHttpReceiveResponse(request.handle, nullptr))
        {
            response.transportError = GetLastError();
            return false;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX))
        {
            response.transportError = GetLastError();
            return false;
        }
        response.statusCode = statusCode;

        wchar_t contentTypeBuffer[256] = {};
        DWORD contentTypeSize = sizeof(contentTypeBuffer);
        if (WinHttpQueryHeaders(
                request.handle,
                WINHTTP_QUERY_CONTENT_TYPE,
                WINHTTP_HEADER_NAME_BY_INDEX,
                contentTypeBuffer,
                &contentTypeSize,
                WINHTTP_NO_HEADER_INDEX))
        {
            const size_t charCount = contentTypeSize / sizeof(wchar_t);
            const std::wstring contentTypeWide(contentTypeBuffer, contentTypeBuffer + charCount);
            response.contentType.clear();
            response.contentType.reserve(contentTypeWide.size());
            for (const wchar_t ch : contentTypeWide)
            {
                if (ch == L'\0')
                {
                    break;
                }
                response.contentType.push_back(static_cast<char>(ch <= 0x7F ? ch : L'?'));
            }
        }

        for (;;)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request.handle, &available))
            {
                response.transportError = GetLastError();
                return false;
            }

            if (available == 0)
            {
                break;
            }

            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request.handle, chunk.data(), available, &read))
            {
                response.transportError = GetLastError();
                return false;
            }

            chunk.resize(read);
            response.body += chunk;
        }

        return true;
    }

    // Формирует итоговый путь запроса с учетом base URL.
    std::wstring ApiClient::BuildRequestPath(const std::wstring& endpointPath) const
    {
        std::wstring normalized = endpointPath;
        if (normalized.empty())
        {
            normalized = L"/";
        }
        else if (normalized.front() != L'/')
        {
            normalized.insert(normalized.begin(), L'/');
        }

        if (m_basePath == L"/")
        {
            return normalized;
        }

        if (normalized == L"/")
        {
            return m_basePath;
        }

        if (m_basePath.back() == L'/' && normalized.front() == L'/')
        {
            return m_basePath.substr(0, m_basePath.size() - 1) + normalized;
        }

        return m_basePath + normalized;
    }

    // Разбирает базовый URL и заполняет параметры подключения.
    bool ApiClient::ParseBaseUrl(const std::wstring& baseUrl)
    {
        URL_COMPONENTS url = {};
        url.dwStructSize = sizeof(url);
        url.dwHostNameLength = static_cast<DWORD>(-1);
        url.dwUrlPathLength = static_cast<DWORD>(-1);
        url.dwExtraInfoLength = static_cast<DWORD>(-1);

        if (!WinHttpCrackUrl(baseUrl.c_str(), 0, 0, &url))
        {
            return false;
        }

        if (url.lpszHostName == nullptr || url.dwHostNameLength == 0)
        {
            return false;
        }
        if (url.nScheme != INTERNET_SCHEME_HTTPS)
        {
            return false;
        }

        m_host.assign(url.lpszHostName, url.dwHostNameLength);
        m_port = url.nPort;
        m_useHttps = true;

        std::wstring path;
        if (url.lpszUrlPath != nullptr && url.dwUrlPathLength > 0)
        {
            path.assign(url.lpszUrlPath, url.dwUrlPathLength);
        }

        if (url.lpszExtraInfo != nullptr && url.dwExtraInfoLength > 0)
        {
            path.append(url.lpszExtraInfo, url.dwExtraInfoLength);
        }

        if (path.empty())
        {
            path = L"/";
        }
        else if (path.back() == L'/' && path.size() > 1)
        {
            path.pop_back();
        }

        m_basePath = path;
        return true;
    }
}
