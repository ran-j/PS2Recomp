#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace ps2x::iso9660
{
    inline constexpr uint32_t kLogicalBlockSize = 2048u;

    struct DirectoryEntry
    {
        std::string name;
        uint32_t extentLba = 0u;
        uint32_t size = 0u;
        uint8_t flags = 0u;

        [[nodiscard]] bool isDirectory() const noexcept
        {
            return (flags & 0x02u) != 0u;
        }
    };

    // Read-only ISO9660 image reader for PS2 disc images. It supports ordinary
    // ISO9660 directory records and the ;version suffix used by PS2 discs.
    class Image
    {
    public:
        Image() = default;
        ~Image() = default;

        Image(const Image &) = delete;
        Image &operator=(const Image &) = delete;

        [[nodiscard]] bool open(const std::filesystem::path &path, std::string *error = nullptr);
        void close() noexcept;

        [[nodiscard]] bool isOpen() const noexcept { return m_stream.is_open(); }
        [[nodiscard]] uint64_t imageSize() const noexcept { return m_imageSize; }

        // Resolve a path relative to the ISO root. Both '/' and '\\' are accepted.
        // File names are compared case-insensitively and ;version suffixes are ignored.
        [[nodiscard]] bool findFile(std::string_view path, DirectoryEntry &result) const;

        // Search all directories for a file name, useful for locating the boot ELF
        // when an authoring tool placed it below the ISO root.
        [[nodiscard]] bool findFileRecursive(std::string_view fileName, DirectoryEntry &result) const;

        // Read a range from an ISO file entry. A short read at EOF is successful and
        // is reported through bytesRead when supplied.
        [[nodiscard]] bool readFile(const DirectoryEntry &entry,
                                    uint64_t offset,
                                    void *destination,
                                    size_t size,
                                    size_t *bytesRead = nullptr) const;

        [[nodiscard]] bool readFileFully(const DirectoryEntry &entry,
                                         std::vector<uint8_t> &destination) const;

    private:
        [[nodiscard]] bool readAt(uint64_t offset, void *destination, size_t size) const;
        [[nodiscard]] bool loadDirectory(const DirectoryEntry &directory,
                                         std::vector<DirectoryEntry> &entries) const;
        [[nodiscard]] bool entryRangeIsValid(uint32_t extentLba, uint32_t size) const noexcept;

        mutable std::ifstream m_stream;
        mutable std::mutex m_streamMutex;
        uint64_t m_imageSize = 0u;
        DirectoryEntry m_rootDirectory;
    };
}
