#include "InMemorySession.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <regex>
#include <string>

namespace
{
    std::string WideToUtf8(const std::wstring& value)
    {
        if (value.empty())
        {
            return {};
        }

        const int requiredSize = WideCharToMultiByte(
            CP_UTF8,
            0,
            value.c_str(),
            static_cast<int>(value.size()),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (requiredSize <= 0)
        {
            return {};
        }

        std::string utf8(requiredSize, '\0');
        const int convertedSize = WideCharToMultiByte(
            CP_UTF8,
            0,
            value.c_str(),
            static_cast<int>(value.size()),
            utf8.data(),
            requiredSize,
            nullptr,
            nullptr);
        if (convertedSize <= 0)
        {
            return {};
        }

        return utf8;
    }

    bool TryExtractJsonInteger(const std::string& json, const char* fieldName, long long& value)
    {
        if (fieldName == nullptr)
        {
            return false;
        }

        const std::string pattern = std::string("\"") + fieldName + "\"\\s*:\\s*(-?\\d+)";
        const std::regex expression(pattern);
        std::smatch matches;
        if (!std::regex_search(json, matches, expression) || matches.size() < 2)
        {
            return false;
        }

        try
        {
            value = std::stoll(matches[1].str());
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool TryDecodeBase64(const std::string& encoded, std::string& decoded)
    {
        static constexpr std::array<int, 256> kBase64Map = []()
        {
            std::array<int, 256> map = {};
            map.fill(-1);

            for (int index = 0; index < 26; ++index)
            {
                map[static_cast<size_t>('A' + index)] = index;
                map[static_cast<size_t>('a' + index)] = 26 + index;
            }

            for (int index = 0; index < 10; ++index)
            {
                map[static_cast<size_t>('0' + index)] = 52 + index;
            }

            map[static_cast<size_t>('+')] = 62;
            map[static_cast<size_t>('/')] = 63;
            return map;
        }();

        decoded.clear();
        int accumulator = 0;
        int bitsInAccumulator = -8;

        for (const unsigned char character : encoded)
        {
            if (character == '=')
            {
                break;
            }

            const int mappedValue = kBase64Map[character];
            if (mappedValue < 0)
            {
                continue;
            }

            accumulator = (accumulator << 6) | mappedValue;
            bitsInAccumulator += 6;
            if (bitsInAccumulator >= 0)
            {
                decoded.push_back(static_cast<char>((accumulator >> bitsInAccumulator) & 0xFF));
                bitsInAccumulator -= 8;
            }
        }

        return !decoded.empty();
    }

    bool TryDecodeBase64Url(const std::string& encoded, std::string& decoded)
    {
        if (encoded.empty())
        {
            return false;
        }

        std::string normalized = encoded;
        std::replace(normalized.begin(), normalized.end(), '-', '+');
        std::replace(normalized.begin(), normalized.end(), '_', '/');

        const size_t remainder = normalized.size() % 4;
        if (remainder != 0)
        {
            normalized.append(4 - remainder, '=');
        }

        return TryDecodeBase64(normalized, decoded);
    }

    bool TryExtractJwtExpirationEpochSeconds(const std::wstring& jwtToken, long long& expEpochSeconds)
    {
        const std::string tokenUtf8 = WideToUtf8(jwtToken);
        if (tokenUtf8.empty())
        {
            return false;
        }

        const size_t firstDot = tokenUtf8.find('.');
        if (firstDot == std::string::npos)
        {
            return false;
        }

        const size_t secondDot = tokenUtf8.find('.', firstDot + 1);
        if (secondDot == std::string::npos || secondDot <= firstDot + 1)
        {
            return false;
        }

        const std::string payloadEncoded = tokenUtf8.substr(firstDot + 1, secondDot - firstDot - 1);
        std::string payloadJson;
        if (!TryDecodeBase64Url(payloadEncoded, payloadJson))
        {
            return false;
        }

        return TryExtractJsonInteger(payloadJson, "exp", expEpochSeconds);
    }
}

namespace ApiIntegration
{
    // Сохраняет access/refresh токены в оперативной памяти.
    void InMemorySession::SetAuthTokens(const AuthTokens& tokens)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tokens = tokens;
        m_nextAuthRefreshAtUtc = CalculateNextAuthRefreshTime(
            tokens.accessToken,
            std::chrono::system_clock::now());
    }

    // Возвращает текущие access/refresh токены из памяти.
    bool InMemorySession::TryGetAuthTokens(AuthTokens& tokens) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_tokens.has_value())
        {
            return false;
        }

        tokens = *m_tokens;
        return true;
    }

