#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace Antivirus
{
    class IByteStream
    {
    public:
        virtual ~IByteStream() = default;

        // Reads up to bytesToRead bytes from the current stream position.
        virtual bool Read(uint8_t* buffer, size_t bytesToRead, size_t& bytesRead) = 0;

        // Moves the current stream position to an absolute byte offset.
        virtual bool Seek(uint64_t position) = 0;

        // Returns the current stream position.
        virtual bool Tell(uint64_t& position) = 0;

        // Returns the total stream size in bytes.
        virtual uint64_t Size() const = 0;
    };

    class ByteStream final : public IByteStream
    {
    public:
        // Creates an in-memory byte stream from raw bytes.
        explicit ByteStream(std::vector<uint8_t> bytes);

        // Reads up to bytesToRead bytes from memory.
        bool Read(uint8_t* buffer, size_t bytesToRead, size_t& bytesRead) override;

        // Moves the memory cursor to an absolute byte offset.
        bool Seek(uint64_t position) override;

        // Returns the current memory cursor position.
        bool Tell(uint64_t& position) override;

        // Returns the in-memory byte count.
        uint64_t Size() const override;

    private:
        std::vector<uint8_t> m_bytes;
        uint64_t m_position = 0;
    };

    class FileByteStream final : public IByteStream
    {
    public:
        // Opens a file stream for binary scanning.
        explicit FileByteStream(const std::filesystem::path& filePath);

        // Returns true when the file stream was opened successfully.
        bool IsOpen() const;

        // Reads up to bytesToRead bytes from the file.
        bool Read(uint8_t* buffer, size_t bytesToRead, size_t& bytesRead) override;

        // Moves the file cursor to an absolute byte offset.
        bool Seek(uint64_t position) override;

        // Returns the current file cursor position.
        bool Tell(uint64_t& position) override;

        // Returns the file size captured at open time.
        uint64_t Size() const override;

    private:
        std::ifstream m_stream;
        uint64_t m_size = 0;
    };
}
