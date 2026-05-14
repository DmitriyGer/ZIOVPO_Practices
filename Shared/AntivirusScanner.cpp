#include "AntivirusScanner.h"

#include "AntivirusEngine.h"
#include "ByteStream.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwctype>
#include <fstream>
#include <utility>

namespace
{
    bool IsScriptOrTextExtension(const std::filesystem::path& filePath)
    {
        std::wstring extension = filePath.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t ch)
        {
            return static_cast<wchar_t>(std::towlower(ch));
        });

        return extension == L".txt"
            || extension == L".log"
            || extension == L".csv"
            || extension == L".json"
            || extension == L".xml"
            || extension == L".html"
            || extension == L".htm"
            || extension == L".js"
            || extension == L".vbs"
            || extension == L".ps1"
            || extension == L".bat"
            || extension == L".cmd";
    }

    bool IsMostlyText(const std::array<uint8_t, 256>& buffer, size_t bytesRead)
    {
        if (bytesRead == 0)
        {
            return true;
        }

        size_t printableCount = 0;
        for (size_t index = 0; index < bytesRead; ++index)
        {
            const uint8_t byte = buffer[index];
            if (byte == 0)
            {
                return false;
            }

            if (byte == 9 || byte == 10 || byte == 13 || (byte >= 32 && byte <= 126))
            {
                ++printableCount;
            }
        }

        return printableCount * 100 >= bytesRead * 85;
    }
}

namespace Antivirus
{
    AntivirusScanner::AntivirusScanner(const InMemoryAvDatabase& database)
        : m_database(database)
    {
    }

    AvFileScanResult AntivirusScanner::ScanFile(const std::filesystem::path& filePath) const
    {
        AvFileScanResult result = {};
        result.Path = filePath.wstring();

        const AvDatabaseInfo databaseInfo = m_database.GetInfo();
        if (databaseInfo.LoadStatus != AvDatabaseLoadStatus::Loaded || databaseInfo.RecordCount == 0)
        {
            result.Status = AvScanStatus::Error;
            result.Message = L"Antivirus database is not loaded.";
            return result;
        }

        std::error_code fileError;
        if (!std::filesystem::is_regular_file(filePath, fileError))
        {
            result.Status = AvScanStatus::Error;
            result.Message = fileError ? L"Could not read file metadata." : L"Path is not a regular file.";
            return result;
        }

        std::wstring typeError;
        result.ObjectType = DetectObjectType(filePath, typeError);
        if (!typeError.empty())
        {
            result.Status = AvScanStatus::Error;
            result.Message = typeError;
            return result;
        }

        FileByteStream stream(filePath);
        if (!stream.IsOpen())
        {
            result.Status = AvScanStatus::Error;
            result.Message = L"Could not open file for scanning.";
            return result;
        }

        AvEngine engine(m_database);
        const AvScanResult scanResult = engine.Scan(stream, result.ObjectType);
        result.Status = scanResult.Status;
        result.DetectionOffset = scanResult.DetectionOffset;
        result.RecordId = scanResult.RecordId;
        result.ObjectSignatureHex = BytesToHex(scanResult.ObjectSignature);

        if (scanResult.Status == AvScanStatus::Detected)
        {
            result.Message = L"Object is infected.";
        }
        else if (scanResult.Status == AvScanStatus::Error)
        {
            result.Message = L"Scan engine returned an error.";
        }
        else
        {
            result.Message = L"Object is clean.";
        }

        return result;
    }

