#include "AntivirusDatabaseStore.h"

#include <windows.h>

#include <array>
#include <fstream>
#include <limits>

namespace
{
    constexpr std::array<uint8_t, 8> kDatabaseMagic =
    {
        'T', 'A', 'V', 'D', 'B', '0', '0', '1'
    };
    constexpr uint16_t kDatabaseVersion = 1;
    constexpr uint8_t kHashAlgorithmSha256 = 1;
    constexpr uint8_t kSignatureAlgorithmDemoHmacSha256 = 1;
    constexpr size_t kSha256Length = 32;

    void AppendUInt8(std::vector<uint8_t>& bytes, uint8_t value)
    {
        bytes.push_back(value);
    }

    void AppendUInt16(std::vector<uint8_t>& bytes, uint16_t value)
    {
        bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        bytes.push_back(static_cast<uint8_t>(value & 0xFF));
    }

    void AppendUInt32(std::vector<uint8_t>& bytes, uint32_t value)
    {
        bytes.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
        bytes.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        bytes.push_back(static_cast<uint8_t>(value & 0xFF));
    }

    void AppendUInt64(std::vector<uint8_t>& bytes, uint64_t value)
    {
        for (int shift = 56; shift >= 0; shift -= 8)
        {
            bytes.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
        }
    }

    bool ReadUInt8(const std::vector<uint8_t>& bytes, size_t& offset, uint8_t& value)
    {
        if (offset + 1 > bytes.size())
        {
            return false;
        }

        value = bytes[offset++];
        return true;
    }

    bool ReadUInt16(const std::vector<uint8_t>& bytes, size_t& offset, uint16_t& value)
    {
        if (offset + 2 > bytes.size())
        {
            return false;
        }

        value = static_cast<uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
        offset += 2;
        return true;
    }

    bool ReadUInt32(const std::vector<uint8_t>& bytes, size_t& offset, uint32_t& value)
    {
        if (offset + 4 > bytes.size())
        {
            return false;
        }

        value = (static_cast<uint32_t>(bytes[offset]) << 24)
            | (static_cast<uint32_t>(bytes[offset + 1]) << 16)
            | (static_cast<uint32_t>(bytes[offset + 2]) << 8)
            | static_cast<uint32_t>(bytes[offset + 3]);
        offset += 4;
        return true;
    }

    bool ReadUInt64(const std::vector<uint8_t>& bytes, size_t& offset, uint64_t& value)
    {
        if (offset + 8 > bytes.size())
        {
            return false;
        }

        value = 0;
        for (size_t index = 0; index < 8; ++index)
        {
            value = (value << 8) | static_cast<uint64_t>(bytes[offset + index]);
        }

        offset += 8;
        return true;
    }

    bool ReadBytes(const std::vector<uint8_t>& bytes, size_t& offset, size_t length, std::vector<uint8_t>& value)
    {
        if (offset + length > bytes.size())
        {
            return false;
        }

        value.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;
        return true;
    }

    std::vector<uint8_t> BuildRecordPayload(const Antivirus::AvDiskSignatureRecord& record)
    {
        std::vector<uint8_t> bytes;
        bytes.reserve(8 + 4 + record.ObjectSignatureBytes.size() + 8 + 8 + 1);

        AppendUInt64(
            bytes,
            Antivirus::BuildSignaturePrefix(record.ObjectSignatureBytes.data(), record.ObjectSignatureBytes.size()));
        AppendUInt32(bytes, static_cast<uint32_t>(record.ObjectSignatureBytes.size()));
        bytes.insert(bytes.end(), record.ObjectSignatureBytes.begin(), record.ObjectSignatureBytes.end());
        AppendUInt64(bytes, record.OffsetBegin);
        AppendUInt64(bytes, record.OffsetEnd);
        AppendUInt8(bytes, static_cast<uint8_t>(record.ObjectType));
        return bytes;
    }

    std::vector<uint8_t> BuildRecordFrame(
        const Antivirus::AvDiskSignatureRecord& record,
        const Antivirus::DemoHmacSha256SignatureVerifier& signer)
    {
        std::vector<uint8_t> bytes = BuildRecordPayload(record);
        std::vector<uint8_t> signature = record.AvRecordSignature;
        if (signature.empty())
        {
            signature = signer.SignForDemo(bytes);
        }

        AppendUInt32(bytes, static_cast<uint32_t>(signature.size()));
        bytes.insert(bytes.end(), signature.begin(), signature.end());
        return bytes;
    }

