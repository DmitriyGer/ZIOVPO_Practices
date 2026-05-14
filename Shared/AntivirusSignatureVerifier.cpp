#include "AntivirusSignatureVerifier.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>

#pragma comment(lib, "Bcrypt.lib")

namespace
{
    // Demo key for lab-only database signing; replace SignatureVerifier for production EDS.
    constexpr uint8_t kEducationalDemoKey[] =
    {
        0x54, 0x72, 0x61, 0x79, 0x41, 0x76, 0x2D, 0x44,
        0x65, 0x6D, 0x6F, 0x2D, 0x48, 0x4D, 0x41, 0x43,
        0x2D, 0x4B, 0x65, 0x79, 0x2D, 0x52, 0x65, 0x70,
        0x6C, 0x61, 0x63, 0x65, 0x2D, 0x4D, 0x65, 0x21
    };

    bool BCryptSha256(
        const std::vector<uint8_t>& payload,
        const uint8_t* key,
        ULONG keyLength,
        std::vector<uint8_t>& hash)
    {
        hash.clear();

        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hashHandle = nullptr;
        std::vector<uint8_t> hashObject;
        DWORD bytesReturned = 0;
        DWORD objectLength = 0;
        DWORD hashLength = 0;

        const ULONG flags = key != nullptr ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0;
        NTSTATUS status = BCryptOpenAlgorithmProvider(
            &algorithm,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            flags);
        if (status < 0)
        {
            return false;
        }

        status = BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength),
            sizeof(objectLength),
            &bytesReturned,
            0);
        if (status >= 0)
        {
            status = BCryptGetProperty(
                algorithm,
                BCRYPT_HASH_LENGTH,
                reinterpret_cast<PUCHAR>(&hashLength),
                sizeof(hashLength),
                &bytesReturned,
                0);
        }

        if (status >= 0)
        {
            hashObject.resize(objectLength);
            hash.resize(hashLength);
            status = BCryptCreateHash(
                algorithm,
                &hashHandle,
                hashObject.data(),
                static_cast<ULONG>(hashObject.size()),
                const_cast<PUCHAR>(key),
                keyLength,
                0);
        }

        if (status >= 0 && !payload.empty())
        {
            status = BCryptHashData(
                hashHandle,
                const_cast<PUCHAR>(payload.data()),
                static_cast<ULONG>(payload.size()),
                0);
        }

        if (status >= 0)
        {
            status = BCryptFinishHash(hashHandle, hash.data(), static_cast<ULONG>(hash.size()), 0);
        }

        if (hashHandle != nullptr)
        {
            BCryptDestroyHash(hashHandle);
        }

        if (algorithm != nullptr)
        {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }

        if (status < 0)
        {
            hash.clear();
            return false;
        }

        return true;
    }
}

namespace Antivirus
{
    std::wstring DemoHmacSha256SignatureVerifier::AlgorithmName() const
    {
        return L"DEMO-HMAC-SHA256";
    }

    bool DemoHmacSha256SignatureVerifier::Verify(
        const std::vector<uint8_t>& payload,
        const std::vector<uint8_t>& signature) const
    {
        const std::vector<uint8_t> expected = SignForDemo(payload);
        return !expected.empty()
            && expected.size() == signature.size()
            && std::equal(expected.begin(), expected.end(), signature.begin());
    }

    std::vector<uint8_t> DemoHmacSha256SignatureVerifier::SignForDemo(const std::vector<uint8_t>& payload) const
    {
        std::vector<uint8_t> signature;
        BCryptSha256(payload, kEducationalDemoKey, static_cast<ULONG>(sizeof(kEducationalDemoKey)), signature);
        return signature;
    }

    bool CalculateSha256(const std::vector<uint8_t>& payload, std::vector<uint8_t>& hash)
    {
        return BCryptSha256(payload, nullptr, 0, hash);
    }
}
