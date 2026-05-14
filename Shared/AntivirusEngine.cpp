#include "AntivirusEngine.h"

#include <algorithm>
#include <array>
#include <limits>
#include <queue>

namespace Antivirus
{
    namespace
    {
        struct AhoNode
        {
            std::array<int, 256> Next = {};
            int Failure = 0;
            std::vector<const AvSignatureRecord*> Records;

            AhoNode()
            {
                Next.fill(-1);
            }
        };

        // Builds an Aho-Corasick trie over full byte signatures stored in memory.
        std::vector<AhoNode> BuildAhoCorasickAutomaton(const InMemoryAvDatabase& database)
        {
            std::vector<AhoNode> nodes(1);
            for (const auto& bucket : database.Records())
            {
                for (const AvSignatureRecord& record : bucket.second)
                {
                    if (record.FullSignature.size() < 8)
                    {
                        continue;
                    }

                    int nodeIndex = 0;
                    for (const uint8_t byte : record.FullSignature)
                    {
                        int nextIndex = nodes[nodeIndex].Next[byte];
                        if (nextIndex < 0)
                        {
                            nextIndex = static_cast<int>(nodes.size());
                            nodes[nodeIndex].Next[byte] = nextIndex;
                            nodes.emplace_back();
                        }

                        nodeIndex = nextIndex;
                    }

                    nodes[nodeIndex].Records.push_back(&record);
                }
            }

            std::queue<int> queue;
            for (size_t byte = 0; byte < 256; ++byte)
            {
                int& nextIndex = nodes[0].Next[byte];
                if (nextIndex < 0)
                {
                    nextIndex = 0;
                    continue;
                }

                queue.push(nextIndex);
            }

            while (!queue.empty())
            {
                const int current = queue.front();
                queue.pop();

                const int failure = nodes[current].Failure;
                nodes[current].Records.insert(
                    nodes[current].Records.end(),
                    nodes[failure].Records.begin(),
                    nodes[failure].Records.end());

                for (size_t byte = 0; byte < 256; ++byte)
                {
                    int& nextIndex = nodes[current].Next[byte];
                    if (nextIndex < 0)
                    {
                        nextIndex = nodes[failure].Next[byte];
                        continue;
                    }

                    nodes[nextIndex].Failure = nodes[failure].Next[byte];
                    queue.push(nextIndex);
                }
            }

            return nodes;
        }

        // Validates object type and offset constraints for one matched record.
        bool IsAhoMatchAllowed(const AvSignatureRecord& record, AvObjectType objectType, uint64_t offset)
        {
            return record.ObjectType == objectType
                && record.ObjectSignatureLength >= 8
                && offset >= record.OffsetBegin
                && offset <= record.OffsetEnd;
        }
    }

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

        const std::vector<AhoNode> ahoNodes = BuildAhoCorasickAutomaton(m_database);
        if (ahoNodes.size() > 1)
        {
            int state = 0;
            for (size_t index = 0; index < bytes.size(); ++index)
            {
                state = ahoNodes[state].Next[bytes[index]];
                for (const AvSignatureRecord* record : ahoNodes[state].Records)
                {
                    if (record == nullptr || record->ObjectSignatureLength > index + 1)
                    {
                        continue;
                    }

                    const uint64_t matchOffset =
                        static_cast<uint64_t>(index + 1 - record->ObjectSignatureLength);
                    if (!IsAhoMatchAllowed(*record, objectType, matchOffset))
                    {
                        continue;
                    }

                    result.Status = AvScanStatus::Detected;
                    result.DetectionOffset = matchOffset;
                    result.ObjectType = objectType;
                    result.ObjectSignatureLength = record->ObjectSignatureLength;
                    result.RecordId = record->RecordId;
                    result.ObjectSignature = record->ObjectSignature;
                    return result;
                }
            }

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