    bool ReadFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& bytes)
    {
        bytes.clear();
        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open())
        {
            return false;
        }

        stream.seekg(0, std::ios::end);
        const std::streamoff length = stream.tellg();
        if (length < 0)
        {
            return false;
        }

        stream.seekg(0, std::ios::beg);
        bytes.resize(static_cast<size_t>(length));
        if (!bytes.empty())
        {
            stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }

        return stream.good() || stream.eof();
    }

    bool WriteFileBytesAtomically(
        const std::filesystem::path& databasePath,
        const std::filesystem::path& temporaryPath,
        const std::vector<uint8_t>& bytes)
    {
        std::error_code error;
        std::filesystem::create_directories(databasePath.parent_path(), error);
        if (error)
        {
            return false;
        }

        {
            std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
            if (!stream.is_open())
            {
                return false;
            }

            if (!bytes.empty())
            {
                stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            }

            if (!stream.good())
            {
                return false;
            }
        }

        std::filesystem::remove(databasePath, error);
        error.clear();
        std::filesystem::rename(temporaryPath, databasePath, error);
        if (error)
        {
            std::filesystem::remove(temporaryPath, error);
            return false;
        }

        return true;
    }

    bool CopyFileReplacing(const std::filesystem::path& sourcePath, const std::filesystem::path& destinationPath)
    {
        std::error_code error;
        std::filesystem::create_directories(destinationPath.parent_path(), error);
        if (error)
        {
            return false;
        }

        error.clear();
        std::filesystem::copy_file(
            sourcePath,
            destinationPath,
            std::filesystem::copy_options::overwrite_existing,
            error);
        return !error;
    }

    std::filesystem::path GetDatabaseDirectory()
    {
        wchar_t overrideDirectory[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"TRAY_AV_DATABASE_DIR", overrideDirectory, ARRAYSIZE(overrideDirectory)) > 0)
        {
            return std::filesystem::path(overrideDirectory);
        }

        wchar_t programData[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"ProgramData", programData, ARRAYSIZE(programData)) > 0)
        {
            return std::filesystem::path(programData) / L"TrayApp" / L"Antivirus";
        }

        return std::filesystem::current_path() / L"Antivirus";
    }

    std::chrono::system_clock::time_point EpochMillisToTimePoint(uint64_t epochMillis)
    {
        return std::chrono::system_clock::time_point{ std::chrono::milliseconds(epochMillis) };
    }

    uint64_t TimePointToEpochMillis(std::chrono::system_clock::time_point value)
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(value.time_since_epoch()).count());
    }

    Antivirus::AvObjectType ReadObjectType(uint8_t value)
    {
        switch (value)
        {
        case static_cast<uint8_t>(Antivirus::AvObjectType::Pe):
            return Antivirus::AvObjectType::Pe;
        case static_cast<uint8_t>(Antivirus::AvObjectType::ScriptText):
            return Antivirus::AvObjectType::ScriptText;
        default:
            return Antivirus::AvObjectType::Unknown;
        }
    }
}

namespace Antivirus
{
    AvDatabaseStoragePaths AvDatabaseStore::GetDefaultStoragePaths()
    {
        const std::filesystem::path directory = GetDatabaseDirectory();
        AvDatabaseStoragePaths paths = {};
        paths.MainDatabasePath = directory / L"main.avdb";
        paths.BackupDatabasePath = directory / L"backup.avdb";
        paths.DefaultDatabasePath = directory / L"default.avdb";
        paths.TemporaryDatabasePath = directory / L"write.tmp";
        paths.IncomingDatabasePath = directory / L"incoming.avdb";
        return paths;
    }

    bool AvDatabaseStore::EnsureDefaultDatabase(
        const AvDatabaseStoragePaths& paths,
        const DemoHmacSha256SignatureVerifier& signer)
    {
        std::error_code error;
        if (std::filesystem::exists(paths.DefaultDatabasePath, error))
        {
            return true;
        }

        return SaveDatabaseFile(
            paths.DefaultDatabasePath,
            paths.TemporaryDatabasePath,
            BuildDefaultRecords(),
            std::chrono::system_clock::now(),
            signer);
    }

