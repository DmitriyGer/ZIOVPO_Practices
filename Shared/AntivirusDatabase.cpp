#include "AntivirusDatabase.h"

#include <cwchar>
#include <limits>

namespace
{
    std::vector<uint8_t> BytesFromAscii(const char* text)
    {
        std::vector<uint8_t> bytes;
        if (text == nullptr)
        {
            return bytes;
        }

        while (*text != '\0')
        {
            bytes.push_back(static_cast<uint8_t>(*text));
            ++text;
        }

        return bytes;
    }
}

namespace Antivirus
{
    void InMemoryAvDatabase::Clear()
    {
        m_records.clear();
        m_releaseDateUtc = {};
        m_lastSuccessfulLoadUtc = {};
        m_recordCount = 0;
        m_loadStatus = AvDatabaseLoadStatus::NotLoaded;
        m_source = AvDatabaseFileSource::None;
        m_lastUpdateStatus.clear();
        m_verifierName.clear();
        m_skippedRecordCount = 0;
        m_lastManifestVerifiedUtc = {};
        m_schedulerEnabled = false;
        m_monitoringEnabled = false;
    }

    bool InMemoryAvDatabase::AddSignature(
        const std::vector<uint8_t>& signatureBytes,
        uint64_t offsetBegin,
        uint64_t offsetEnd,
        AvObjectType objectType,
        const std::vector<uint8_t>& avRecordSignature,
        const std::wstring& recordId)
    {
        if (signatureBytes.size() < 8
            || signatureBytes.size() > (std::numeric_limits<uint32_t>::max)()
            || offsetBegin > offsetEnd
            || objectType == AvObjectType::Unknown)
        {
            return false;
        }

        AvSignatureRecord record = {};
        record.ObjectSignaturePrefix = BuildSignaturePrefix(signatureBytes.data(), signatureBytes.size());
        record.ObjectSignatureLength = static_cast<uint32_t>(signatureBytes.size());
        record.RecordId = recordId;
        if (record.RecordId.empty())
        {
            wchar_t generatedId[64] = {};
            swprintf_s(
                generatedId,
                L"AVREC-%016llX-%u",
                static_cast<unsigned long long>(record.ObjectSignaturePrefix),
                record.ObjectSignatureLength);
            record.RecordId = generatedId;
        }

        record.ObjectSignature = ToLittleEndianBytes(CalculateFnv1a64(signatureBytes.data(), signatureBytes.size()));
        record.FullSignature = signatureBytes;
        record.OffsetBegin = offsetBegin;
        record.OffsetEnd = offsetEnd;
        record.ObjectType = objectType;
        record.AvRecordSignature = avRecordSignature;

        m_records[record.ObjectSignaturePrefix].push_back(record);
        ++m_recordCount;
        if (m_releaseDateUtc == std::chrono::system_clock::time_point{})
        {
            m_releaseDateUtc = std::chrono::system_clock::now();
        }
        if (m_lastSuccessfulLoadUtc == std::chrono::system_clock::time_point{})
        {
            m_lastSuccessfulLoadUtc = std::chrono::system_clock::now();
        }
        m_loadStatus = AvDatabaseLoadStatus::Loaded;

        return true;
    }

    void InMemoryAvDatabase::LoadDemoRecords()
    {
        Clear();
        m_releaseDateUtc = std::chrono::system_clock::now();

        const std::vector<uint8_t> demoRecordSignature = BytesFromAscii("TRAY-AV-DEMO-SIGNATURE");

        AddSignature(
            BytesFromAscii("PE-DEMO-DROPPER"),
            0,
            4096,
            AvObjectType::Pe,
            demoRecordSignature,
            L"DEMO-PE-DROPPER");

        AddSignature(
            BytesFromAscii("PE-DEMO-TROJAN"),
            512,
            8192,
            AvObjectType::Pe,
            demoRecordSignature,
            L"DEMO-PE-TROJAN");

        AddSignature(
            BytesFromAscii("SCRIPTX-BAD-CALL"),
            0,
            (std::numeric_limits<uint64_t>::max)(),
            AvObjectType::ScriptText,
            demoRecordSignature,
            L"DEMO-SCRIPT-BAD-CALL");

        AddSignature(
            BytesFromAscii("X5O!P%@AP[4\\PZX54(P^)7CC)7}$EICAR-STANDARD-ANTIVIRUS-TEST-FILE!$H+H*"),
            0,
            (std::numeric_limits<uint64_t>::max)(),
            AvObjectType::ScriptText,
            demoRecordSignature,
            L"DEMO-EICAR-TEST-FILE");
    }

    void InMemoryAvDatabase::SetLoadedReleaseDate(std::chrono::system_clock::time_point releaseDateUtc)
    {
        m_releaseDateUtc = releaseDateUtc;
        if (m_recordCount > 0)
        {
            m_loadStatus = AvDatabaseLoadStatus::Loaded;
        }
    }

    void InMemoryAvDatabase::SetLoadMetadata(
        AvDatabaseFileSource source,
        std::chrono::system_clock::time_point lastSuccessfulLoadUtc,
        const std::wstring& lastUpdateStatus,
        const std::wstring& verifierName,
        size_t skippedRecordCount,
        std::chrono::system_clock::time_point lastManifestVerifiedUtc)
    {
        m_source = source;
        m_lastSuccessfulLoadUtc = lastSuccessfulLoadUtc;
        m_lastUpdateStatus = lastUpdateStatus;
        if (!verifierName.empty())
        {
            m_verifierName = verifierName;
        }
        m_skippedRecordCount = skippedRecordCount;
        if (lastManifestVerifiedUtc != std::chrono::system_clock::time_point{})
        {
            m_lastManifestVerifiedUtc = lastManifestVerifiedUtc;
        }
        if (m_recordCount > 0)
        {
            m_loadStatus = AvDatabaseLoadStatus::Loaded;
        }
    }

    void InMemoryAvDatabase::SetRuntimeFeatureStatus(bool schedulerEnabled, bool monitoringEnabled)
    {
        m_schedulerEnabled = schedulerEnabled;
        m_monitoringEnabled = monitoringEnabled;
    }

    AvDatabaseInfo InMemoryAvDatabase::GetInfo() const
    {
        AvDatabaseInfo info = {};
        info.ReleaseDateUtc = m_releaseDateUtc;
        info.LastSuccessfulLoadUtc = m_lastSuccessfulLoadUtc;
        info.RecordCount = m_recordCount;
        info.LoadStatus = m_loadStatus;
        info.Source = m_source;
        info.LastUpdateStatus = m_lastUpdateStatus;
        info.VerifierName = m_verifierName;
        info.SkippedRecordCount = m_skippedRecordCount;
        info.LastManifestVerifiedUtc = m_lastManifestVerifiedUtc;
        info.SchedulerEnabled = m_schedulerEnabled;
        info.MonitoringEnabled = m_monitoringEnabled;
        return info;
    }

    const InMemoryAvDatabase::SignatureMap& InMemoryAvDatabase::Records() const
    {
        return m_records;
    }
}
