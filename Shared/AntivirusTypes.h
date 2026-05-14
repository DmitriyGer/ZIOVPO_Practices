#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Antivirus
{
    enum class AvObjectType
    {
        Unknown = 0,
        Pe = 1,
        ScriptText = 2
    };

    enum class AvScanStatus
    {
        Clean = 0,
        Detected = 1,
        Error = 2
    };

    enum class AvDatabaseLoadStatus
    {
        NotLoaded = 0,
        Loaded = 1
    };

    enum class AvDatabaseFileSource
    {
        None = 0,
        Main = 1,
        Backup = 2,
        Default = 3,
        Updated = 4
    };

    struct AvSignatureRecord
    {
        std::wstring RecordId;
        uint64_t ObjectSignaturePrefix = 0;
        uint32_t ObjectSignatureLength = 0;
        std::vector<uint8_t> ObjectSignature;
        uint64_t OffsetBegin = 0;
        uint64_t OffsetEnd = 0;
        AvObjectType ObjectType = AvObjectType::Unknown;
        std::vector<uint8_t> AvRecordSignature;
    };

    struct AvDatabaseInfo
    {
        std::chrono::system_clock::time_point ReleaseDateUtc = {};
        std::chrono::system_clock::time_point LastSuccessfulLoadUtc = {};
        size_t RecordCount = 0;
        AvDatabaseLoadStatus LoadStatus = AvDatabaseLoadStatus::NotLoaded;
        AvDatabaseFileSource Source = AvDatabaseFileSource::None;
        std::wstring LastUpdateStatus;
    };

    struct AvScanResult
    {
        AvScanStatus Status = AvScanStatus::Clean;
        uint64_t DetectionOffset = 0;
        AvObjectType ObjectType = AvObjectType::Unknown;
        uint32_t ObjectSignatureLength = 0;
        std::wstring RecordId;
        std::vector<uint8_t> ObjectSignature;

        // Returns true when scan result contains a detection.
        bool IsDetected() const;
    };

    // Builds the uint64 map key from the first eight signature bytes.
    uint64_t BuildSignaturePrefix(const uint8_t* bytes, size_t byteCount);

    // Calculates FNV-1a 64-bit over the full signature fragment.
    uint64_t CalculateFnv1a64(const uint8_t* bytes, size_t byteCount);

    // Encodes a uint64 value as little-endian bytes for vector comparisons.
    std::vector<uint8_t> ToLittleEndianBytes(uint64_t value);

    // Converts bytes to an uppercase hexadecimal string.
    std::wstring BytesToHex(const std::vector<uint8_t>& bytes);
}
