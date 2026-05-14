#include "AntivirusSignatureVerifier.h"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>

#pragma comment(lib, "Bcrypt.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Advapi32.lib")

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

    constexpr wchar_t kEmbeddedSignaturePublicKeyBase64[] =
        L"MIIDfTCCAmWgAwIBAgIIBcwkgRWFqW4wDQYJKoZIhvcNAQEMBQAwbTELMAkGA1UEBhMCUlUxDzANBgNVBAgTBk1vc2NvdzEPMA0GA1UEBxMGTW9zY293MQwwCgYDVQQKEwNNRkExDjAMBgNVBAsTBUxhYiAzMR4wHAYDVQQDExVaSU9WUE8gVGlja2V0IFNpZ25pbmcwHhcNMjYwNDA0MDg0ODM0WhcNMzYwNDAxMDg0ODM0WjBtMQswCQYDVQQGEwJSVTEPMA0GA1UECBMGTW9zY293MQ8wDQYDVQQHEwZNb3Njb3cxDDAKBgNVBAoTA01GQTEOMAwGA1UECxMFTGFiIDMxHjAcBgNVBAMTFVpJT1ZQTyBUaWNrZXQgU2lnbmluZzCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMYLS/oBuHUc8p+EGKk9PsAaMzB2DMyrcw3CJ3/Xdh70JVr97KSFGIZTEda4b3fC3lSWCF8bdRhP06ycE8P9ej1Z1xMk+mwC+D9GrT0aaaUtig8jHAl1hI96jkeTHztCtcrt+ZCXopWL5kIuQ8gQob59hOkYeIpa8PC1cRmqmcjaYUrxE2c/+KXLf1m2J+SsSloZKvOyXKr1yQIy4VkNv4FF1Ac/KS9qFkKYd6h9oVvyUPgKQulYFIPjea8Oy4dgKT7aFKNV1i3xyBn5zHXdEe99JMsWYP2Nwmfuibqa7kCt+eX+QlYNaN7ueW1UhkBnsUIhYhanoRRaq9QRT4nqHUcCAwEAAaMhMB8wHQYDVR0OBBYEFOWVG+VX0xGeODc/h6vMU/ytLk7yMA0GCSqGSIb3DQEBDAUAA4IBAQApgDT67BVAkNQkO69ElPpWX9cbw22/TfuH7+o/6axz6BtcFpy0ipLmpfVol14KJpwn71fXWAV3omYeAapxOAJlINsIbOniOSf0CGNNDqurKGdbfihRCu/MPPujWbstmwWamTIFvY1QPnoICe7mbk8Gr58OOkAcrvfzKhKSkwlICSxdInb8ubomUaIqb4gjurloYgwjZ+nR4dd55u6VGKVG61Pp5uCi4bY1gJwieI9M7lSiknP6SXHW2Eqy28Kjl6j40GG9mTbrSNWUMHiftcvPteCn8uPeqH+GaDV0z207JJDh2jcvSgXlMvYzuBOwy83RUkz7qbmFI1pTsxECKYe9";

    struct CryptProviderHandle
    {
        HCRYPTPROV value = 0;
        ~CryptProviderHandle()
        {
            if (value != 0)
            {
                CryptReleaseContext(value, 0);
            }
        }
    };

    struct CryptHashHandle
    {
        HCRYPTHASH value = 0;
        ~CryptHashHandle()
        {
            if (value != 0)
            {
                CryptDestroyHash(value);
            }
        }
    };

    struct CryptKeyHandle
    {
        HCRYPTKEY value = 0;
        ~CryptKeyHandle()
        {
            if (value != 0)
            {
                CryptDestroyKey(value);
            }
        }
    };

    struct CertContextHandle
    {
        PCCERT_CONTEXT value = nullptr;
        ~CertContextHandle()
        {
            if (value != nullptr)
            {
                CertFreeCertificateContext(value);
            }
        }
    };

    bool DecodeBase64(const std::wstring& text, std::vector<uint8_t>& bytes)
    {
        bytes.clear();
        if (text.empty())
        {
            return false;
        }

        DWORD byteCount = 0;
        if (!CryptStringToBinaryW(
                text.c_str(),
                static_cast<DWORD>(text.size()),
                CRYPT_STRING_BASE64,
                nullptr,
                &byteCount,
                nullptr,
                nullptr))
        {
            return false;
        }

        bytes.resize(byteCount);
        return CryptStringToBinaryW(
            text.c_str(),
            static_cast<DWORD>(text.size()),
            CRYPT_STRING_BASE64,
            bytes.data(),
            &byteCount,
            nullptr,
            nullptr) != FALSE;
    }

    std::wstring ReadPublicKeyBase64()
    {
        wchar_t buffer[8192] = {};
        const DWORD length = GetEnvironmentVariableW(
            L"TRAY_AV_SIGNATURE_PUBLIC_KEY_BASE64",
            buffer,
            ARRAYSIZE(buffer));
        if (length > 0 && length < ARRAYSIZE(buffer))
        {
            return buffer;
        }

        return kEmbeddedSignaturePublicKeyBase64;
    }

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

    RsaSha256SignatureVerifier::RsaSha256SignatureVerifier()
    {
        DecodeBase64(ReadPublicKeyBase64(), m_publicKeyDer);
    }

    std::wstring RsaSha256SignatureVerifier::AlgorithmName() const
    {
        return L"SHA256withRSA";
    }

    bool RsaSha256SignatureVerifier::Verify(
        const std::vector<uint8_t>& payload,
        const std::vector<uint8_t>& signature) const
    {
        if (!IsConfigured() || signature.empty())
        {
            return false;
        }

        CERT_PUBLIC_KEY_INFO* publicKeyInfo = nullptr;
        DWORD publicKeyInfoSize = 0;
        const BOOL decodedPublicKey = CryptDecodeObjectEx(
                X509_ASN_ENCODING,
                X509_PUBLIC_KEY_INFO,
                m_publicKeyDer.data(),
                static_cast<DWORD>(m_publicKeyDer.size()),
                CRYPT_DECODE_ALLOC_FLAG,
                nullptr,
                &publicKeyInfo,
                &publicKeyInfoSize);

        CertContextHandle certificate;
        if (!decodedPublicKey)
        {
            certificate.value = CertCreateCertificateContext(
                X509_ASN_ENCODING,
                m_publicKeyDer.data(),
                static_cast<DWORD>(m_publicKeyDer.size()));
            if (certificate.value == nullptr)
            {
                return false;
            }

            publicKeyInfo = const_cast<CERT_PUBLIC_KEY_INFO*>(&certificate.value->pCertInfo->SubjectPublicKeyInfo);
        }

        CryptProviderHandle provider;
        if (!CryptAcquireContextW(&provider.value, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        {
            if (decodedPublicKey)
            {
                LocalFree(publicKeyInfo);
            }
            return false;
        }

        CryptKeyHandle publicKey;
        const BOOL importOk = CryptImportPublicKeyInfo(
            provider.value,
            X509_ASN_ENCODING,
            publicKeyInfo,
            &publicKey.value);
        if (decodedPublicKey)
        {
            LocalFree(publicKeyInfo);
        }
        if (!importOk)
        {
            return false;
        }

        CryptHashHandle hash;
        if (!CryptCreateHash(provider.value, CALG_SHA_256, 0, 0, &hash.value))
        {
            return false;
        }

        if (!payload.empty()
            && !CryptHashData(hash.value, payload.data(), static_cast<DWORD>(payload.size()), 0))
        {
            return false;
        }

        std::vector<uint8_t> littleEndianSignature(signature.rbegin(), signature.rend());
        return CryptVerifySignatureW(
            hash.value,
            littleEndianSignature.data(),
            static_cast<DWORD>(littleEndianSignature.size()),
            publicKey.value,
            nullptr,
            0) != FALSE;
    }

    bool RsaSha256SignatureVerifier::IsConfigured() const
    {
        return !m_publicKeyDer.empty();
    }

    CompositeSignatureVerifier::CompositeSignatureVerifier()
        : m_lastAlgorithmName(L"unverified")
    {
    }

    std::wstring CompositeSignatureVerifier::AlgorithmName() const
    {
        return m_lastAlgorithmName;
    }

    bool CompositeSignatureVerifier::Verify(
        const std::vector<uint8_t>& payload,
        const std::vector<uint8_t>& signature) const
    {
        if (m_rsaVerifier.Verify(payload, signature))
        {
            m_lastAlgorithmName = m_rsaVerifier.AlgorithmName();
            return true;
        }

        if (m_demoVerifier.Verify(payload, signature))
        {
            m_lastAlgorithmName = L"DEMO-HMAC-SHA256 fallback";
            return true;
        }

        m_lastAlgorithmName = L"unverified";
        return false;
    }
}
