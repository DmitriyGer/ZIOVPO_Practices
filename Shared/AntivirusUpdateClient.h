#pragma once

#include "AntivirusDatabaseStore.h"
#include "ApiClient.h"

#include <filesystem>

namespace Antivirus
{
    struct AvUpdateDownloadResult
    {
        bool Downloaded = false;
        bool NetworkReachable = false;
        size_t ImportedRecordCount = 0;
        size_t SkippedRecordCount = 0;
        std::wstring Message;
    };

    class AvUpdateClient
    {
    public:
        // Creates an update client over the existing authenticated API client.
        explicit AvUpdateClient(const ApiIntegration::ApiClient& apiClient);

        // Checks whether the update service is reachable with the current bearer token.
        bool IsNetworkAvailable(const std::wstring& accessToken) const;

        // Downloads the full backend signature export and writes a local incoming .avdb.
        AvUpdateDownloadResult DownloadFullDatabase(
            const std::wstring& accessToken,
            const std::filesystem::path& incomingPath,
            const std::filesystem::path& temporaryPath,
            const SignatureVerifier& webSignatureVerifier,
            const DemoHmacSha256SignatureVerifier& localSigner) const;

    private:
        const ApiIntegration::ApiClient& m_apiClient;
    };
}