    bool AvDatabaseStore::SaveDatabaseFile(
        const std::filesystem::path& databasePath,
        const std::filesystem::path& temporaryPath,
        const std::vector<AvDiskSignatureRecord>& records,
        std::chrono::system_clock::time_point releaseDateUtc,
        const DemoHmacSha256SignatureVerifier& signer)
    {
        if (records.size() > (std::numeric_limits<uint32_t>::max)())
        {
            return false;
        }

        std::vector<uint8_t> recordsBytes;
        for (const AvDiskSignatureRecord& record : records)
        {
            if (record.ObjectSignatureBytes.size() < 8
                || record.ObjectSignatureBytes.size() > (std::numeric_limits<uint32_t>::max)()
                || record.OffsetBegin > record.OffsetEnd
                || record.ObjectType == AvObjectType::Unknown)
            {
                return false;
            }

            const std::vector<uint8_t> frame = BuildRecordFrame(record, signer);
            recordsBytes.insert(recordsBytes.end(), frame.begin(), frame.end());
        }

        std::vector<uint8_t> recordsHash;
        if (!CalculateSha256(recordsBytes, recordsHash) || recordsHash.size() != kSha256Length)
        {
            return false;
        }

        std::vector<uint8_t> manifest;
        manifest.insert(manifest.end(), kDatabaseMagic.begin(), kDatabaseMagic.end());
        AppendUInt16(manifest, kDatabaseVersion);
        AppendUInt64(manifest, TimePointToEpochMillis(releaseDateUtc));
        AppendUInt32(manifest, static_cast<uint32_t>(records.size()));
        AppendUInt8(manifest, kHashAlgorithmSha256);
        AppendUInt8(manifest, kSignatureAlgorithmDemoHmacSha256);
        manifest.insert(manifest.end(), recordsHash.begin(), recordsHash.end());

        const std::vector<uint8_t> manifestSignature = signer.SignForDemo(manifest);
        if (manifestSignature.empty() || manifestSignature.size() > (std::numeric_limits<uint32_t>::max)())
        {
            return false;
        }

        std::vector<uint8_t> fileBytes = manifest;
        AppendUInt32(fileBytes, static_cast<uint32_t>(manifestSignature.size()));
        fileBytes.insert(fileBytes.end(), manifestSignature.begin(), manifestSignature.end());
        fileBytes.insert(fileBytes.end(), recordsBytes.begin(), recordsBytes.end());

        return WriteFileBytesAtomically(databasePath, temporaryPath, fileBytes);
    }

