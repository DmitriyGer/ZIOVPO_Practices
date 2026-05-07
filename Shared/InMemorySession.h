#pragma once

#include "ApiModels.h"

#include <chrono>
#include <mutex>
#include <optional>
#include <string>

namespace ApiIntegration
{
    class InMemorySession
    {
    public:
        // Сохраняет access/refresh токены в оперативной памяти.
        void SetAuthTokens(const AuthTokens& tokens);

        // Возвращает текущие access/refresh токены из памяти.
        bool TryGetAuthTokens(AuthTokens& tokens) const;

        // Сохраняет лицензионный ticket и подпись в оперативной памяти.
        void SetLicenseTicket(const LicenseTicket& ticket, const std::wstring& signature);

        // Возвращает текущие ticket и подпись из памяти.
        bool TryGetLicenseTicket(LicenseTicket& ticket, std::wstring& signature) const;

        // Очищает только access/refresh токены из памяти.
        void ClearAuth();

        // Очищает только лицензионный ticket и подпись из памяти.
        void ClearLicense();

        // Очищает все секреты сессии из памяти.
        void ClearAll();

        // Возвращает рассчитанное время следующего refresh токена.
        std::optional<std::chrono::system_clock::time_point> GetNextAuthRefreshTimeUtc() const;

        // Возвращает безопасное состояние лицензии для внутренней логики службы.
        LicenseState GetLicenseState() const;

    private:
        // Рассчитывает время следующего refresh по exp из JWT либо fallback.
        static std::optional<std::chrono::system_clock::time_point> CalculateNextAuthRefreshTime(
            const std::wstring& accessToken,
            std::chrono::system_clock::time_point nowUtc);

        // Рассчитывает время следующего обновления ticket по ticketTtlSeconds.
        static std::optional<std::chrono::system_clock::time_point> CalculateNextTicketRefreshTime(
            const LicenseTicket& ticket,
            std::chrono::system_clock::time_point nowUtc);

        mutable std::mutex m_mutex;
        std::optional<AuthTokens> m_tokens;
        std::optional<LicenseTicket> m_ticket;
        std::wstring m_signature;
        std::optional<std::chrono::system_clock::time_point> m_nextAuthRefreshAtUtc;
        std::optional<std::chrono::system_clock::time_point> m_nextTicketRefreshAtUtc;
    };
}
