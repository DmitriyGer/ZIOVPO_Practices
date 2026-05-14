#include "AntivirusUpdateClient.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace
{
    bool ReadU8(const std::vector<uint8_t>& bytes, size_t& offset, uint8_t& value)
    {
        if (offset + 1 > bytes.size())
        {
            return false;
        }
        value = bytes[offset++];
        return true;
    }

    bool ReadU16(const std::vector<uint8_t>& bytes, size_t& offset, uint16_t& value)
    {
        if (offset + 2 > bytes.size())
        {
            return false;
        }
        value = static_cast<uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
        offset += 2;
        return true;
    }

    bool ReadU32(const std::vector<uint8_t>& bytes, size_t& offset, uint32_t& value)
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

    bool ReadU64(const std::vector<uint8_t>& bytes, size_t& offset, uint64_t& value)
    {
        if (offset + 8 > bytes.size())
        {
            return false;
        }
        value = 0;
        for (size_t index = 0; index < 8; ++index)
        {
            value = (value << 8) | bytes[offset + index];
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

    bool ReadByteArray(const std::vector<uint8_t>& bytes, size_t& offset, std::vector<uint8_t>& value)
    {
        uint32_t length = 0;
        return ReadU32(bytes, offset, length) && ReadBytes(bytes, offset, length, value);
    }

    bool ReadUtf8(const std::vector<uint8_t>& bytes, size_t& offset, std::string& value)
    {
        std::vector<uint8_t> raw;
        if (!ReadByteArray(bytes, offset, raw))
        {
            return false;
        }
        value.assign(raw.begin(), raw.end());
        return true;
    }

    std::wstring ToWide(const std::string& value)
    {
        return std::wstring(value.begin(), value.end());
    }

    std::string ToHex(const std::vector<uint8_t>& bytes)
    {
        constexpr char kHex[] = "0123456789ABCDEF";
        std::string text;
        text.reserve(bytes.size() * 2);
        for (uint8_t byte : bytes)
        {
            text.push_back(kHex[(byte >> 4) & 0x0F]);
            text.push_back(kHex[byte & 0x0F]);
        }
        return text;
    }

    std::string EscapeJson(const std::string& value)
    {
        std::ostringstream stream;
        for (const char ch : value)
        {
            switch (ch)
            {
            case '\\': stream << "\\\\"; break;
            case '"': stream << "\\\""; break;
            case '\b': stream << "\\b"; break;
            case '\f': stream << "\\f"; break;
            case '\n': stream << "\\n"; break;
            case '\r': stream << "\\r"; break;
            case '\t': stream << "\\t"; break;
            default:
                stream << ch;
                break;
            }
        }
        return stream.str();
    }

    std::string ObjectTypeCodeToFileType(uint8_t code)
    {
        switch (code)
        {
        case 1: return "exe";
        case 2: return "dll";
        case 3: return "sys";
        case 11: return "script";
        default: return "bin";
        }
    }

    Antivirus::AvObjectType ObjectTypeFromFileTypeCode(uint8_t code)
    {
        return code == 1 || code == 2 || code == 3
            ? Antivirus::AvObjectType::Pe
            : Antivirus::AvObjectType::ScriptText;
    }

    std::string BuildCanonicalSignaturePayload(
        const std::string& threatName,
        const std::vector<uint8_t>& firstBytes,
        const std::vector<uint8_t>& remainderHash,
        uint64_t remainderLength,
        uint8_t fileTypeCode,
        uint64_t offsetStart,
        uint64_t offsetEnd,
        uint8_t status)
    {
        std::ostringstream json;
        json << "{\"fileType\":\"" << ObjectTypeCodeToFileType(fileTypeCode) << "\",";
        json << "\"firstBytesHex\":\"" << ToHex(firstBytes) << "\",";
        json << "\"offsetEnd\":" << offsetEnd << ",";
        json << "\"offsetStart\":" << offsetStart << ",";
        json << "\"remainderHashHex\":\"" << ToHex(remainderHash) << "\",";
        json << "\"remainderLength\":" << remainderLength << ",";
        json << "\"status\":\"" << (status == 2 ? "DELETED" : "ACTUAL") << "\",";
        json << "\"threatName\":\"" << EscapeJson(threatName) << "\"}";
        return json.str();
    }

    std::string ExtractBoundary(const std::string& contentType)
    {
        const std::string marker = "boundary=";
        const size_t pos = contentType.find(marker);
        if (pos == std::string::npos)
        {
            return {};
        }
        std::string boundary = contentType.substr(pos + marker.size());
        if (!boundary.empty() && boundary.front() == '"')
        {
            boundary.erase(boundary.begin());
        }
        if (!boundary.empty() && boundary.back() == '"')
        {
            boundary.pop_back();
        }
        return boundary;
    }

    std::map<std::string, std::vector<uint8_t>> ParseMultipart(
        const std::vector<uint8_t>& body,
        const std::string& contentType)
    {
        std::map<std::string, std::vector<uint8_t>> parts;
        const std::string boundary = ExtractBoundary(contentType);
        if (boundary.empty())
        {
            return parts;
        }

        const std::string text(body.begin(), body.end());
        const std::string delimiter = "--" + boundary;
        size_t pos = 0;
        while ((pos = text.find(delimiter, pos)) != std::string::npos)
        {
            pos += delimiter.size();
            if (pos < text.size() && text.compare(pos, 2, "--") == 0)
            {
                break;
            }
            if (text.compare(pos, 2, "\r\n") == 0)
            {
                pos += 2;
            }

            const size_t headerEnd = text.find("\r\n\r\n", pos);
            if (headerEnd == std::string::npos)
            {
                break;
            }
            const std::string headers = text.substr(pos, headerEnd - pos);
            const size_t dataStart = headerEnd + 4;
            const size_t next = text.find("\r\n" + delimiter, dataStart);
            if (next == std::string::npos)
            {
                break;
            }

            std::string fileName;
            const std::string nameMarker = "filename=\"";
            const size_t namePos = headers.find(nameMarker);
            if (namePos != std::string::npos)
            {
                const size_t nameStart = namePos + nameMarker.size();
                const size_t nameEnd = headers.find('"', nameStart);
                if (nameEnd != std::string::npos)
                {
                    fileName = headers.substr(nameStart, nameEnd - nameStart);
                }
            }

            if (!fileName.empty())
            {
                parts[fileName] = std::vector<uint8_t>(
                    body.begin() + static_cast<std::ptrdiff_t>(dataStart),
                    body.begin() + static_cast<std::ptrdiff_t>(next));
            }
            pos = next + 2;
        }

        return parts;
    }

    struct ManifestRecord
    {
        uint8_t Status = 0;
        uint64_t DataOffset = 0;
        uint32_t DataLength = 0;
        std::vector<uint8_t> RecordSignature;
    };

    bool ParseManifest(
        const std::vector<uint8_t>& manifest,
        const Antivirus::SignatureVerifier& verifier,
        std::vector<ManifestRecord>& records,
        std::vector<uint8_t>& expectedDataHash,
        std::chrono::system_clock::time_point& releaseDateUtc)
    {
        records.clear();
        size_t offset = 0;
        std::string magic;
        uint16_t version = 0;
        uint8_t exportType = 0;
        uint64_t releaseEpochMillis = 0;
        uint64_t sinceEpochMillis = 0;
        uint32_t recordCount = 0;
        if (!ReadUtf8(manifest, offset, magic)
            || !ReadU16(manifest, offset, version)
            || !ReadU8(manifest, offset, exportType)
            || !ReadU64(manifest, offset, releaseEpochMillis)
            || !ReadU64(manifest, offset, sinceEpochMillis)
            || !ReadU32(manifest, offset, recordCount)
            || !ReadBytes(manifest, offset, 32, expectedDataHash)
            || magic != "MF-DMITRIY"
            || version != 1)
        {
            return false;
        }

        records.reserve(recordCount);
        for (uint32_t index = 0; index < recordCount; ++index)
        {
            std::vector<uint8_t> uuid;
            uint64_t updatedAt = 0;
            ManifestRecord record = {};
            uint32_t signatureLength = 0;
            if (!ReadBytes(manifest, offset, 16, uuid)
                || !ReadU8(manifest, offset, record.Status)
                || !ReadU64(manifest, offset, updatedAt)
                || !ReadU64(manifest, offset, record.DataOffset)
                || !ReadU32(manifest, offset, record.DataLength)
                || !ReadU32(manifest, offset, signatureLength)
                || !ReadBytes(manifest, offset, signatureLength, record.RecordSignature))
            {
                return false;
            }
            records.push_back(std::move(record));
        }

        const size_t unsignedManifestEnd = offset;
        uint32_t manifestSignatureLength = 0;
        std::vector<uint8_t> manifestSignature;
        if (!ReadU32(manifest, offset, manifestSignatureLength)
            || !ReadBytes(manifest, offset, manifestSignatureLength, manifestSignature)
            || offset != manifest.size())
        {
            return false;
        }

        const std::vector<uint8_t> unsignedManifest(
            manifest.begin(),
            manifest.begin() + static_cast<std::ptrdiff_t>(unsignedManifestEnd));
        if (!verifier.Verify(unsignedManifest, manifestSignature))
        {
            return false;
        }

        releaseDateUtc = std::chrono::system_clock::time_point{ std::chrono::milliseconds(releaseEpochMillis) };
        return true;
    }

    bool ConvertServerDataToLocalRecords(
        const std::vector<uint8_t>& data,
        const std::vector<ManifestRecord>& manifestRecords,
        const Antivirus::SignatureVerifier& verifier,
        std::vector<Antivirus::AvDiskSignatureRecord>& localRecords,
        size_t& skipped)
    {
        localRecords.clear();
        skipped = 0;

        size_t headerOffset = 0;
        std::string magic;
        uint16_t version = 0;
        uint32_t dataRecordCount = 0;
        if (!ReadUtf8(data, headerOffset, magic)
            || !ReadU16(data, headerOffset, version)
            || !ReadU32(data, headerOffset, dataRecordCount)
            || magic != "DB-DMITRIY"
            || version != 1
            || dataRecordCount != manifestRecords.size())
        {
            return false;
        }

        for (const ManifestRecord& manifestRecord : manifestRecords)
        {
            if (manifestRecord.Status == 2)
            {
                ++skipped;
                continue;
            }

            size_t offset = headerOffset + static_cast<size_t>(manifestRecord.DataOffset);
            const size_t end = offset + manifestRecord.DataLength;
            std::string threatName;
            std::vector<uint8_t> firstBytes;
            std::vector<uint8_t> remainderHash;
            uint64_t remainderLength = 0;
            uint8_t fileTypeCode = 0;
            uint64_t offsetStart = 0;
            uint64_t offsetEnd = 0;

            if (end > data.size()
                || !ReadUtf8(data, offset, threatName)
                || !ReadByteArray(data, offset, firstBytes)
                || !ReadByteArray(data, offset, remainderHash)
                || !ReadU64(data, offset, remainderLength)
                || !ReadU8(data, offset, fileTypeCode)
                || !ReadU64(data, offset, offsetStart)
                || !ReadU64(data, offset, offsetEnd)
                || offset != end)
            {
                return false;
            }

            const std::string canonicalJson = BuildCanonicalSignaturePayload(
                threatName,
                firstBytes,
                remainderHash,
                remainderLength,
                fileTypeCode,
                offsetStart,
                offsetEnd,
                manifestRecord.Status);
            const std::vector<uint8_t> payload(canonicalJson.begin(), canonicalJson.end());
            if (!verifier.Verify(payload, manifestRecord.RecordSignature) || remainderLength != 0)
            {
                ++skipped;
                continue;
            }

            Antivirus::AvDiskSignatureRecord localRecord = {};
            localRecord.ObjectSignatureBytes = firstBytes;
            localRecord.OffsetBegin = offsetStart;
            localRecord.OffsetEnd = offsetEnd;
            localRecord.ObjectType = ObjectTypeFromFileTypeCode(fileTypeCode);
            localRecords.push_back(std::move(localRecord));
        }

        return true;
    }
}

namespace Antivirus
{
    AvUpdateClient::AvUpdateClient(const ApiIntegration::ApiClient& apiClient)
        : m_apiClient(apiClient)
    {
    }

    bool AvUpdateClient::IsNetworkAvailable(const std::wstring& accessToken) const
    {
        return m_apiClient.IsServiceReachable(&accessToken);
    }

    AvUpdateDownloadResult AvUpdateClient::DownloadFullDatabase(
        const std::wstring& accessToken,
        const std::filesystem::path& incomingPath,
        const std::filesystem::path& temporaryPath,
        const SignatureVerifier& webSignatureVerifier,
        const DemoHmacSha256SignatureVerifier& localSigner) const
    {
        AvUpdateDownloadResult result = {};
        ApiIntegration::ApiClient::BinaryResponse response = {};
        result.NetworkReachable = m_apiClient.DownloadBinary(
            L"GET",
            L"/api/binary/signatures/full",
            {},
            L"multipart/mixed",
            &accessToken,
            response);
        if (!result.NetworkReachable || response.statusCode != 200)
        {
            result.Message = L"Could not download binary signature package from backend.";
            return result;
        }

        const std::map<std::string, std::vector<uint8_t>> parts = ParseMultipart(response.body, response.contentType);
        const auto manifestIt = parts.find("manifest.bin");
        const auto dataIt = parts.find("data.bin");
        if (manifestIt == parts.end() || dataIt == parts.end())
        {
            result.Message = L"Backend binary signature package does not contain manifest.bin and data.bin.";
            return result;
        }

        std::vector<ManifestRecord> manifestRecords;
        std::vector<uint8_t> expectedDataHash;
        std::chrono::system_clock::time_point releaseDateUtc = {};
        if (!ParseManifest(manifestIt->second, webSignatureVerifier, manifestRecords, expectedDataHash, releaseDateUtc))
        {
            result.Message = L"Backend manifest signature or format is invalid.";
            return result;
        }

        std::vector<uint8_t> actualDataHash;
        if (!CalculateSha256(dataIt->second, actualDataHash) || actualDataHash != expectedDataHash)
        {
            result.Message = L"Backend data.bin checksum is invalid.";
            return result;
        }

        std::vector<AvDiskSignatureRecord> localRecords;
        if (!ConvertServerDataToLocalRecords(
                dataIt->second,
                manifestRecords,
                webSignatureVerifier,
                localRecords,
                result.SkippedRecordCount))
        {
            result.Message = L"Could not parse backend data.bin records.";
            return result;
        }

        result.ImportedRecordCount = localRecords.size();
        if (localRecords.empty())
        {
            result.Message = L"Backend package has no records compatible with local full-byte AVDB format.";
            return result;
        }

        result.Downloaded = AvDatabaseStore::SaveDatabaseFile(
            incomingPath,
            temporaryPath,
            localRecords,
            releaseDateUtc,
            localSigner);
        result.Message = result.Downloaded
            ? L"Backend binary signature package imported to incoming AVDB."
            : L"Could not write incoming AVDB.";
        return result;
    }
}