    AvDatabaseLoadResult AvDatabaseStore::LoadDatabaseFile(
        const std::filesystem::path& databasePath,
        InMemoryAvDatabase& database,
        const SignatureVerifier& verifier,
        AvDatabaseFileSource source)
    {
        AvDatabaseLoadResult result = {};
        result.Source = source;

        std::vector<uint8_t> fileBytes;
        if (!ReadFileBytes(databasePath, fileBytes))
        {
            result.Message = L"Database file is not readable.";
            return result;
        }

        size_t offset = 0;
        if (fileBytes.size() < kDatabaseMagic.size())
        {
            result.Message = L"Database file is too small.";
            return result;
        }

        if (!std::equal(kDatabaseMagic.begin(), kDatabaseMagic.end(), fileBytes.begin()))
        {
            result.Message = L"Database magic is invalid.";
            return result;
        }
        offset += kDatabaseMagic.size();

        uint16_t version = 0;
        uint64_t releaseEpochMillis = 0;
        uint32_t recordCount = 0;
        uint8_t hashAlgorithm = 0;
        uint8_t signatureAlgorithm = 0;
        std::vector<uint8_t> expectedRecordsHash;
        uint32_t manifestSignatureLength = 0;

        if (!ReadUInt16(fileBytes, offset, version)
            || !ReadUInt64(fileBytes, offset, releaseEpochMillis)
            || !ReadUInt32(fileBytes, offset, recordCount)
            || !ReadUInt8(fileBytes, offset, hashAlgorithm)
            || !ReadUInt8(fileBytes, offset, signatureAlgorithm)
            || !ReadBytes(fileBytes, offset, kSha256Length, expectedRecordsHash)
            || !ReadUInt32(fileBytes, offset, manifestSignatureLength))
        {
            result.Message = L"Database manifest is truncated.";
            return result;
        }

        if (version != kDatabaseVersion
            || hashAlgorithm != kHashAlgorithmSha256
            || signatureAlgorithm != kSignatureAlgorithmDemoHmacSha256)
        {
            result.Message = L"Database manifest version or algorithms are unsupported.";
            return result;
        }

        const size_t manifestSignatureOffset = offset;
        std::vector<uint8_t> manifestSignature;
        if (!ReadBytes(fileBytes, offset, manifestSignatureLength, manifestSignature))
        {
            result.Message = L"Database manifest signature is truncated.";
            return result;
        }

        const std::vector<uint8_t> unsignedManifest(
            fileBytes.begin(),
            fileBytes.begin() + static_cast<std::ptrdiff_t>(manifestSignatureOffset - sizeof(uint32_t)));
        if (!verifier.Verify(unsignedManifest, manifestSignature))
        {
            result.Message = L"Database manifest signature is invalid.";
            return result;
        }

        std::vector<uint8_t> recordsBytes(
            fileBytes.begin() + static_cast<std::ptrdiff_t>(offset),
            fileBytes.end());
        std::vector<uint8_t> actualRecordsHash;
        if (!CalculateSha256(recordsBytes, actualRecordsHash) || actualRecordsHash != expectedRecordsHash)
        {
            result.Message = L"Database records checksum is invalid.";
            return result;
        }

        InMemoryAvDatabase loadedDatabase;
        size_t recordsOffset = 0;
        for (uint32_t index = 0; index < recordCount; ++index)
        {
            const size_t recordStart = recordsOffset;
            uint64_t storedPrefix = 0;
            uint32_t signatureLength = 0;
            std::vector<uint8_t> signatureBytes;
            uint64_t offsetBegin = 0;
            uint64_t offsetEnd = 0;
            uint8_t objectTypeCode = 0;
            uint32_t recordSignatureLength = 0;
            std::vector<uint8_t> recordSignature;

            const bool recordFrameReadable =
                ReadUInt64(recordsBytes, recordsOffset, storedPrefix)
                && ReadUInt32(recordsBytes, recordsOffset, signatureLength)
                && ReadBytes(recordsBytes, recordsOffset, signatureLength, signatureBytes)
                && ReadUInt64(recordsBytes, recordsOffset, offsetBegin)
                && ReadUInt64(recordsBytes, recordsOffset, offsetEnd)
                && ReadUInt8(recordsBytes, recordsOffset, objectTypeCode)
                && ReadUInt32(recordsBytes, recordsOffset, recordSignatureLength)
                && ReadBytes(recordsBytes, recordsOffset, recordSignatureLength, recordSignature);
            if (!recordFrameReadable)
            {
                result.Message = L"Database record frame is truncated.";
                return result;
            }

            const size_t recordPayloadEnd = recordsOffset - sizeof(uint32_t) - recordSignature.size();
            std::vector<uint8_t> recordPayload(
                recordsBytes.begin() + static_cast<std::ptrdiff_t>(recordStart),
                recordsBytes.begin() + static_cast<std::ptrdiff_t>(recordPayloadEnd));

            if (!verifier.Verify(recordPayload, recordSignature))
            {
                OutputDebugStringW(
                    L"[TrayService][AV] Record signature is invalid; record skipped."
                    L" TODO: request this record from update server when a compatible endpoint exists.\r\n");
                ++result.SkippedRecordCount;
                continue;
            }

            const AvObjectType objectType = ReadObjectType(objectTypeCode);
            if (storedPrefix != BuildSignaturePrefix(signatureBytes.data(), signatureBytes.size())
                || !loadedDatabase.AddSignature(signatureBytes, offsetBegin, offsetEnd, objectType, recordSignature))
            {
                ++result.SkippedRecordCount;
                continue;
            }

            ++result.LoadedRecordCount;
        }

        if (recordsOffset != recordsBytes.size())
        {
            result.Message = L"Database contains unexpected trailing bytes.";
            return result;
        }

        loadedDatabase.SetLoadedReleaseDate(EpochMillisToTimePoint(releaseEpochMillis));
        loadedDatabase.SetLoadMetadata(source, std::chrono::system_clock::now(), L"Database loaded.");
        database = loadedDatabase;
        result.Loaded = result.LoadedRecordCount > 0;
        result.Message = result.Loaded ? L"Database loaded." : L"Database has no valid records.";
        return result;
    }

