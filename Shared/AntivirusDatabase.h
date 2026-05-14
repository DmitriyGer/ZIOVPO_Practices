#pragma once

#include "AntivirusTypes.h"

#include <map>
#include <string>

namespace Antivirus
{
    class InMemoryAvDatabase
    {
    public:
        using SignatureMap = std::map<uint64_t, std::vector<AvSignatureRecord>>;

        // Removes all records and resets database release metadata.
        void Clear();

        // Adds one raw signature record and stores its hash in memory.
        bool AddSignature(
            const std::vector<uint8_t>& signatureBytes,
            uint64_t offsetBegin,
            uint64_t offsetEnd,
            AvObjectType objectType,
            const std::vector<uint8_t>& avRecordSignature,
            const std::wstring& recordId = L"");

        // Loads deterministic demo signatures at service startup.
        void LoadDemoRecords();

        // Sets the release timestamp after records have been loaded from disk.
        void SetLoadedReleaseDate(std::chrono::system_clock::time_point releaseDateUtc);

        // Stores metadata about the latest successful disk load.
        void SetLoadMetadata(
            AvDatabaseFileSource source,
            std::chrono::system_clock::time_point lastSuccessfulLoadUtc,
            const std::wstring& lastUpdateStatus);

        // Returns release date and current record count.
        AvDatabaseInfo GetInfo() const;

        // Returns the in-memory prefix-indexed signature map.
        const SignatureMap& Records() const;

    private:
        SignatureMap m_records;
        std::chrono::system_clock::time_point m_releaseDateUtc = {};
        std::chrono::system_clock::time_point m_lastSuccessfulLoadUtc = {};
        size_t m_recordCount = 0;
        AvDatabaseLoadStatus m_loadStatus = AvDatabaseLoadStatus::NotLoaded;
        AvDatabaseFileSource m_source = AvDatabaseFileSource::None;
        std::wstring m_lastUpdateStatus;
    };
}
