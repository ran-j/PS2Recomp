#include "iop_cdvd.h"

#include "iop_cpu.h"
#include "iop_memory.h"
#include "ps2x/iop/iop_host.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kSectorSize = 2048u;
        constexpr uint32_t kPrimaryVolumeDescriptorLsn = 16u;
        constexpr uint32_t kVolumeDescriptorTerminatorLsn = 17u;
        constexpr uint32_t kFirstDirectoryLsn = 20u;
        constexpr uint32_t kCdvdErrorNone = 0u;
        constexpr uint32_t kCdvdErrorRead = 0x30u;
        constexpr uint32_t kCdvdTypePs2Dvd = 0x14u;
        constexpr uint32_t kCdvdReadyComplete = 2u;
        constexpr uint32_t kCdvdStatusPause = 0x0Au;
        constexpr uint32_t kCdvdInitExit = 5u;
        constexpr uint32_t kCdvdCallbackRead = 1u;
        constexpr uint32_t kCdvdCallbackSeek = 4u;

        uint32_t alignSectors(uint64_t bytes)
        {
            return static_cast<uint32_t>((bytes + kSectorSize - 1u) / kSectorSize);
        }

        void writeLe16(uint8_t *destination, uint16_t value)
        {
            destination[0] = static_cast<uint8_t>(value);
            destination[1] = static_cast<uint8_t>(value >> 8u);
        }

        void writeBe16(uint8_t *destination, uint16_t value)
        {
            destination[0] = static_cast<uint8_t>(value >> 8u);
            destination[1] = static_cast<uint8_t>(value);
        }

        void writeLe32(uint8_t *destination, uint32_t value)
        {
            for (uint32_t i = 0u; i < 4u; ++i)
                destination[i] = static_cast<uint8_t>(value >> (i * 8u));
        }

        void writeBe32(uint8_t *destination, uint32_t value)
        {
            for (uint32_t i = 0u; i < 4u; ++i)
                destination[i] = static_cast<uint8_t>(value >> ((3u - i) * 8u));
        }

        void writeBoth16(uint8_t *destination, uint16_t value)
        {
            writeLe16(destination, value);
            writeBe16(destination + 2u, value);
        }

        void writeBoth32(uint8_t *destination, uint32_t value)
        {
            writeLe32(destination, value);
            writeBe32(destination + 4u, value);
        }

        std::string isoName(const std::filesystem::path &path, bool directory)
        {
            std::string name = path.filename().string();
            for (char &character : name)
            {
                const unsigned char byte = static_cast<unsigned char>(character);
                character = byte < 0x80u ? static_cast<char>(std::toupper(byte)) : '_';
            }
            if (name.size() > 200u)
                name.resize(200u);
            if (!directory && name.find(';') == std::string::npos)
                name += ";1";
            return name;
        }

        size_t directoryRecordSize(size_t identifierSize)
        {
            const size_t unpadded = 33u + identifierSize;
            return unpadded + (unpadded & 1u);
        }

        uint32_t directoryBytesFor(const std::vector<size_t> &identifierSizes)
        {
            uint64_t cursor = 0u;
            for (const size_t identifierSize : identifierSizes)
            {
                const uint64_t recordSize = directoryRecordSize(identifierSize);
                const uint64_t sectorOffset = cursor % kSectorSize;
                if (sectorOffset + recordSize > kSectorSize)
                    cursor += kSectorSize - sectorOffset;
                cursor += recordSize;
            }
            return static_cast<uint32_t>(std::max<uint64_t>(kSectorSize, alignSectors(cursor) * kSectorSize));
        }

        size_t writeDirectoryRecord(uint8_t *destination,
                                    uint32_t lsn,
                                    uint32_t size,
                                    bool directory,
                                    const uint8_t *identifier,
                                    size_t identifierSize)
        {
            const size_t recordSize = directoryRecordSize(identifierSize);
            std::memset(destination, 0, recordSize);
            destination[0] = static_cast<uint8_t>(recordSize);
            writeBoth32(destination + 2u, lsn);
            writeBoth32(destination + 10u, size);
            destination[25] = directory ? 2u : 0u;
            writeBoth16(destination + 28u, 1u);
            destination[32] = static_cast<uint8_t>(identifierSize);
            std::memcpy(destination + 33u, identifier, identifierSize);
            return recordSize;
        }
    }

    class IopCdvd::Impl
    {
    public:
        struct Callback
        {
            uint32_t address = 0u;
            uint32_t gp = 0u;
        };

        struct IsoNode
        {
            std::filesystem::path hostPath;
            std::string identifier;
            size_t parent = 0u;
            bool directory = false;
            uint32_t lsn = 0u;
            uint32_t size = 0u;
            uint32_t sectors = 0u;
            uint64_t handle = 0u;
            std::vector<size_t> children;
        };

        Impl(IopHost &hostRef, IopMemory &memoryRef)
            : host(hostRef), memory(memoryRef)
        {
        }

        ~Impl()
        {
            closeFiles();
        }

        void reset()
        {
            closeFiles();
            callback = {};
            initialized = false;
            mediaMode = 0u;
            currentLsn = 0u;
            lastError = kCdvdErrorNone;
            virtualIsoBuilt = false;
            virtualIsoValid = false;
            completionCallback.reset();
            nodes.clear();
            metadataSectors.clear();
            imageHandle = 0u;
        }

        bool dispatchImport(uint16_t ordinal, IopCpuState &cpu)
        {
            const uint32_t a0 = cpu.gpr[4];
            const uint32_t a1 = cpu.gpr[5];
            const uint32_t a2 = cpu.gpr[6];

            switch (ordinal)
            {
            case 4: // sceCdInit
                initialized = a0 != kCdvdInitExit;
                if (initialized)
                {
                    callback = {};
                    completionCallback.reset();
                }
                lastError = kCdvdErrorNone;
                cpu.gpr[2] = 1u;
                return true;

            case 5: // sceCdStandby
                cpu.gpr[2] = 1u;
                return true;

            case 6: // sceCdRead
                if (readSectors(a0, a1, a2))
                {
                    if (callback.address != 0u)
                    {
                        completionCallback = CompletionCallback{
                            callback.address,
                            callback.gp,
                            kCdvdCallbackRead,
                        };
                    }
                    cpu.gpr[2] = 1u;
                }
                else
                    cpu.gpr[2] = 0u;
                return true;

            case 7: // sceCdSeek
                currentLsn = a0;
                lastError = kCdvdErrorNone;
                if (callback.address != 0u)
                {
                    completionCallback = CompletionCallback{
                        callback.address,
                        callback.gp,
                        kCdvdCallbackSeek,
                    };
                }
                cpu.gpr[2] = 1u;
                return true;

            case 8: // sceCdGetError
                cpu.gpr[2] = lastError;
                return true;

            case 11: // sceCdSync
                cpu.gpr[2] = 0u;
                return true;

            case 12: // sceCdGetDiskType
                cpu.gpr[2] = kCdvdTypePs2Dvd;
                return true;

            case 13: // sceCdDiskReady
                cpu.gpr[2] = kCdvdReadyComplete;
                return true;

            case 28: // sceCdStatus
                cpu.gpr[2] = kCdvdStatusPause;
                return true;

            case 37: // sceCdCallback
            {
                const uint32_t previous = callback.address;
                callback = {a0, cpu.gpr[28]};
                cpu.gpr[2] = previous;
                return true;
            }

            case 75: // sceCdMmode
                mediaMode = a0;
                cpu.gpr[2] = 1u;
                return true;

            default:
                return false;
            }
        }

        std::optional<CompletionCallback> takeCompletionCallback() noexcept
        {
            std::optional<CompletionCallback> result = completionCallback;
            completionCallback.reset();
            return result;
        }

    private:
        void closeFiles()
        {
            if (imageHandle != 0u)
                host.closeHostFile(imageHandle);
            imageHandle = 0u;
            for (IsoNode &node : nodes)
            {
                if (node.handle != 0u)
                    host.closeHostFile(node.handle);
                node.handle = 0u;
            }
        }

        bool addDirectory(size_t parent, const std::filesystem::path &path)
        {
            std::error_code error;
            std::vector<std::filesystem::directory_entry> entries;
            for (
                std::filesystem::directory_iterator iterator(path, std::filesystem::directory_options::skip_permission_denied, error),
                end;
                !error && iterator != end;
                iterator.increment(error))
            {
                const std::filesystem::directory_entry &entry = *iterator;
                if (entry.is_symlink(error))
                {
                    error.clear();
                    continue;
                }
                error.clear();
                if (entry.is_directory(error) || entry.is_regular_file(error))
                    entries.push_back(entry);
                error.clear();
            }

            std::sort(entries.begin(), entries.end(),
                      [](const auto &lhs, const auto &rhs)
                      {
                          return isoName(lhs.path(), lhs.is_directory()) < isoName(rhs.path(), rhs.is_directory());
                      });

            for (const auto &entry : entries)
            {
                error.clear();
                const bool directory = entry.is_directory(error);
                if (error)
                    continue;
                IsoNode node;
                node.hostPath = entry.path();
                node.identifier = isoName(entry.path(), directory);
                node.parent = parent;
                node.directory = directory;
                if (!directory)
                {
                    const uint64_t fileSize = entry.file_size(error);
                    if (error)
                        continue;
                    node.size = static_cast<uint32_t>(std::min<uint64_t>(fileSize, std::numeric_limits<uint32_t>::max()));
                }
                const size_t index = nodes.size();
                nodes.push_back(std::move(node));
                nodes[parent].children.push_back(index);
                if (directory && !addDirectory(index, entry.path()))
                    return false;
            }
            return true;
        }

        bool buildVirtualIso()
        {
            if (virtualIsoBuilt)
                return virtualIsoValid;
            virtualIsoBuilt = true;

            const std::string rootValue = host.hostPath(HostPathKind::CdRoot);
            if (rootValue.empty())
                return false;
            const std::filesystem::path rootPath(rootValue);
            std::error_code error;
            if (!std::filesystem::is_directory(rootPath, error) || error)
                return false;

            nodes.clear();
            IsoNode root;
            root.hostPath = rootPath;
            root.identifier.clear();
            root.parent = 0u;
            root.directory = true;
            nodes.push_back(std::move(root));
            if (!addDirectory(0u, rootPath))
                return false;

            for (IsoNode &node : nodes)
            {
                if (!node.directory)
                    continue;
                std::vector<size_t> identifierSizes = {1u, 1u};
                identifierSizes.reserve(node.children.size() + 2u);
                for (const size_t child : node.children)
                    identifierSizes.push_back(nodes[child].identifier.size());
                node.size = directoryBytesFor(identifierSizes);
                node.sectors = node.size / kSectorSize;
            }

            uint32_t cursor = kFirstDirectoryLsn;
            for (IsoNode &node : nodes)
            {
                if (!node.directory)
                    continue;
                node.lsn = cursor;
                cursor += node.sectors;
            }
            for (IsoNode &node : nodes)
            {
                if (node.directory)
                    continue;
                node.lsn = cursor;
                node.sectors = alignSectors(node.size);
                cursor += node.sectors;
            }
            volumeSectors = std::max<uint32_t>(cursor, 32u);

            std::array<uint8_t, kSectorSize> primary{};
            primary[0] = 1u;
            std::memcpy(primary.data() + 1u, "CD001", 5u);
            primary[6] = 1u;
            std::memset(primary.data() + 8u, ' ', 32u);
            std::memcpy(primary.data() + 8u, "PS2XRECOMP", 10u);
            std::memset(primary.data() + 40u, ' ', 32u);
            std::memcpy(primary.data() + 40u, "PS2X VIRTUAL DISC", 17u);
            writeBoth32(primary.data() + 80u, volumeSectors);
            writeBoth16(primary.data() + 120u, 1u);
            writeBoth16(primary.data() + 124u, 1u);
            writeBoth16(primary.data() + 128u, static_cast<uint16_t>(kSectorSize));
            const uint8_t rootIdentifier = 0u;
            (void)writeDirectoryRecord(primary.data() + 156u, nodes[0].lsn, nodes[0].size, true, &rootIdentifier, 1u);
            primary[881] = 1u;
            metadataSectors[kPrimaryVolumeDescriptorLsn] = primary;

            std::array<uint8_t, kSectorSize> terminator{};
            terminator[0] = 255u;
            std::memcpy(terminator.data() + 1u, "CD001", 5u);
            terminator[6] = 1u;
            metadataSectors[kVolumeDescriptorTerminatorLsn] = terminator;

            for (size_t nodeIndex = 0u; nodeIndex < nodes.size(); ++nodeIndex)
            {
                const IsoNode &node = nodes[nodeIndex];
                if (!node.directory)
                    continue;
                std::vector<uint8_t> bytes(node.size, 0u);
                size_t offset = 0u;
                const auto appendRecord = [&](const IsoNode &entry, const uint8_t *identifier, size_t identifierSize)
                {
                    const size_t recordSize = directoryRecordSize(identifierSize);
                    const size_t sectorOffset = offset % kSectorSize;
                    if (sectorOffset + recordSize > kSectorSize)
                        offset += kSectorSize - sectorOffset;
                    offset += writeDirectoryRecord(bytes.data() + offset,
                                                   entry.lsn,
                                                   entry.size,
                                                   entry.directory,
                                                   identifier,
                                                   identifierSize);
                };
                const uint8_t selfIdentifier = 0u;
                const uint8_t parentIdentifier = 1u;
                appendRecord(node, &selfIdentifier, 1u);
                appendRecord(nodes[node.parent], &parentIdentifier, 1u);
                for (const size_t childIndex : node.children)
                {
                    const IsoNode &child = nodes[childIndex];
                    appendRecord(child, reinterpret_cast<const uint8_t *>(child.identifier.data()), child.identifier.size());
                }
                for (uint32_t sector = 0u; sector < node.sectors; ++sector)
                {
                    std::array<uint8_t, kSectorSize> contents{};
                    std::memcpy(contents.data(), bytes.data() + sector * kSectorSize, kSectorSize);
                    metadataSectors[node.lsn + sector] = contents;
                }
            }

            virtualIsoValid = true;
            return true;
        }

        IsoNode *fileForSector(uint32_t lsn)
        {
            for (IsoNode &node : nodes)
            {
                if (!node.directory && lsn >= node.lsn && lsn < node.lsn + node.sectors)
                    return &node;
            }
            return nullptr;
        }

        bool readVirtualSector(uint32_t lsn, uint8_t *destination)
        {
            const auto metadata = metadataSectors.find(lsn);
            if (metadata != metadataSectors.end())
            {
                std::memcpy(destination, metadata->second.data(), kSectorSize);
                return true;
            }

            IsoNode *node = fileForSector(lsn);
            if (!node)
            {
                std::memset(destination, 0, kSectorSize);
                return lsn < volumeSectors;
            }
            if (node->handle == 0u)
                node->handle = host.openHostFile(node->hostPath.string());
            if (node->handle == 0u)
                return false;

            std::memset(destination, 0, kSectorSize);
            const uint64_t offset = static_cast<uint64_t>(lsn - node->lsn) * kSectorSize;
            const size_t wanted = static_cast<size_t>(std::min<uint64_t>(kSectorSize, static_cast<uint64_t>(node->size) - offset));
            size_t bytesRead = 0u;
            return host.readHostFile(node->handle, offset, destination, wanted, bytesRead) && bytesRead == wanted;
        }

        bool readSectors(uint32_t lsn, uint32_t sectors, uint32_t destination)
        {
            if (sectors == 0u)
            {
                lastError = kCdvdErrorNone;
                return true;
            }
            const uint64_t byteCount64 = static_cast<uint64_t>(sectors) * kSectorSize;
            if (byteCount64 > IopMemory::RamSize || !memory.ownsRamRange(destination, static_cast<size_t>(byteCount64)))
            {
                lastError = kCdvdErrorRead;
                return false;
            }
            const size_t byteCount = static_cast<size_t>(byteCount64);
            std::vector<uint8_t> bytes(byteCount, 0u);

            bool read = false;
            const std::string imagePath = host.hostPath(HostPathKind::CdImage);
            if (!imagePath.empty())
            {
                if (imageHandle == 0u)
                    imageHandle = host.openHostFile(imagePath);
                if (imageHandle != 0u)
                {
                    size_t bytesRead = 0u;
                    read = host.readHostFile(imageHandle,
                                             static_cast<uint64_t>(lsn) * kSectorSize,
                                             bytes.data(),
                                             byteCount,
                                             bytesRead) &&
                           bytesRead == byteCount;
                }
            }

            if (!read && buildVirtualIso())
            {
                read = true;
                for (uint32_t sector = 0u; sector < sectors; ++sector)
                {
                    if (!readVirtualSector(lsn + sector, bytes.data() + static_cast<size_t>(sector) * kSectorSize))
                    {
                        read = false;
                        break;
                    }
                }
            }

            if (!read || !memory.writeRam(destination, bytes.data(), bytes.size()))
            {
                lastError = kCdvdErrorRead;
                return false;
            }
            lastError = kCdvdErrorNone;
            return true;
        }

        IopHost &host;
        IopMemory &memory;
        Callback callback;
        std::optional<CompletionCallback> completionCallback;
        bool initialized = false;
        uint32_t mediaMode = 0u;
        uint32_t currentLsn = 0u;
        uint32_t lastError = kCdvdErrorNone;
        uint64_t imageHandle = 0u;
        bool virtualIsoBuilt = false;
        bool virtualIsoValid = false;
        uint32_t volumeSectors = 0u;
        std::vector<IsoNode> nodes;
        std::unordered_map<uint32_t, std::array<uint8_t, kSectorSize>> metadataSectors;
    };

    IopCdvd::IopCdvd(IopHost &host, IopMemory &memory)
        : m_impl(std::make_unique<Impl>(host, memory))
    {
    }

    IopCdvd::~IopCdvd() = default;

    void IopCdvd::reset() noexcept
    {
        m_impl->reset();
    }

    bool IopCdvd::dispatchImport(uint16_t ordinal, IopCpuState &cpu)
    {
        return m_impl->dispatchImport(ordinal, cpu);
    }

    std::optional<IopCdvd::CompletionCallback> IopCdvd::takeCompletionCallback() noexcept
    {
        return m_impl->takeCompletionCallback();
    }
}