    AvDirectoryScanResult AntivirusScanner::ScanDirectory(const std::filesystem::path& directoryPath) const
    {
        AvDirectoryScanResult result = {};
        result.Path = directoryPath.wstring();

        std::error_code directoryError;
        if (!std::filesystem::is_directory(directoryPath, directoryError))
        {
            result.Message = directoryError ? L"Could not read directory metadata." : L"Path is not a directory.";
            ++result.ErrorCount;
            return result;
        }

        std::filesystem::recursive_directory_iterator iterator(
            directoryPath,
            std::filesystem::directory_options::skip_permission_denied,
            directoryError);
        const std::filesystem::recursive_directory_iterator endIterator;
        if (directoryError)
        {
            result.Message = L"Could not open directory for recursive scanning.";
            ++result.ErrorCount;
            return result;
        }

        for (; iterator != endIterator; iterator.increment(directoryError))
        {
            if (directoryError)
            {
                AvFileScanResult errorResult = {};
                errorResult.Status = AvScanStatus::Error;
                errorResult.Message = L"Could not enumerate this directory entry.";
                result.Results.push_back(errorResult);
                ++result.TotalScanned;
                ++result.ErrorCount;
                directoryError.clear();
                continue;
            }

            std::error_code entryError;
            if (!iterator->is_regular_file(entryError))
            {
                continue;
            }

            ++result.TotalScanned;
            AvFileScanResult fileResult = ScanFile(iterator->path());
            if (fileResult.Status == AvScanStatus::Detected)
            {
                ++result.InfectedCount;
            }
            else if (fileResult.Status == AvScanStatus::Error)
            {
                ++result.ErrorCount;
            }

            result.Results.push_back(std::move(fileResult));
        }

        result.Message = L"Directory scan completed.";
        return result;
    }

    AvDirectoryScanResult AntivirusScanner::ScanFixedDrives() const
    {
        AvDirectoryScanResult result = {};
        result.Path = L"<fixed-drives>";

        const std::vector<std::filesystem::path> driveRoots = ListFixedDriveRoots();
        for (const std::filesystem::path& driveRoot : driveRoots)
        {
            AvDirectoryScanResult driveResult = ScanDirectory(driveRoot);
            result.TotalScanned += driveResult.TotalScanned;
            result.InfectedCount += driveResult.InfectedCount;
            result.ErrorCount += driveResult.ErrorCount;

            const size_t remainingCapacity = result.Results.size() < 32 ? 32 - result.Results.size() : 0;
            const size_t copyCount = (std::min)(remainingCapacity, driveResult.Results.size());
            result.Results.insert(
                result.Results.end(),
                driveResult.Results.begin(),
                driveResult.Results.begin() + static_cast<std::ptrdiff_t>(copyCount));
        }

        result.Message = L"Fixed drives scan completed.";
        return result;
    }

    AvDatabaseInfo AntivirusScanner::GetDatabaseInfo() const
    {
        return m_database.GetInfo();
    }

    std::vector<std::filesystem::path> AntivirusScanner::ListFixedDriveRoots()
    {
        std::vector<std::filesystem::path> roots;
        const DWORD driveMask = GetLogicalDrives();
        for (wchar_t letter = L'A'; letter <= L'Z'; ++letter)
        {
            const DWORD bit = 1u << (letter - L'A');
            if ((driveMask & bit) == 0)
            {
                continue;
            }

            wchar_t rootPath[] = { letter, L':', L'\\', L'\0' };
            if (GetDriveTypeW(rootPath) == DRIVE_FIXED)
            {
                roots.emplace_back(rootPath);
            }
        }

        return roots;
    }

    AvObjectType AntivirusScanner::DetectObjectType(const std::filesystem::path& filePath, std::wstring& errorMessage)
    {
        errorMessage.clear();

        FileByteStream stream(filePath);
        if (!stream.IsOpen())
        {
            errorMessage = L"Could not open file for type detection.";
            return AvObjectType::Unknown;
        }

        std::array<uint8_t, 256> header = {};
        size_t bytesRead = 0;
        if (!stream.Read(header.data(), header.size(), bytesRead))
        {
            errorMessage = L"Could not read file header.";
            return AvObjectType::Unknown;
        }

        if (bytesRead >= 2 && header[0] == static_cast<uint8_t>('M') && header[1] == static_cast<uint8_t>('Z'))
        {
            return AvObjectType::Pe;
        }

        if (IsScriptOrTextExtension(filePath) || IsMostlyText(header, bytesRead))
        {
            return AvObjectType::ScriptText;
        }

        return AvObjectType::ScriptText;
    }
}
