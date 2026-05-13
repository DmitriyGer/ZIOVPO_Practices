#pragma once

#include "AntivirusDatabase.h"
#include "ByteStream.h"

namespace Antivirus
{
    class AvEngine
    {
    public:
        // Creates a scanner over an in-memory antivirus database.
        explicit AvEngine(const InMemoryAvDatabase& database);

        // Scans a byte stream for signatures matching the supplied object type.
        AvScanResult Scan(IByteStream& stream, AvObjectType objectType) const;

        // Opens and scans one file through the byte stream abstraction.
        AvScanResult ScanFile(const std::filesystem::path& filePath, AvObjectType objectType) const;

    private:
        const InMemoryAvDatabase& m_database;
    };
}
