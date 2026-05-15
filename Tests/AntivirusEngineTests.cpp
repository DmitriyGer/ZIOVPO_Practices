#include "../Shared/AntivirusDatabase.h"
#include "../Shared/AntivirusDatabaseStore.h"
#include "../Shared/AntivirusScanner.h"
#include "../Shared/ServiceApiState.h"

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
    std::vector<uint8_t> BytesFromAscii(const char* text)
    {
        std::vector<uint8_t> bytes;
        while (text != nullptr && *text != '\0')
        {
            bytes.push_back(static_cast<uint8_t>(*text));
            ++text;
        }

        return bytes;
    }

    std::filesystem::path CreateTestRoot()
    {
        const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::filesystem::path root = std::filesystem::temp_directory_path()
            / (L"TrayAvTests_" + std::to_wstring(static_cast<long long>(ticks)));
        std::filesystem::create_directories(root);
        return root;
    }

    bool WriteBytes(const std::filesystem::path& filePath, const std::vector<uint8_t>& bytes)
    {
        std::ofstream stream(filePath, std::ios::binary);
        if (!stream.is_open())
        {
            return false;
        }

        if (!bytes.empty())
        {
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }

        return stream.good();
    }

    bool CorruptByteAt(const std::filesystem::path& filePath, std::streamoff byteOffset)
    {
        std::fstream stream(filePath, std::ios::binary | std::ios::in | std::ios::out);
        if (!stream.is_open())
        {
            return false;
        }

        stream.seekg(byteOffset, std::ios::beg);
        char value = 0;
        stream.read(&value, 1);
        if (!stream.good())
        {
            return false;
        }

        value ^= 0x01;
        stream.seekp(byteOffset, std::ios::beg);
        stream.write(&value, 1);
        return stream.good();
    }

    bool ExpectFileVerdict(
        const char* name,
        const Antivirus::AvFileScanResult& result,
        Antivirus::AvScanStatus expectedStatus)
    {
        if (result.Status == expectedStatus)
        {
            return true;
        }

        std::cerr << name << " failed: unexpected file verdict\n";
        return false;
    }

    bool ExpectTrue(const char* name, bool value)
    {
        if (value)
        {
            return true;
        }

        std::cerr << name << " failed\n";
        return false;
    }

    Antivirus::AvDiskSignatureRecord MakeRecord(
        const char* signatureText,
        uint64_t offsetBegin,
        uint64_t offsetEnd,
        Antivirus::AvObjectType objectType)
    {
        Antivirus::AvDiskSignatureRecord record = {};
        record.ObjectSignatureBytes = BytesFromAscii(signatureText);
        record.OffsetBegin = offsetBegin;
        record.OffsetEnd = offsetEnd;
        record.ObjectType = objectType;
        return record;
    }
}

