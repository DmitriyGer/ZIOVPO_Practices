#include "AntivirusEngine.h"

#include <algorithm>
#include <limits>

namespace Antivirus
{
    AvEngine::AvEngine(const InMemoryAvDatabase& database)
        : m_database(database)
    {
    }

    AvScanResult AvEngine::Scan(IByteStream& stream, AvObjectType objectType) const
    {
        AvScanResult result = {};
        if (objectType == AvObjectType::Unknown || !stream.Seek(0))
        {
            result.Status = AvScanStatus::Error;
            return result;
        }

        const uint64_t streamSize = stream.Size();
        if (streamSize < 8)
        {
            return result;
        }

        if (streamSize > static_cast<uint64_t>((std::numeric_limits<size_t>::max)()))
        {
            result.Status = AvScanStatus::Error;
            return result;
        }

        std::vector<uint8_t> bytes(static_cast<size_t>(streamSize));
        size_t totalBytesRead = 0;
        while (totalBytesRead < bytes.size())
        {
            size_t bytesRead = 0;
            if (!stream.Read(bytes.data() + totalBytesRead, bytes.size() - totalBytesRead, bytesRead))
            {
                result.Status = AvScanStatus::Error;
                return result;
            }

            if (bytesRead == 0)
            {
                break;
            }

            totalBytesRead += bytesRead;
        }

        if (totalBytesRead != bytes.size())
        {
            result.Status = AvScanStatus::Error;
            return result;
        }

        for (size_t scanOffset = 0; scanOffset + 8 <= bytes.size(); ++scanOffset)
        {
            const uint64_t prefix = BuildSignaturePrefix(bytes.data() + scanOffset, bytes.size() - scanOffset);
            const auto& records = m_database.Records();
            const auto bucket = records.find(prefix);
            if (bucket == records.end())
            {
                continue;
            }

            for (const AvSignatureRecord& record : bucket->second)
            {
                if (record.ObjectType != objectType)
                {
                    continue;
                }

                const uint64_t currentOffset = static_cast<uint64_t>(scanOffset);
                if (currentOffset < record.OffsetBegin || currentOffset > record.OffsetEnd)
                {
                    continue;
                }

                if (record.ObjectSignatureLength < 8)
                {
                    continue;
                }

                const size_t fullLength = static_cast<size_t>(record.ObjectSignatureLength);
                if (scanOffset + fullLength > bytes.size())
                {
                    continue;
                }

                const std::vector<uint8_t> fragmentHash =
                    ToLittleEndianBytes(CalculateFnv1a64(bytes.data() + scanOffset, fullLength));
                if (fragmentHash != record.ObjectSignature)
                {
                    continue;
                }

                result.Status = AvScanStatus::Detected;
                result.DetectionOffset = currentOffset;
                result.ObjectType = objectType;
                result.ObjectSignatureLength = record.ObjectSignatureLength;
                result.RecordId = record.RecordId;
                result.ObjectSignature = record.ObjectSignature;
                return result;
            }
        }

        return result;
    }

    AvScanResult AvEngine::ScanFile(const std::filesystem::path& filePath, AvObjectType objectType) const
    {
        FileByteStream stream(filePath);
        if (!stream.IsOpen())
        {
            AvScanResult result = {};
            result.Status = AvScanStatus::Error;
            return result;
        }

        return Scan(stream, objectType);
    }
}
