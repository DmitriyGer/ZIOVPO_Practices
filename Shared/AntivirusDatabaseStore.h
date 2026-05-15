#pragma once

#include "AntivirusDatabase.h"
#include "AntivirusSignatureVerifier.h"

#include <filesystem>

namespace Antivirus
{
    struct AvDatabaseStoragePaths
    {
        std::filesystem::path MainDatabasePath;
        std::filesystem::path BackupDatabasePath;
        std::filesystem::path DefaultDatabasePath;
        std::filesystem::path TemporaryDatabasePath;
        std::filesystem::path IncomingDatabasePath;
    };

    struct AvDiskSignatureRecord
    {
        std::vector<uint8_t> ObjectSignatureBytes;
        uint64_t OffsetBegin = 0;
        uint64_t OffsetEnd = 0;
        AvObjectType ObjectType = AvObjectType::Unknown;
        std::vector<uint8_t> AvRecordSignature;
    };

    struct AvDatabaseLoadResult
    {
        bool Loaded = false;
        AvDatabaseFileSource Source = AvDatabaseFileSource::None;
        size_t LoadedRecordCount = 0;
        size_t SkippedRecordCount = 0;
        std::wstring Message;
    };

    class AvDatabaseStore
    {
    public:
        // Builds the default main/backup/default/temp antivirus database paths.
        static AvDatabaseStoragePaths GetDefaultStoragePaths();

        // Creates the default EICAR database file when it is not present.
        static bool EnsureDefaultDatabase(
            const AvDatabaseStoragePaths& paths,
            const DemoHmacSha256SignatureVerifier& signer);

        // Saves records as one compact signed binary antivirus database.
        static bool SaveDatabaseFile(
            const std::filesystem::path& databasePath,
            const std::filesystem::path& temporaryPath,
            const std::vector<AvDiskSignatureRecord>& records,
            std::chrono::system_clock::time_point releaseDateUtc,
            const DemoHmacSha256SignatureVerifier& signer);

        // Loads one database file after checking manifest and per-record signatures.
        static AvDatabaseLoadResult LoadDatabaseFile(
            const std::filesystem::path& databasePath,
            InMemoryAvDatabase& database,
            const SignatureVerifier& verifier,
            AvDatabaseFileSource source);

        // Loads main, then backup, then default database into the in-memory engine.
        static AvDatabaseLoadResult LoadWithFallback(
            const AvDatabaseStoragePaths& paths,
            InMemoryAvDatabase& database,
            const SignatureVerifier& verifier,
            const DemoHmacSha256SignatureVerifier& defaultSigner);

        // Installs a prepared update file with backup and rollback protection.
        static AvDatabaseLoadResult InstallUpdateFromFile(
            const AvDatabaseStoragePaths& paths,
            const std::filesystem::path& updatePath,
            InMemoryAvDatabase& database,
            const SignatureVerifier& verifier,
            const DemoHmacSha256SignatureVerifier& defaultSigner);

        // Copies the active main database to the backup path when possible.
        static bool BackupMainDatabase(const AvDatabaseStoragePaths& paths);

        // Returns the built-in EICAR record used by the default database.
        static std::vector<AvDiskSignatureRecord> BuildDefaultRecords();
    };
}
