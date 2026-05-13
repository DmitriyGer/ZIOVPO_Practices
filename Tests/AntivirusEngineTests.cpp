#include "../Shared/AntivirusDatabase.h"
#include "../Shared/AntivirusScanner.h"
#include "../Shared/ServiceApiState.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);

    if (!ok)
    {
        return 1;
    }

    std::cout << "AntivirusEngineTests passed\n";
    return 0;
}