    AvDatabaseLoadResult AvDatabaseStore::LoadWithFallback(
        const AvDatabaseStoragePaths& paths,
        InMemoryAvDatabase& database,
        const SignatureVerifier& verifier,
        const DemoHmacSha256SignatureVerifier& defaultSigner)
    {
        EnsureDefaultDatabase(paths, defaultSigner);

        AvDatabaseLoadResult result = LoadDatabaseFile(paths.MainDatabasePath, database, verifier, AvDatabaseFileSource::Main);
        if (result.Loaded)
        {
            return result;
        }

        result = LoadDatabaseFile(paths.BackupDatabasePath, database, verifier, AvDatabaseFileSource::Backup);
        if (result.Loaded)
        {
            return result;
        }

        result = LoadDatabaseFile(paths.DefaultDatabasePath, database, verifier, AvDatabaseFileSource::Default);
        if (result.Loaded)
        {
            return result;
        }

        SaveDatabaseFile(
            paths.DefaultDatabasePath,
            paths.TemporaryDatabasePath,
            BuildDefaultRecords(),
            std::chrono::system_clock::now(),
            defaultSigner);

        result = LoadDatabaseFile(paths.DefaultDatabasePath, database, verifier, AvDatabaseFileSource::Default);
        if (result.Loaded)
        {
            return result;
        }

        database.Clear();
        return result;
    }

    AvDatabaseLoadResult AvDatabaseStore::InstallUpdateFromFile(
        const AvDatabaseStoragePaths& paths,
        const std::filesystem::path& updatePath,
        InMemoryAvDatabase& database,
        const SignatureVerifier& verifier,
        const DemoHmacSha256SignatureVerifier& defaultSigner)
    {
        AvDatabaseLoadResult result = {};

        std::error_code error;
        if (!std::filesystem::exists(updatePath, error))
        {
            result.Message = L"No pending antivirus database update file.";
            return result;
        }

        BackupMainDatabase(paths);

        std::vector<uint8_t> updateBytes;
        if (!ReadFileBytes(updatePath, updateBytes)
            || !WriteFileBytesAtomically(paths.MainDatabasePath, paths.TemporaryDatabasePath, updateBytes))
        {
            result.Message = L"Could not stage antivirus database update.";
            return LoadWithFallback(paths, database, verifier, defaultSigner);
        }

        result = LoadDatabaseFile(paths.MainDatabasePath, database, verifier, AvDatabaseFileSource::Updated);
        if (result.Loaded)
        {
            database.SetLoadMetadata(
                AvDatabaseFileSource::Updated,
                std::chrono::system_clock::now(),
                L"Antivirus database update installed.");
            std::filesystem::remove(updatePath, error);
            return result;
        }

        if (std::filesystem::exists(paths.BackupDatabasePath, error)
            && CopyFileReplacing(paths.BackupDatabasePath, paths.MainDatabasePath))
        {
            result = LoadDatabaseFile(paths.MainDatabasePath, database, verifier, AvDatabaseFileSource::Backup);
            if (result.Loaded)
            {
                database.SetLoadMetadata(
                    AvDatabaseFileSource::Backup,
                    std::chrono::system_clock::now(),
                    L"Antivirus database update failed; backup restored.");
                return result;
            }
        }

        result = LoadDatabaseFile(paths.DefaultDatabasePath, database, verifier, AvDatabaseFileSource::Default);
        if (result.Loaded)
        {
            database.SetLoadMetadata(
                AvDatabaseFileSource::Default,
                std::chrono::system_clock::now(),
                L"Antivirus database update failed; default database loaded.");
            return result;
        }

        SaveDatabaseFile(
            paths.DefaultDatabasePath,
            paths.TemporaryDatabasePath,
            BuildDefaultRecords(),
            std::chrono::system_clock::now(),
            defaultSigner);
        result = LoadDatabaseFile(paths.DefaultDatabasePath, database, verifier, AvDatabaseFileSource::Default);
        if (result.Loaded)
        {
            database.SetLoadMetadata(
                AvDatabaseFileSource::Default,
                std::chrono::system_clock::now(),
                L"Antivirus database update failed; default database recreated.");
        }

        return result;
    }

    bool AvDatabaseStore::BackupMainDatabase(const AvDatabaseStoragePaths& paths)
    {
        std::error_code error;
        if (!std::filesystem::exists(paths.MainDatabasePath, error))
        {
            return false;
        }

        return CopyFileReplacing(paths.MainDatabasePath, paths.BackupDatabasePath);
    }

    std::vector<AvDiskSignatureRecord> AvDatabaseStore::BuildDefaultRecords()
    {
        constexpr char kEicar[] =
            "X5O!P%@AP[4\\PZX54(P^)7CC)7}$EICAR-STANDARD-ANTIVIRUS-TEST-FILE!$H+H*";

        AvDiskSignatureRecord record = {};
        record.ObjectSignatureBytes.assign(std::begin(kEicar), std::end(kEicar) - 1);
        record.OffsetBegin = 0;
        record.OffsetEnd = (std::numeric_limits<uint64_t>::max)();
        record.ObjectType = AvObjectType::ScriptText;
        return { record };
    }
}