    // Сохраняет лицензионный ticket и подпись в оперативной памяти.
    void InMemorySession::SetLicenseTicket(const LicenseTicket& ticket, const std::wstring& signature)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_ticket = ticket;
        m_signature = signature;
        m_nextTicketRefreshAtUtc = CalculateNextTicketRefreshTime(
            ticket,
            std::chrono::system_clock::now());
    }

    // Возвращает текущие ticket и подпись из памяти.
    bool InMemorySession::TryGetLicenseTicket(LicenseTicket& ticket, std::wstring& signature) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_ticket.has_value())
        {
            return false;
        }

        ticket = *m_ticket;
        signature = m_signature;
        return true;
    }

    // Очищает только access/refresh токены из памяти.
    void InMemorySession::ClearAuth()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tokens.reset();
        m_nextAuthRefreshAtUtc.reset();
    }

    // Очищает только лицензионный ticket и подпись из памяти.
    void InMemorySession::ClearLicense()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_ticket.reset();
        m_signature.clear();
        m_nextTicketRefreshAtUtc.reset();
    }

    // Очищает все секреты сессии из памяти.
    void InMemorySession::ClearAll()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tokens.reset();
        m_ticket.reset();
        m_signature.clear();
        m_nextAuthRefreshAtUtc.reset();
        m_nextTicketRefreshAtUtc.reset();
    }

    // Возвращает рассчитанное время следующего refresh токена.
    std::optional<std::chrono::system_clock::time_point> InMemorySession::GetNextAuthRefreshTimeUtc() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_nextAuthRefreshAtUtc;
    }

    // Возвращает безопасное состояние лицензии для внутренней логики службы.
    LicenseState InMemorySession::GetLicenseState() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        LicenseState state = {};
        if (!m_ticket.has_value())
        {
            state.hasLicense = false;
            state.errorMessage = L"NO_LICENSE";
            return state;
        }

        state.hasLicense = true;
        state.blocked = m_ticket->blocked;
        state.expirationDateUtc = m_ticket->expirationDateUtc;
        state.nextRefreshAtUtc = m_nextTicketRefreshAtUtc;

        if (state.expirationDateUtc.has_value())
        {
            state.expired = state.expirationDateUtc.value() <= std::chrono::system_clock::now();
        }

        if (state.blocked)
        {
            state.errorMessage = L"LICENSE_BLOCKED";
        }
        else if (state.expired)
        {
            state.errorMessage = L"LICENSE_EXPIRED";
        }

        return state;
    }

    // Рассчитывает время следующего refresh по exp из JWT либо fallback.
    std::optional<std::chrono::system_clock::time_point> InMemorySession::CalculateNextAuthRefreshTime(
        const std::wstring& accessToken,
        std::chrono::system_clock::time_point nowUtc)
    {
        constexpr auto kFallbackDelay = std::chrono::minutes(5);
        constexpr auto kMinimalDelay = std::chrono::seconds(30);
        constexpr auto kReserveBeforeExpiration = std::chrono::seconds(60);

        long long expEpochSeconds = 0;
        if (!TryExtractJwtExpirationEpochSeconds(accessToken, expEpochSeconds))
        {
            return nowUtc + kFallbackDelay;
        }

        const std::chrono::system_clock::time_point expirationTimeUtc =
            std::chrono::system_clock::from_time_t(static_cast<time_t>(expEpochSeconds));
        std::chrono::system_clock::time_point refreshTimeUtc = expirationTimeUtc - kReserveBeforeExpiration;
        if (refreshTimeUtc < nowUtc + kMinimalDelay)
        {
            refreshTimeUtc = nowUtc + kMinimalDelay;
        }

        return refreshTimeUtc;
    }

    // Рассчитывает время следующего обновления ticket по ticketTtlSeconds.
    std::optional<std::chrono::system_clock::time_point> InMemorySession::CalculateNextTicketRefreshTime(
        const LicenseTicket& ticket,
        std::chrono::system_clock::time_point nowUtc)
    {
        if (!ticket.ticketTtlSeconds.has_value() || ticket.ticketTtlSeconds.value() <= 0)
        {
            return std::nullopt;
        }

        const auto ttl = std::chrono::seconds(ticket.ticketTtlSeconds.value());
        const auto reserve = (std::max)(
            std::chrono::seconds(5),
            (std::min)(std::chrono::seconds(60), ttl / 5));
        auto delay = ttl - reserve;
        if (delay < std::chrono::seconds(10))
        {
            delay = std::chrono::seconds(10);
        }

        return nowUtc + delay;
    }
}
