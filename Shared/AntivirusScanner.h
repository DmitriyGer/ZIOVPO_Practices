#pragma once

#include "AntivirusDatabase.h"
#include "AntivirusTypes.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Antivirus
{
    struct AvFileScanResult
    {
        AvScanStatus Status = AvScanStatus::Clean;
        std::wstring Path;
        AvObjectType ObjectType = AvObjectType::Unknown;
        uint64_t DetectionOffset = 0;
        std::wstring RecordId;
        std::wstring ObjectSignatureHex;
        std::wstring Message;
    };

    struct AvDirectoryScanResult
    {
        std::wstring Path;
        uint64_t TotalScanned = 0;
        uint64_t InfectedCount = 0;
        uint64_t ErrorCount = 0;
        std::vector<AvFileScanResult> Results;
        std::wstring Message;
    };

    class AntivirusScanner
    {
    public:
        // Creates file and directory scanner over an in-memory database snapshot.
        explicit AntivirusScanner(const InMemoryAvDatabase& database);

        // Scans one selected file through FileByteStream and AvEngine.
        AvFileScanResult ScanFile(const std::filesystem::path& filePath) const;

        // Recursively scans files under one selected directory.
        AvDirectoryScanResult ScanDirectory(const std::filesystem::path& directoryPath) const;

        // Returns current database metadata used by this scanner.
        AvDatabaseInfo GetDatabaseInfo() const;

        // Determines a minimal object type used by the AV engine.
        static AvObjectType DetectObjectType(const std::filesystem::path& filePath, std::wstring& errorMessage);

    private:
        const InMemoryAvDatabase& m_database;
    };
}
