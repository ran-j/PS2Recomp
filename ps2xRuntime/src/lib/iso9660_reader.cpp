#include "runtime/iso9660_reader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace ps2x::iso9660
{
    namespace
    {
        constexpr uint64_t kVolumeDescriptorStart = 16u;
        constexpr uint32_t kMaximumDirectoryBytes = 64u * 1024u * 1024u;
        constexpr size_t kMaximumVisitedDirectories = 65536u;

        uint16_t readLe16(const uint8_t *value)
        {
            return static_cast<uint16_t>(value[0]) |
                   static_cast<uint16_t>(static_cast<uint16_t>(value[1]) << 8u);
        }

        uint32_t readLe32(const uint8_t *value)
        {
            return static_cast<uint32_t>(value[0]) |
                   (static_cast<uint32_t>(value[1]) << 8u) |
                   (static_cast<uint32_t>(value[2]) << 16u) |
                   (static_cast<uint32_t>(value[3]) << 24u);
        }

        std::string normalizedName(std::string_view name)
        {
            const size_t version = name.find(';');
            if (version != std::string_view::npos)
                name = name.substr(0u, version);

            while (!name.empty() && name.back() == '.')
                name.remove_suffix(1u);

            std::string result;
            result.reserve(name.size());
            for (const unsigned char character : name)
                result.push_back(static_cast<char>(std::toupper(character)));
            return result;
        }

        bool parseDirectoryRecord(const uint8_t *record,
                                  size_t recordLength,
                                  DirectoryEntry &result,
                                  bool allowSpecialIdentifier = false)
        {
            if (!record || recordLength < 34u || record[0] < 34u || record[0] > recordLength)
                return false;

            const uint8_t identifierLength = record[32u];
            if (identifierLength == 0u || static_cast<size_t>(33u + identifierLength) > record[0])
                return false;

            // ISO9660 uses one-byte identifiers 0 and 1 for "." and "..".
            // The PVD root directory record is also encoded with identifier 0.
            if (identifierLength == 1u && (record[33u] == 0u || record[33u] == 1u))
            {
                if (!allowSpecialIdentifier)
                    return false;
                result.name = record[33u] == 0u ? "." : "..";
            }
            else
            {
                result.name.assign(reinterpret_cast<const char *>(record + 33u), identifierLength);
            }
            // Extended-attribute blocks precede the file data extent.
            const uint64_t dataExtent = static_cast<uint64_t>(readLe32(record + 2u)) + record[1u];
            if (dataExtent > std::numeric_limits<uint32_t>::max())
                return false;
            result.extentLba = static_cast<uint32_t>(dataExtent);
            result.size = readLe32(record + 10u);
            result.flags = record[25u];
            return true;
        }

        bool isSafeDirectoryEntry(const DirectoryEntry &entry)
        {
            return !entry.name.empty() && entry.extentLba != 0u;
        }
    }

    bool Image::open(const std::filesystem::path &path, std::string *error)
    {
        close();

        const auto fail = [this, error](const char *message)
        {
            close();
            if (error)
                *error = message;
            return false;
        };

        std::error_code sizeError;
        const uint64_t fileSize = std::filesystem::file_size(path, sizeError);
        if (sizeError)
            return fail("Unable to determine ISO image size");
        if (fileSize < (kVolumeDescriptorStart + 1u) * kLogicalBlockSize)
            return fail("File is too small to contain an ISO9660 volume descriptor");

        m_stream.open(path, std::ios::binary);
        if (!m_stream)
            return fail("Unable to open ISO image");

        m_imageSize = fileSize;
        std::array<uint8_t, kLogicalBlockSize> descriptor{};

        bool foundPrimaryVolumeDescriptor = false;
        for (uint64_t sector = kVolumeDescriptorStart; sector < kVolumeDescriptorStart + 64u; ++sector)
        {
            const uint64_t offset = sector * kLogicalBlockSize;
            if (!readAt(offset, descriptor.data(), descriptor.size()))
                break;

            if (descriptor[1u] != 'C' || descriptor[2u] != 'D' ||
                descriptor[3u] != '0' || descriptor[4u] != '0' ||
                descriptor[5u] != '1' || descriptor[6u] != 1u)
            {
                return fail("Invalid ISO9660 volume descriptor signature");
            }

            if (descriptor[0u] == 255u)
                break;
            if (descriptor[0u] != 1u)
                continue;
            if (readLe16(descriptor.data() + 128u) != kLogicalBlockSize)
                return fail("Unsupported ISO9660 logical block size");

            DirectoryEntry root;
            const uint8_t rootRecordLength = descriptor[156u];
            if (rootRecordLength < 34u ||
                !parseDirectoryRecord(descriptor.data() + 156u,
                                      rootRecordLength,
                                      root,
                                      true) ||
                !root.isDirectory() ||
                !entryRangeIsValid(root.extentLba, root.size))
            {
                return fail("Invalid ISO9660 root directory record");
            }

            m_rootDirectory = std::move(root);
            foundPrimaryVolumeDescriptor = true;
            break;
        }

        if (!foundPrimaryVolumeDescriptor)
            return fail("ISO9660 primary volume descriptor not found");

        if (error)
            error->clear();
        return true;
    }

    void Image::close() noexcept
    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        if (m_stream.is_open())
            m_stream.close();
        m_stream.clear();
        m_imageSize = 0u;
        m_rootDirectory = {};
    }

    bool Image::readAt(uint64_t offset, void *destination, size_t size) const
    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        if (!m_stream.is_open() || (!destination && size != 0u))
            return false;
        if (offset > m_imageSize || static_cast<uint64_t>(size) > m_imageSize - offset)
            return false;
        if (size == 0u)
            return true;
        if (offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
            size > static_cast<size_t>(std::numeric_limits<std::streamsize>::max()))
        {
            return false;
        }

        m_stream.clear();
        m_stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!m_stream)
            return false;

        m_stream.read(static_cast<char *>(destination), static_cast<std::streamsize>(size));
        return m_stream.gcount() == static_cast<std::streamsize>(size);
    }

    bool Image::entryRangeIsValid(uint32_t extentLba, uint32_t size) const noexcept
    {
        if (extentLba == 0u)
            return false;

        const uint64_t offset = static_cast<uint64_t>(extentLba) * kLogicalBlockSize;
        return offset <= m_imageSize && static_cast<uint64_t>(size) <= m_imageSize - offset;
    }

    bool Image::loadDirectory(const DirectoryEntry &directory,
                              std::vector<DirectoryEntry> &entries) const
    {
        entries.clear();
        if (!directory.isDirectory() ||
            directory.size > kMaximumDirectoryBytes ||
            !entryRangeIsValid(directory.extentLba, directory.size))
        {
            return false;
        }

        std::vector<uint8_t> bytes(directory.size);
        const uint64_t offset = static_cast<uint64_t>(directory.extentLba) * kLogicalBlockSize;
        if (!bytes.empty() && !readAt(offset, bytes.data(), bytes.size()))
            return false;

        size_t cursor = 0u;
        while (cursor < bytes.size())
        {
            const uint8_t recordLength = bytes[cursor];
            if (recordLength == 0u)
            {
                const size_t nextSector =
                    ((cursor / kLogicalBlockSize) + 1u) * kLogicalBlockSize;
                if (nextSector <= cursor)
                    break;
                cursor = std::min(nextSector, bytes.size());
                continue;
            }

            if (static_cast<size_t>(recordLength) > bytes.size() - cursor)
                return false;

            DirectoryEntry entry;
            if (parseDirectoryRecord(bytes.data() + cursor, recordLength, entry))
            {
                if (!isSafeDirectoryEntry(entry) ||
                    !entryRangeIsValid(entry.extentLba, entry.size))
                {
                    return false;
                }
                entries.push_back(std::move(entry));
            }

            cursor += recordLength;
        }
        return true;
    }

    bool Image::findFile(std::string_view path, DirectoryEntry &result) const
    {
        result = {};
        if (!isOpen())
            return false;

        std::vector<std::string> components;
        size_t start = 0u;
        while (start < path.size())
        {
            while (start < path.size() && (path[start] == '/' || path[start] == '\\'))
                ++start;
            if (start >= path.size())
                break;

            size_t end = start;
            while (end < path.size() && path[end] != '/' && path[end] != '\\')
                ++end;

            const std::string component = normalizedName(path.substr(start, end - start));
            if (!component.empty() && component != ".")
            {
                if (component == "..")
                    return false;
                components.push_back(component);
            }
            start = end;
        }

        if (components.empty())
            return false;

        DirectoryEntry currentDirectory = m_rootDirectory;
        for (size_t componentIndex = 0u; componentIndex < components.size(); ++componentIndex)
        {
            std::vector<DirectoryEntry> entries;
            if (!loadDirectory(currentDirectory, entries))
                return false;

            const auto found = std::find_if(entries.begin(), entries.end(),
                                            [&components, componentIndex](const DirectoryEntry &entry)
                                            {
                                                return normalizedName(entry.name) == components[componentIndex];
                                            });
            if (found == entries.end())
                return false;

            if (componentIndex + 1u == components.size())
            {
                result = *found;
                return true;
            }
            if (!found->isDirectory())
                return false;
            currentDirectory = *found;
        }

        return false;
    }

    bool Image::findFileRecursive(std::string_view fileName, DirectoryEntry &result) const
    {
        result = {};
        if (!isOpen())
            return false;

        const std::string wanted = normalizedName(fileName);
        if (wanted.empty())
            return false;

        std::vector<DirectoryEntry> pending{m_rootDirectory};
        std::unordered_set<uint32_t> visited;
        size_t cursor = 0u;

        while (cursor < pending.size() && visited.size() < kMaximumVisitedDirectories)
        {
            const DirectoryEntry directory = pending[cursor++];
            if (!visited.insert(directory.extentLba).second)
                continue;

            std::vector<DirectoryEntry> entries;
            if (!loadDirectory(directory, entries))
                return false;

            for (const DirectoryEntry &entry : entries)
            {
                if (!entry.isDirectory() && normalizedName(entry.name) == wanted)
                {
                    result = entry;
                    return true;
                }
                if (entry.isDirectory() && visited.find(entry.extentLba) == visited.end())
                    pending.push_back(entry);
            }
        }

        return false;
    }

    bool Image::readFile(const DirectoryEntry &entry,
                         uint64_t offset,
                         void *destination,
                         size_t size,
                         size_t *bytesRead) const
    {
        if (bytesRead)
            *bytesRead = 0u;
        if (!isOpen() || entry.isDirectory() ||
            !entryRangeIsValid(entry.extentLba, entry.size) ||
            offset > entry.size || (!destination && size != 0u))
        {
            return false;
        }

        const uint64_t remaining = static_cast<uint64_t>(entry.size) - offset;
        const size_t count = static_cast<size_t>(std::min<uint64_t>(remaining, size));
        const uint64_t imageOffset =
            static_cast<uint64_t>(entry.extentLba) * kLogicalBlockSize + offset;

        if (!readAt(imageOffset, destination, count))
            return false;
        if (bytesRead)
            *bytesRead = count;
        return true;
    }

    bool Image::readFileFully(const DirectoryEntry &entry,
                              std::vector<uint8_t> &destination) const
    {
        destination.clear();
        if (entry.isDirectory() || !entryRangeIsValid(entry.extentLba, entry.size))
            return false;

        destination.resize(entry.size);
        size_t bytesRead = 0u;
        if (!readFile(entry, 0u, destination.data(), destination.size(), &bytesRead) ||
            bytesRead != destination.size())
        {
            destination.clear();
            return false;
        }
        return true;
    }
}