int main()
{
    bool ok = true;
    const std::filesystem::path root = CreateTestRoot();

    const std::vector<uint8_t> signature = BytesFromAscii("SCRIPT-DEMO-TEST-SIGNATURE");
    const std::vector<uint8_t> recordSignature = BytesFromAscii("TEST-RECORD-SIGNATURE");

    {
        Antivirus::InMemoryAvDatabase database;
        database.AddSignature(signature, 0, 4096, Antivirus::AvObjectType::ScriptText, recordSignature, L"TEST-MISSING");
        Antivirus::AntivirusScanner scanner(database);

        const std::filesystem::path cleanFile = root / L"clean.txt";
        ok = ExpectTrue("write clean file", WriteBytes(cleanFile, BytesFromAscii("clean stream without malware"))) && ok;
        ok = ExpectFileVerdict("ScanFile missing signature", scanner.ScanFile(cleanFile), Antivirus::AvScanStatus::Clean) && ok;
    }

    {
        Antivirus::InMemoryAvDatabase database;
        database.AddSignature(signature, 0, 4096, Antivirus::AvObjectType::Pe, recordSignature, L"TEST-TYPE");
        Antivirus::AntivirusScanner scanner(database);

        const std::filesystem::path mismatchFile = root / L"type-mismatch.txt";
        ok = ExpectTrue("write type mismatch file", WriteBytes(mismatchFile, signature)) && ok;
        ok = ExpectFileVerdict("ScanFile object type mismatch", scanner.ScanFile(mismatchFile), Antivirus::AvScanStatus::Clean) && ok;
    }

    {
        Antivirus::InMemoryAvDatabase database;
        database.AddSignature(signature, 8, 4096, Antivirus::AvObjectType::ScriptText, recordSignature, L"TEST-OFFSET");
        Antivirus::AntivirusScanner scanner(database);

        const std::filesystem::path offsetFile = root / L"offset-mismatch.txt";
        ok = ExpectTrue("write offset mismatch file", WriteBytes(offsetFile, signature)) && ok;
        ok = ExpectFileVerdict("ScanFile offset mismatch", scanner.ScanFile(offsetFile), Antivirus::AvScanStatus::Clean) && ok;
    }

    {
        Antivirus::InMemoryAvDatabase database;
        database.AddSignature(signature, 3, 3, Antivirus::AvObjectType::ScriptText, recordSignature, L"TEST-INFECTED");
        Antivirus::AntivirusScanner scanner(database);

        std::vector<uint8_t> bytes = BytesFromAscii("ABC");
        bytes.insert(bytes.end(), signature.begin(), signature.end());

        const std::filesystem::path infectedFile = root / L"infected.txt";
        ok = ExpectTrue("write infected file", WriteBytes(infectedFile, bytes)) && ok;

        const Antivirus::AvFileScanResult scanResult = scanner.ScanFile(infectedFile);
        ok = ExpectFileVerdict("ScanFile infected", scanResult, Antivirus::AvScanStatus::Detected) && ok;
        ok = ExpectTrue("ScanFile record id", scanResult.RecordId == L"TEST-INFECTED") && ok;
        ok = ExpectTrue("ScanFile signature hash", !scanResult.ObjectSignatureHex.empty()) && ok;
    }

    {
        Antivirus::InMemoryAvDatabase database;
        database.AddSignature(signature, 0, 0, Antivirus::AvObjectType::ScriptText, recordSignature, L"TEST-DIRECTORY");
        Antivirus::AntivirusScanner scanner(database);

        const std::filesystem::path directoryRoot = root / L"directory";
        std::filesystem::create_directories(directoryRoot / L"nested");
        ok = ExpectTrue("write directory clean file", WriteBytes(directoryRoot / L"clean.txt", BytesFromAscii("clean"))) && ok;
        ok = ExpectTrue("write directory infected file", WriteBytes(directoryRoot / L"bad.txt", signature)) && ok;
        ok = ExpectTrue("write directory nested file", WriteBytes(directoryRoot / L"nested" / L"nested.txt", BytesFromAscii("nested clean"))) && ok;

        const Antivirus::AvDirectoryScanResult scanResult = scanner.ScanDirectory(directoryRoot);
        ok = ExpectTrue("ScanDirectory total", scanResult.TotalScanned == 3) && ok;
        ok = ExpectTrue("ScanDirectory infected count", scanResult.InfectedCount == 1) && ok;
        ok = ExpectTrue("ScanDirectory error count", scanResult.ErrorCount == 0) && ok;
        ok = ExpectTrue("ScanDirectory result list", scanResult.Results.size() == 3) && ok;
    }

    {
        Antivirus::InMemoryAvDatabase database;
        database.LoadDemoRecords();
        Antivirus::AntivirusScanner scanner(database);

        const Antivirus::AvDatabaseInfo info = scanner.GetDatabaseInfo();
        ok = ExpectTrue("GetAvDatabaseInfo record count", info.RecordCount > 0) && ok;
        ok = ExpectTrue(
            "GetAvDatabaseInfo release date",
            info.ReleaseDateUtc != std::chrono::system_clock::time_point{}) && ok;
        ok = ExpectTrue(
            "GetAvDatabaseInfo load status",
            info.LoadStatus == Antivirus::AvDatabaseLoadStatus::Loaded) && ok;
    }

    {
        const Antivirus::DemoHmacSha256SignatureVerifier signer;
        const std::filesystem::path databaseRoot = root / L"database-store";
        SetEnvironmentVariableW(L"TRAY_AV_DATABASE_DIR", databaseRoot.c_str());
        const Antivirus::AvDatabaseStoragePaths paths = Antivirus::AvDatabaseStore::GetDefaultStoragePaths();

        const std::vector<Antivirus::AvDiskSignatureRecord> records =
        {
            MakeRecord(
                "X5O!P%@AP[4\\PZX54(P^)7CC)7}$EICAR-STANDARD-ANTIVIRUS-TEST-FILE!$H+H*",
                0,
                (std::numeric_limits<uint64_t>::max)(),
                Antivirus::AvObjectType::ScriptText)
        };

        ok = ExpectTrue(
            "SaveDatabaseFile binary",
            Antivirus::AvDatabaseStore::SaveDatabaseFile(
                paths.MainDatabasePath,
                paths.TemporaryDatabasePath,
                records,
                std::chrono::system_clock::now(),
                signer)) && ok;
        ok = ExpectTrue("main database exists", std::filesystem::exists(paths.MainDatabasePath)) && ok;

        ok = ExpectTrue(
            "EnsureDefaultDatabase creates file",
            Antivirus::AvDatabaseStore::EnsureDefaultDatabase(paths, signer)) && ok;
        ok = ExpectTrue("default database exists", std::filesystem::exists(paths.DefaultDatabasePath)) && ok;

        Antivirus::InMemoryAvDatabase database;
        const Antivirus::AvDatabaseLoadResult loadResult =
            Antivirus::AvDatabaseStore::LoadWithFallback(paths, database, signer, signer);
        ok = ExpectTrue("LoadWithFallback main source", loadResult.Source == Antivirus::AvDatabaseFileSource::Main) && ok;
        ok = ExpectTrue("LoadWithFallback main loaded", loadResult.Loaded && loadResult.LoadedRecordCount == 1) && ok;

        Antivirus::AntivirusScanner scanner(database);
        const std::filesystem::path eicarFile = root / L"eicar.txt";
        const std::filesystem::path cleanFile = root / L"clean-eicar-control.txt";
        ok = ExpectTrue("write EICAR file", WriteBytes(eicarFile, records[0].ObjectSignatureBytes)) && ok;
        ok = ExpectTrue("write non-EICAR file", WriteBytes(cleanFile, BytesFromAscii("plain harmless content"))) && ok;
        ok = ExpectFileVerdict("EICAR detected", scanner.ScanFile(eicarFile), Antivirus::AvScanStatus::Detected) && ok;
        ok = ExpectFileVerdict("non-EICAR clean", scanner.ScanFile(cleanFile), Antivirus::AvScanStatus::Clean) && ok;

        ServiceApiState& serviceState = ServiceApiState::Instance();
        serviceState.Initialize();

        TrayRpcAvDatabaseInfo rpcInfo = {};
        ok = ExpectTrue(
            "Service startup loads disk database",
            serviceState.GetAvDatabaseInfo(&rpcInfo) == TRAY_RPC_OK
            && rpcInfo.recordCount == 1
            && rpcInfo.loadStatus == TRAY_RPC_AV_DATABASE_LOADED) && ok;

        serviceState.Shutdown();
    }

    {
        const Antivirus::DemoHmacSha256SignatureVerifier signer;
        const std::filesystem::path fallbackRoot = root / L"database-fallback";
        const Antivirus::AvDatabaseStoragePaths paths =
        {
            fallbackRoot / L"main.avdb",
            fallbackRoot / L"backup.avdb",
            fallbackRoot / L"default.avdb",
            fallbackRoot / L"write.tmp",
            fallbackRoot / L"incoming.avdb"
        };

        const std::vector<Antivirus::AvDiskSignatureRecord> records =
        {
            MakeRecord("PRIMARY-SIGNATURE", 0, 4096, Antivirus::AvObjectType::ScriptText)
        };
        const std::vector<Antivirus::AvDiskSignatureRecord> backupRecords =
        {
            MakeRecord("BACKUP-SIGNATURE", 0, 4096, Antivirus::AvObjectType::ScriptText)
        };

        ok = ExpectTrue(
            "write main fallback database",
            Antivirus::AvDatabaseStore::SaveDatabaseFile(
                paths.MainDatabasePath,
                paths.TemporaryDatabasePath,
                records,
                std::chrono::system_clock::now(),
                signer)) && ok;
        ok = ExpectTrue(
            "write backup fallback database",
            Antivirus::AvDatabaseStore::SaveDatabaseFile(
                paths.BackupDatabasePath,
                paths.TemporaryDatabasePath,
                backupRecords,
                std::chrono::system_clock::now(),
                signer)) && ok;
        ok = ExpectTrue("corrupt main manifest signature", CorruptByteAt(paths.MainDatabasePath, 60)) && ok;

        Antivirus::InMemoryAvDatabase database;
        Antivirus::AvDatabaseLoadResult loadResult =
            Antivirus::AvDatabaseStore::LoadWithFallback(paths, database, signer, signer);
        ok = ExpectTrue("bad manifest falls back to backup", loadResult.Source == Antivirus::AvDatabaseFileSource::Backup) && ok;
        ok = ExpectTrue("backup loaded", loadResult.Loaded && loadResult.LoadedRecordCount == 1) && ok;

        std::filesystem::remove(paths.BackupDatabasePath);
        loadResult = Antivirus::AvDatabaseStore::LoadWithFallback(paths, database, signer, signer);
        ok = ExpectTrue("bad main and missing backup fall back to default", loadResult.Source == Antivirus::AvDatabaseFileSource::Default) && ok;
        ok = ExpectTrue("default loaded", loadResult.Loaded && loadResult.LoadedRecordCount == 1) && ok;
    }

    {
        const Antivirus::DemoHmacSha256SignatureVerifier signer;
        const std::filesystem::path badRecordRoot = root / L"database-bad-record";
        const Antivirus::AvDatabaseStoragePaths paths =
        {
            badRecordRoot / L"main.avdb",
            badRecordRoot / L"backup.avdb",
            badRecordRoot / L"default.avdb",
            badRecordRoot / L"write.tmp",
            badRecordRoot / L"incoming.avdb"
        };

        Antivirus::AvDiskSignatureRecord badRecord =
            MakeRecord("BAD-RECORD-SIGNATURE", 0, 4096, Antivirus::AvObjectType::ScriptText);
        badRecord.AvRecordSignature = BytesFromAscii("not-a-valid-record-signature");

        const std::vector<Antivirus::AvDiskSignatureRecord> records =
        {
            MakeRecord(
                "X5O!P%@AP[4\\PZX54(P^)7CC)7}$EICAR-STANDARD-ANTIVIRUS-TEST-FILE!$H+H*",
                0,
                (std::numeric_limits<uint64_t>::max)(),
                Antivirus::AvObjectType::ScriptText),
            badRecord
        };

        ok = ExpectTrue(
            "write database with bad record signature",
            Antivirus::AvDatabaseStore::SaveDatabaseFile(
                paths.MainDatabasePath,
                paths.TemporaryDatabasePath,
                records,
                std::chrono::system_clock::now(),
                signer)) && ok;

        Antivirus::InMemoryAvDatabase database;
        const Antivirus::AvDatabaseLoadResult loadResult =
            Antivirus::AvDatabaseStore::LoadDatabaseFile(
                paths.MainDatabasePath,
                database,
                signer,
                Antivirus::AvDatabaseFileSource::Main);
        ok = ExpectTrue("bad record skipped", loadResult.Loaded && loadResult.LoadedRecordCount == 1) && ok;
        ok = ExpectTrue("bad record skip count", loadResult.SkippedRecordCount == 1) && ok;
    }

    {
        const Antivirus::DemoHmacSha256SignatureVerifier signer;
        const std::filesystem::path updateRoot = root / L"database-update";
        const Antivirus::AvDatabaseStoragePaths paths =
        {
            updateRoot / L"main.avdb",
            updateRoot / L"backup.avdb",
            updateRoot / L"default.avdb",
            updateRoot / L"write.tmp",
            updateRoot / L"incoming.avdb"
        };

        const std::vector<Antivirus::AvDiskSignatureRecord> oldRecords =
        {
            MakeRecord("OLD-UPDATE-SIGNATURE", 0, 4096, Antivirus::AvObjectType::ScriptText)
        };
        const std::vector<Antivirus::AvDiskSignatureRecord> newRecords =
        {
            MakeRecord("NEW-UPDATE-SIGNATURE", 0, 4096, Antivirus::AvObjectType::ScriptText)
        };

        ok = ExpectTrue(
            "write old main database",
            Antivirus::AvDatabaseStore::SaveDatabaseFile(
                paths.MainDatabasePath,
                paths.TemporaryDatabasePath,
                oldRecords,
                std::chrono::system_clock::now(),
                signer)) && ok;
        ok = ExpectTrue(
            "write incoming update database",
            Antivirus::AvDatabaseStore::SaveDatabaseFile(
                paths.IncomingDatabasePath,
                paths.TemporaryDatabasePath,
                newRecords,
                std::chrono::system_clock::now(),
                signer)) && ok;

        Antivirus::InMemoryAvDatabase updatedDatabase;
        Antivirus::AvDatabaseLoadResult updateResult =
            Antivirus::AvDatabaseStore::InstallUpdateFromFile(
                paths,
                paths.IncomingDatabasePath,
                updatedDatabase,
                signer,
                signer);
        ok = ExpectTrue("update installed", updateResult.Loaded && updateResult.Source == Antivirus::AvDatabaseFileSource::Updated) && ok;
        ok = ExpectTrue("backup created before update", std::filesystem::exists(paths.BackupDatabasePath)) && ok;

        Antivirus::AntivirusScanner scanner(updatedDatabase);
        const std::filesystem::path newFile = root / L"new-update.txt";
        const std::filesystem::path oldFile = root / L"old-update.txt";
        ok = ExpectTrue("write new update file", WriteBytes(newFile, newRecords[0].ObjectSignatureBytes)) && ok;
        ok = ExpectTrue("write old update file", WriteBytes(oldFile, oldRecords[0].ObjectSignatureBytes)) && ok;
        ok = ExpectFileVerdict("new update detected", scanner.ScanFile(newFile), Antivirus::AvScanStatus::Detected) && ok;
        ok = ExpectFileVerdict("old update no longer detected", scanner.ScanFile(oldFile), Antivirus::AvScanStatus::Clean) && ok;
    }

    {
        const Antivirus::DemoHmacSha256SignatureVerifier signer;
        const std::filesystem::path rollbackRoot = root / L"database-rollback";
        const Antivirus::AvDatabaseStoragePaths paths =
        {
            rollbackRoot / L"main.avdb",
            rollbackRoot / L"backup.avdb",
            rollbackRoot / L"default.avdb",
            rollbackRoot / L"write.tmp",
            rollbackRoot / L"incoming.avdb"
        };

        const std::vector<Antivirus::AvDiskSignatureRecord> stableRecords =
        {
            MakeRecord("ROLLBACK-STABLE-SIGNATURE", 0, 4096, Antivirus::AvObjectType::ScriptText)
        };
        const std::vector<Antivirus::AvDiskSignatureRecord> badUpdateRecords =
        {
            MakeRecord("ROLLBACK-BAD-SIGNATURE", 0, 4096, Antivirus::AvObjectType::ScriptText)
        };

        ok = ExpectTrue(
            "write rollback main database",
            Antivirus::AvDatabaseStore::SaveDatabaseFile(
                paths.MainDatabasePath,
                paths.TemporaryDatabasePath,
                stableRecords,
                std::chrono::system_clock::now(),
                signer)) && ok;
        ok = ExpectTrue(
            "write rollback incoming database",
            Antivirus::AvDatabaseStore::SaveDatabaseFile(
                paths.IncomingDatabasePath,
                paths.TemporaryDatabasePath,
                badUpdateRecords,
                std::chrono::system_clock::now(),
                signer)) && ok;
        ok = ExpectTrue("damage rollback incoming manifest", CorruptByteAt(paths.IncomingDatabasePath, 60)) && ok;

        Antivirus::InMemoryAvDatabase rolledBackDatabase;
        const Antivirus::AvDatabaseLoadResult rollbackResult =
            Antivirus::AvDatabaseStore::InstallUpdateFromFile(
                paths,
                paths.IncomingDatabasePath,
                rolledBackDatabase,
                signer,
                signer);
        ok = ExpectTrue(
            "bad update rolls back to backup",
            rollbackResult.Loaded && rollbackResult.Source == Antivirus::AvDatabaseFileSource::Backup) && ok;

        Antivirus::AntivirusScanner scanner(rolledBackDatabase);
        const std::filesystem::path stableFile = root / L"rollback-stable.txt";
        const std::filesystem::path badUpdateFile = root / L"rollback-bad.txt";
        ok = ExpectTrue("write stable rollback file", WriteBytes(stableFile, stableRecords[0].ObjectSignatureBytes)) && ok;
        ok = ExpectTrue("write bad rollback file", WriteBytes(badUpdateFile, badUpdateRecords[0].ObjectSignatureBytes)) && ok;
        ok = ExpectFileVerdict("rollback stable detected", scanner.ScanFile(stableFile), Antivirus::AvScanStatus::Detected) && ok;
        ok = ExpectFileVerdict("rollback bad update clean", scanner.ScanFile(badUpdateFile), Antivirus::AvScanStatus::Clean) && ok;
    }

    {
        const std::vector<std::filesystem::path> fixedDrives = Antivirus::AntivirusScanner::ListFixedDriveRoots();
        bool onlyAbsoluteRoots = true;
        for (const std::filesystem::path& driveRoot : fixedDrives)
        {
            onlyAbsoluteRoots = onlyAbsoluteRoots && driveRoot.is_absolute();
        }
        ok = ExpectTrue("fixed drive roots are enumerable", onlyAbsoluteRoots) && ok;
    }

    {
        ServiceApiState& serviceState = ServiceApiState::Instance();
        serviceState.Initialize();

        TrayRpcAvDatabaseInfo rpcInfo = {};
        ok = ExpectTrue(
            "Service GetAvDatabaseInfo status",
            serviceState.GetAvDatabaseInfo(&rpcInfo) == TRAY_RPC_OK) && ok;
        ok = ExpectTrue("Service GetAvDatabaseInfo record count", rpcInfo.recordCount > 0) && ok;
        ok = ExpectTrue("Service GetAvDatabaseInfo release date", rpcInfo.hasReleaseDate != 0) && ok;
        ok = ExpectTrue(
            "Service GetAvDatabaseInfo load status",
            rpcInfo.loadStatus == TRAY_RPC_AV_DATABASE_LOADED) && ok;

        serviceState.Shutdown();
    }

    SetEnvironmentVariableW(L"TRAY_AV_DATABASE_DIR", nullptr);

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);

    if (!ok)
    {
        return 1;
    }

    std::cout << "AntivirusEngineTests passed\n";
    return 0;
}
