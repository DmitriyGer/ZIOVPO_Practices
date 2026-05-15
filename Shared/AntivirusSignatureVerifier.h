#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Antivirus
{
    class SignatureVerifier
    {
    public:
        virtual ~SignatureVerifier() = default;

        // Returns the algorithm name expected in the antivirus database manifest.
        virtual std::wstring AlgorithmName() const = 0;

        // Verifies a signature over one binary payload.
        virtual bool Verify(const std::vector<uint8_t>& payload, const std::vector<uint8_t>& signature) const = 0;
    };

    class DemoHmacSha256SignatureVerifier final : public SignatureVerifier
    {
    public:
        // Returns the demo algorithm name stored in local educational databases.
        std::wstring AlgorithmName() const override;

        // Verifies an educational HMAC/SHA256 signature over one binary payload.
        bool Verify(const std::vector<uint8_t>& payload, const std::vector<uint8_t>& signature) const override;

        // Produces a demo HMAC/SHA256 signature for default/test database files.
        std::vector<uint8_t> SignForDemo(const std::vector<uint8_t>& payload) const;
    };

    class RsaSha256SignatureVerifier final : public SignatureVerifier
    {
    public:
        // Creates a verifier with an embedded or environment-provided DER public key.
        RsaSha256SignatureVerifier();

        // Returns the production signature algorithm name.
        std::wstring AlgorithmName() const override;

        // Verifies a SHA256withRSA signature over one binary payload.
        bool Verify(const std::vector<uint8_t>& payload, const std::vector<uint8_t>& signature) const override;

        // Returns true when a public key was decoded successfully.
        bool IsConfigured() const;

    private:
        std::vector<uint8_t> m_publicKeyDer;
    };

    class CompositeSignatureVerifier final : public SignatureVerifier
    {
    public:
        // Creates a verifier that tries RSA first and demo HMAC for local test bases.
        CompositeSignatureVerifier();

        // Returns the name of the last verifier that accepted a signature.
        std::wstring AlgorithmName() const override;

        // Verifies a payload with RSA/SHA256 or demo HMAC fallback.
        bool Verify(const std::vector<uint8_t>& payload, const std::vector<uint8_t>& signature) const override;

    private:
        RsaSha256SignatureVerifier m_rsaVerifier;
        DemoHmacSha256SignatureVerifier m_demoVerifier;
        mutable std::wstring m_lastAlgorithmName;
    };

    // Calculates SHA-256 for database content checksums.
    bool CalculateSha256(const std::vector<uint8_t>& payload, std::vector<uint8_t>& hash);
}
