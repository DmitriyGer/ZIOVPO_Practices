#include "ByteStream.h"

#include <algorithm>
#include <utility>

namespace Antivirus
{
    ByteStream::ByteStream(std::vector<uint8_t> bytes)
        : m_bytes(std::move(bytes))
    {
    }

    bool ByteStream::Read(uint8_t* buffer, size_t bytesToRead, size_t& bytesRead)
    {
        bytesRead = 0;
        if (buffer == nullptr && bytesToRead > 0)
        {
            return false;
        }

        const uint64_t remaining = m_position < m_bytes.size()
            ? static_cast<uint64_t>(m_bytes.size()) - m_position
            : 0;
        const size_t count = static_cast<size_t>((std::min)(
            remaining,
            static_cast<uint64_t>(bytesToRead)));
        if (count > 0)
        {
            std::copy_n(m_bytes.data() + static_cast<size_t>(m_position), count, buffer);
            m_position += count;
        }

        bytesRead = count;
        return true;
    }

    bool ByteStream::Seek(uint64_t position)
    {
        if (position > m_bytes.size())
        {
            return false;
        }

        m_position = position;
        return true;
    }

    bool ByteStream::Tell(uint64_t& position)
    {
        position = m_position;
        return true;
    }

    uint64_t ByteStream::Size() const
    {
        return static_cast<uint64_t>(m_bytes.size());
    }

    FileByteStream::FileByteStream(const std::filesystem::path& filePath)
    {
        m_stream.open(filePath, std::ios::binary);
        if (!m_stream.is_open())
        {
            return;
        }

        m_stream.seekg(0, std::ios::end);
        const std::streampos endPosition = m_stream.tellg();
        if (endPosition < 0)
        {
            m_stream.close();
            return;
        }

        m_size = static_cast<uint64_t>(endPosition);
        m_stream.seekg(0, std::ios::beg);
    }

    bool FileByteStream::IsOpen() const
    {
        return m_stream.is_open();
    }

    bool FileByteStream::Read(uint8_t* buffer, size_t bytesToRead, size_t& bytesRead)
    {
        bytesRead = 0;
        if (!m_stream.is_open() || (buffer == nullptr && bytesToRead > 0))
        {
            return false;
        }

        m_stream.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(bytesToRead));
        bytesRead = static_cast<size_t>(m_stream.gcount());
        if (m_stream.bad())
        {
            return false;
        }

        if (m_stream.eof())
        {
            m_stream.clear();
        }

        return true;
    }

    bool FileByteStream::Seek(uint64_t position)
    {
        if (!m_stream.is_open() || position > m_size)
        {
            return false;
        }

        m_stream.clear();
        m_stream.seekg(static_cast<std::streamoff>(position), std::ios::beg);
        return !m_stream.fail();
    }

    bool FileByteStream::Tell(uint64_t& position)
    {
        if (!m_stream.is_open())
        {
            return false;
        }

        const std::streampos streamPosition = m_stream.tellg();
        if (streamPosition < 0)
        {
            return false;
        }

        position = static_cast<uint64_t>(streamPosition);
        return true;
    }

    uint64_t FileByteStream::Size() const
    {
        return m_size;
    }
}
