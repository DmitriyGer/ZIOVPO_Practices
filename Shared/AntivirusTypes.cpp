#include "AntivirusTypes.h"

#include <array>

namespace Antivirus
{
    bool AvScanResult::IsDetected() const
    {
        return Status == AvScanStatus::Detected;
    }

    uint64_t BuildSignaturePrefix(const uint8_t* bytes, size_t byteCount)
    {
        if (bytes == nullptr || byteCount < 8)
        {
            return 0;
        }

        uint64_t prefix = 0;
        for (size_t index = 0; index < 8; ++index)
        {
            prefix |= static_cast<uint64_t>(bytes[index]) << (index * 8);
        }

        return prefix;
    }

    uint64_t CalculateFnv1a64(const uint8_t* bytes, size_t byteCount)
    {
        constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
        constexpr uint64_t kFnvPrime = 1099511628211ull;

        uint64_t hash = kFnvOffsetBasis;
        if (bytes == nullptr)
        {
            return hash;
        }

        for (size_t index = 0; index < byteCount; ++index)
        {
            hash ^= static_cast<uint64_t>(bytes[index]);
            hash *= kFnvPrime;
        }

        return hash;
    }

    std::vector<uint8_t> ToLittleEndianBytes(uint64_t value)
    {
        std::vector<uint8_t> bytes(8);
        for (size_t index = 0; index < bytes.size(); ++index)
        {
            bytes[index] = static_cast<uint8_t>((value >> (index * 8)) & 0xFF);
        }

        return bytes;
    }

    std::wstring BytesToHex(const std::vector<uint8_t>& bytes)
    {
        constexpr std::array<wchar_t, 16> kHexDigits =
        {
            L'0', L'1', L'2', L'3',
            L'4', L'5', L'6', L'7',
            L'8', L'9', L'A', L'B',
            L'C', L'D', L'E', L'F'
        };

        std::wstring text;
        text.reserve(bytes.size() * 2);
        for (const uint8_t byte : bytes)
        {
            text.push_back(kHexDigits[(byte >> 4) & 0x0F]);
            text.push_back(kHexDigits[byte & 0x0F]);
        }

        return text;
    }
}
