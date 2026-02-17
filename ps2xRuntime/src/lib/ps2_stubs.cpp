#include "ps2_stubs.h"
#include "ps2_runtime.h"
#include "ps2_syscalls.h"
#include <iostream>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <mutex>

#ifndef PS2_CD_REMAP_IDX_TO_AFS
#define PS2_CD_REMAP_IDX_TO_AFS 1
#endif

namespace
{
    constexpr uint32_t kCdSectorSize = 2048;
    constexpr uint32_t kCdPseudoLbnStart = 0x00100000;

    struct CdFileEntry
    {
        std::filesystem::path hostPath;
        uint32_t sizeBytes = 0;
        uint32_t baseLbn = 0;
        uint32_t sectors = 0;
    };

    std::unordered_map<std::string, CdFileEntry> g_cdFilesByKey;
    std::unordered_map<std::string, std::filesystem::path> g_cdLeafIndex;
    std::filesystem::path g_cdLeafIndexRoot;
    bool g_cdLeafIndexBuilt = false;
    uint32_t g_nextPseudoLbn = kCdPseudoLbnStart;
    int32_t g_lastCdError = 0;
    uint32_t g_cdMode = 0;
    uint32_t g_cdStreamingLbn = 0;
    bool g_cdInitialized = false;

    constexpr uint32_t kIopHeapBase = 0x01A00000;
    constexpr uint32_t kIopHeapLimit = 0x01F00000;
    constexpr uint32_t kIopHeapAlign = 16;
    uint32_t g_iopHeapNext = kIopHeapBase;

    std::string toLowerAscii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    std::string stripIsoVersionSuffix(std::string value)
    {
        const std::size_t semicolon = value.find(';');
        if (semicolon == std::string::npos)
        {
            return value;
        }

        bool numericSuffix = semicolon + 1 < value.size();
        for (std::size_t i = semicolon + 1; i < value.size(); ++i)
        {
            if (!std::isdigit(static_cast<unsigned char>(value[i])))
            {
                numericSuffix = false;
                break;
            }
        }

        if (numericSuffix)
        {
            value.erase(semicolon);
        }
        return value;
    }

    std::string normalizePathSeparators(std::string value)
    {
        std::replace(value.begin(), value.end(), '\\', '/');
        return value;
    }

    void trimLeadingSeparators(std::string &value)
    {
        while (!value.empty() && (value.front() == '/' || value.front() == '\\'))
        {
            value.erase(value.begin());
        }
    }

    std::string normalizeCdPathNoPrefix(std::string path)
    {
        path = normalizePathSeparators(std::move(path));
        std::string lower = toLowerAscii(path);
        if (lower.rfind("cdrom0:", 0) == 0)
        {
            path = path.substr(7);
        }
        else if (lower.rfind("cdrom:", 0) == 0)
        {
            path = path.substr(6);
        }

        trimLeadingSeparators(path);
        while (!path.empty() && std::isspace(static_cast<unsigned char>(path.front())))
        {
            path.erase(path.begin());
        }
        while (!path.empty() && std::isspace(static_cast<unsigned char>(path.back())))
        {
            path.pop_back();
        }
        path = stripIsoVersionSuffix(std::move(path));
        return path;
    }

    std::filesystem::path getCdRootPath()
    {
        const PS2Runtime::IoPaths &paths = PS2Runtime::getIoPaths();
        if (!paths.cdRoot.empty())
        {
            return paths.cdRoot;
        }
        if (!paths.elfDirectory.empty())
        {
            return paths.elfDirectory;
        }

        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        return ec ? std::filesystem::path(".") : cwd.lexically_normal();
    }

    std::filesystem::path getCdImagePath()
    {
        return PS2Runtime::getIoPaths().cdImage;
    }

    uint32_t sectorsForBytes(uint64_t byteCount)
    {
        const uint64_t sectors = (byteCount + (kCdSectorSize - 1)) / kCdSectorSize;
        return sectors > 0 ? static_cast<uint32_t>(sectors) : 1;
    }

    std::string cdPathKey(const std::string &ps2Path)
    {
        return toLowerAscii(normalizeCdPathNoPrefix(ps2Path));
    }

    std::filesystem::path cdHostPath(const std::string &ps2Path)
    {
        const std::string normalized = normalizeCdPathNoPrefix(ps2Path);
        std::filesystem::path resolved = getCdRootPath();
        if (!normalized.empty())
        {
            resolved /= std::filesystem::path(normalized);
        }
        return resolved.lexically_normal();
    }

    bool resolveCaseInsensitivePath(const std::filesystem::path &root,
                                    const std::filesystem::path &relative,
                                    std::filesystem::path &resolvedOut)
    {
        std::filesystem::path current = root;
        for (const auto &component : relative)
        {
            const std::filesystem::path direct = current / component;
            std::error_code ec;
            if (std::filesystem::exists(direct, ec) && !ec)
            {
                current = direct;
                continue;
            }

            bool matched = false;
            const std::string needle = toLowerAscii(component.string());
            std::error_code iterEc;
            for (const auto &entry : std::filesystem::directory_iterator(current, iterEc))
            {
                if (iterEc)
                {
                    break;
                }

                const std::string candidate = toLowerAscii(entry.path().filename().string());
                if (candidate == needle)
                {
                    current = entry.path();
                    matched = true;
                    break;
                }
            }

            if (!matched)
            {
                return false;
            }
        }

        std::error_code fileEc;
        if (std::filesystem::is_regular_file(current, fileEc) && !fileEc)
        {
            resolvedOut = current;
            return true;
        }
        return false;
    }

    void ensureCdLeafIndex(const std::filesystem::path &root)
    {
        if (g_cdLeafIndexBuilt && g_cdLeafIndexRoot == root)
        {
            return;
        }

        g_cdLeafIndex.clear();
        g_cdLeafIndexRoot = root;
        g_cdLeafIndexBuilt = true;

        std::error_code ec;
        if (!std::filesystem::exists(root, ec) || ec)
        {
            return;
        }

        for (const auto &entry : std::filesystem::recursive_directory_iterator(
                 root, std::filesystem::directory_options::skip_permission_denied, ec))
        {
            if (ec)
            {
                break;
            }
            if (!entry.is_regular_file())
            {
                continue;
            }

            const std::string leaf = toLowerAscii(entry.path().filename().string());
            g_cdLeafIndex.emplace(leaf, entry.path());
        }
    }

    bool registerCdFile(const std::string &ps2Path, CdFileEntry &entryOut)
    {
        const std::string key = cdPathKey(ps2Path);
        if (key.empty())
        {
            g_lastCdError = -1;
            return false;
        }

        auto existing = g_cdFilesByKey.find(key);
        if (existing != g_cdFilesByKey.end())
        {
            entryOut = existing->second;
            g_lastCdError = 0;
            return true;
        }

        const std::filesystem::path root = getCdRootPath();
        std::filesystem::path path = cdHostPath(ps2Path);
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec || !std::filesystem::is_regular_file(path, ec))
        {
            const std::filesystem::path relative(normalizeCdPathNoPrefix(ps2Path));
            std::filesystem::path resolvedCasePath;
            if (resolveCaseInsensitivePath(root, relative, resolvedCasePath))
            {
                path = resolvedCasePath;
                ec.clear();
            }
            else
            {
                ensureCdLeafIndex(root);
                const std::string leaf = toLowerAscii(relative.filename().string());
                auto it = g_cdLeafIndex.find(leaf);
                if (it != g_cdLeafIndex.end())
                {
                    path = it->second;
                    ec.clear();
                }
                else
                {
                    g_lastCdError = -1;
                    return false;
                }
            }
        }

        const uint64_t sizeBytes = std::filesystem::file_size(path, ec);
        if (ec)
        {
            g_lastCdError = -1;
            return false;
        }

        CdFileEntry entry;
        entry.hostPath = path;
        entry.sizeBytes = static_cast<uint32_t>(std::min<uint64_t>(sizeBytes, 0xFFFFFFFFu));
        entry.baseLbn = g_nextPseudoLbn;
        entry.sectors = sectorsForBytes(sizeBytes);

        g_nextPseudoLbn += entry.sectors + 1;
        g_cdFilesByKey.emplace(key, entry);
        entryOut = entry;
        g_lastCdError = 0;
        return true;
    }

    bool readHostRange(const std::filesystem::path &path, uint64_t offsetBytes, uint8_t *dst, size_t byteCount)
    {
        if (!dst)
        {
            g_lastCdError = -1;
            return false;
        }
        if (byteCount == 0)
        {
            g_lastCdError = 0;
            return true;
        }

        std::memset(dst, 0, byteCount);
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            g_lastCdError = -1;
            return false;
        }

        file.seekg(static_cast<std::streamoff>(offsetBytes), std::ios::beg);
        if (!file.good())
        {
            g_lastCdError = -1;
            return false;
        }

        file.read(reinterpret_cast<char *>(dst), static_cast<std::streamsize>(byteCount));
        g_lastCdError = 0;
        return true;
    }

    bool readCdSectors(uint32_t lbn, uint32_t sectors, uint8_t *dst, size_t byteCount)
    {
        for (const auto &[key, entry] : g_cdFilesByKey)
        {
            const uint32_t endLbn = entry.baseLbn + entry.sectors;
            if (lbn < entry.baseLbn || lbn >= endLbn)
            {
                continue;
            }

            const uint64_t relativeLbn = static_cast<uint64_t>(lbn - entry.baseLbn);
            const uint64_t offset = relativeLbn * kCdSectorSize;
            return readHostRange(entry.hostPath, offset, dst, byteCount);
        }

        const std::filesystem::path cdImage = getCdImagePath();
        if (!cdImage.empty())
        {
            const uint64_t offset = static_cast<uint64_t>(lbn) * kCdSectorSize;
            return readHostRange(cdImage, offset, dst, byteCount);
        }

        std::cerr << "sceCdRead unresolved LBN 0x" << std::hex << lbn
                  << " sectors=" << std::dec << sectors
                  << " (no mapped file and no configured CD image)" << std::endl;
        g_lastCdError = -1;
        return false;
    }

    bool writeCdSearchResult(uint8_t *rdram, uint32_t fileAddr, const std::string &ps2Path, const CdFileEntry &entry)
    {
        // sceCdlFILE layout: u32 lsn, u32 size, char name[16], u8 date[8]
        uint8_t *fileStruct = getMemPtr(rdram, fileAddr);
        if (!fileStruct)
        {
            return false;
        }

        std::array<uint8_t, 32> packed{};
        std::memcpy(packed.data() + 0, &entry.baseLbn, sizeof(entry.baseLbn));
        std::memcpy(packed.data() + 4, &entry.sizeBytes, sizeof(entry.sizeBytes));

        std::filesystem::path leafPath(normalizeCdPathNoPrefix(ps2Path));
        std::string leaf = leafPath.filename().string();
        leaf = stripIsoVersionSuffix(std::move(leaf));
        std::strncpy(reinterpret_cast<char *>(packed.data() + 8), leaf.c_str(), 15);

        std::memcpy(fileStruct, packed.data(), packed.size());
        return true;
    }

    bool hostFileHasAfsMagic(const std::filesystem::path &path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }

        char magic[4] = {};
        file.read(magic, sizeof(magic));
        if (file.gcount() < 3)
        {
            return false;
        }

        return magic[0] == 'A' && magic[1] == 'F' && magic[2] == 'S';
    }

    bool tryRemapGdInitSearchToAfs(const std::string &ps2Path,
                                   uint32_t callerRa,
                                   const CdFileEntry &foundEntry,
                                   CdFileEntry &entryOut,
                                   std::string &resolvedPathOut)
    {
#if !PS2_CD_REMAP_IDX_TO_AFS
        {
            return false;
        }
#endif

        if (callerRa != 0x2d9444u)
        {
            return false;
        }

        std::filesystem::path relative(normalizeCdPathNoPrefix(ps2Path));
        const std::string ext = toLowerAscii(relative.extension().string());
        const std::string leaf = toLowerAscii(relative.filename().string());

        if (ext == ".idx")
        {
            if (foundEntry.sizeBytes > (kCdSectorSize * 8u))
            {
                return false;
            }

            std::filesystem::path afsRelative = relative;
            afsRelative.replace_extension(".AFS");

            CdFileEntry afsEntry;
            if (!registerCdFile(afsRelative.generic_string(), afsEntry))
            {
                return false;
            }
            if (!hostFileHasAfsMagic(afsEntry.hostPath))
            {
                return false;
            }

            entryOut = afsEntry;
            resolvedPathOut = afsRelative.generic_string();
            return true;
        }

        return false;
    }

    uint8_t toBcd(uint32_t value)
    {
        const uint32_t clamped = value % 100;
        return static_cast<uint8_t>(((clamped / 10) << 4) | (clamped % 10));
    }

    uint32_t fromBcd(uint8_t value)
    {
        return static_cast<uint32_t>(((value >> 4) & 0x0F) * 10 + (value & 0x0F));
    }

    std::unordered_map<uint32_t, FILE *> g_file_map;
    uint32_t g_next_file_handle = 1; // Start file handles > 0 (0 is NULL)
    std::mutex g_file_mutex;

    uint32_t generate_file_handle()
    {
        uint32_t handle = 0;
        do
        {
            handle = g_next_file_handle++;
            if (g_next_file_handle == 0)
                g_next_file_handle = 1;
        } while (handle == 0 || g_file_map.count(handle));
        return handle;
    }

    FILE *get_file_ptr(uint32_t handle)
    {
        if (handle == 0)
            return nullptr;
        std::lock_guard<std::mutex> lock(g_file_mutex);
        auto it = g_file_map.find(handle);
        return (it != g_file_map.end()) ? it->second : nullptr;
    }
}

namespace
{
    // convert a host pointer within rdram back to a PS2 address
    uint32_t hostPtrToPs2Addr(uint8_t *rdram, const void *hostPtr)
    {
        if (!hostPtr)
            return 0; // Handle NULL pointer case

        const uint8_t *ptr_u8 = static_cast<const uint8_t *>(hostPtr);
        std::ptrdiff_t offset = ptr_u8 - rdram;

        // Check if is in rdram range
        if (offset >= 0 && static_cast<size_t>(offset) < PS2_RAM_SIZE)
        {
            return PS2_RAM_BASE + static_cast<uint32_t>(offset);
        }
        else
        {
            std::cerr << "Warning: hostPtrToPs2Addr failed - host pointer " << hostPtr << " is outside rdram range [" << static_cast<void *>(rdram) << ", " << static_cast<void *>(rdram + PS2_RAM_SIZE) << ")" << std::endl;
            return 0;
        }
    }
}

namespace
{
    bool tryReadWordFromRdram(uint8_t *rdram, uint32_t addr, uint32_t &outWord)
    {
        const uint8_t *ptr = getConstMemPtr(rdram, addr);
        if (!ptr)
        {
            return false;
        }
        std::memcpy(&outWord, ptr, sizeof(outWord));
        return true;
    }

    bool tryReadWordFromGuest(uint8_t *rdram, PS2Runtime *runtime, uint32_t addr, uint32_t &outWord)
    {
        if (tryReadWordFromRdram(rdram, addr, outWord))
        {
            return true;
        }

        if (runtime)
        {
            try
            {
                PS2Memory &mem = runtime->memory();
                outWord = static_cast<uint32_t>(mem.read8(addr + 0u)) |
                          (static_cast<uint32_t>(mem.read8(addr + 1u)) << 8u) |
                          (static_cast<uint32_t>(mem.read8(addr + 2u)) << 16u) |
                          (static_cast<uint32_t>(mem.read8(addr + 3u)) << 24u);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }
        return false;
    }

    bool tryReadByteFromGuest(uint8_t *rdram, PS2Runtime *runtime, uint32_t addr, uint8_t &outByte)
    {
        const uint8_t *chPtr = getConstMemPtr(rdram, addr);
        if (chPtr)
        {
            outByte = *chPtr;
            return true;
        }

        if (runtime)
        {
            try
            {
                outByte = runtime->memory().read8(addr);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }
        return false;
    }

    bool writeGuestBytes(uint8_t *rdram, PS2Runtime *runtime, uint32_t addr, const uint8_t *src, size_t len)
    {
        if (!src || len == 0)
        {
            return true;
        }

        bool allViaPtrs = true;
        for (size_t i = 0; i < len; ++i)
        {
            const uint64_t guestAddr = static_cast<uint64_t>(addr) + i;
            if (guestAddr > 0xFFFFFFFFull)
            {
                return false;
            }
            uint8_t *dst = getMemPtr(rdram, static_cast<uint32_t>(guestAddr));
            if (!dst)
            {
                allViaPtrs = false;
                break;
            }
            *dst = src[i];
        }
        if (allViaPtrs)
        {
            return true;
        }

        if (runtime)
        {
            try
            {
                PS2Memory &mem = runtime->memory();
                for (size_t i = 0; i < len; ++i)
                {
                    const uint64_t guestAddr = static_cast<uint64_t>(addr) + i;
                    if (guestAddr > 0xFFFFFFFFull)
                    {
                        return false;
                    }
                    mem.write8(static_cast<uint32_t>(guestAddr), src[i]);
                }
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        return false;
    }

    std::string readPs2CStringBounded(uint8_t *rdram, PS2Runtime *runtime, uint32_t addr, size_t maxLen = 512)
    {
        std::string out;
        if (addr == 0 || maxLen == 0)
        {
            return out;
        }

        out.reserve(std::min<size_t>(maxLen, 128));
        for (size_t i = 0; i < maxLen; ++i)
        {
            const uint64_t guestAddr = static_cast<uint64_t>(addr) + i;
            if (guestAddr > 0xFFFFFFFFull)
            {
                break;
            }

            uint8_t chByte = 0;
            if (!tryReadByteFromGuest(rdram, runtime, static_cast<uint32_t>(guestAddr), chByte))
            {
                break;
            }

            const char ch = static_cast<char>(chByte);
            if (ch == '\0')
            {
                break;
            }
            out.push_back(ch);
        }

        return out;
    }

    std::string readPs2CStringBounded(uint8_t *rdram, uint32_t addr, size_t maxLen = 512)
    {
        return readPs2CStringBounded(rdram, nullptr, addr, maxLen);
    }

    std::string sanitizeForLog(const std::string &value)
    {
        std::string out;
        out.reserve(value.size());
        for (unsigned char ch : value)
        {
            if (ch == '\n' || ch == '\r' || ch == '\t' || (ch >= 0x20 && ch < 0x7F))
            {
                out.push_back(static_cast<char>(ch));
            }
            else
            {
                out.push_back('.');
            }
        }
        return out;
    }

    class Ps2VarArgCursor
    {
    public:
        Ps2VarArgCursor(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int fixedArgs)
            : m_rdram(rdram),
              m_ctx(ctx),
              m_runtime(runtime),
              m_fixedArgs(fixedArgs),
              m_stackBase(getRegU32(ctx, 29) + 0x10)
        {
            if (m_fixedArgs < 0)
            {
                m_fixedArgs = 0;
            }
            m_slotIndex = static_cast<uint32_t>(m_fixedArgs);
        }

        uint32_t nextU32()
        {
            const uint32_t value = readWordAtSlot(m_slotIndex);
            ++m_slotIndex;
            return value;
        }

        uint64_t nextU64()
        {
            // O32 ABI aligns 64-bit variadic values on even 32-bit slots.
            if ((m_slotIndex & 1u) != 0u)
            {
                ++m_slotIndex;
            }
            const uint64_t low = readWordAtSlot(m_slotIndex);
            const uint64_t high = readWordAtSlot(m_slotIndex + 1u);
            m_slotIndex += 2u;
            return low | (high << 32);
        }

    private:
        uint32_t readWordAtSlot(uint32_t slotIndex) const
        {
            if (slotIndex < 4u)
            {
                // slot0..slot3 -> a0..a3 (r4..r7)
                return getRegU32(m_ctx, 4 + static_cast<int>(slotIndex));
            }

            const uint32_t stackIndex = slotIndex - 4u;
            const uint32_t stackAddr = m_stackBase + stackIndex * 4u;
            uint32_t value = 0;
            (void)tryReadWordFromGuest(m_rdram, m_runtime, stackAddr, value);
            return value;
        }

        uint8_t *m_rdram;
        R5900Context *m_ctx;
        PS2Runtime *m_runtime;
        int m_fixedArgs;
        uint32_t m_stackBase;
        uint32_t m_slotIndex = 0;
    };

    class Ps2VaListCursor
    {
    public:
        Ps2VaListCursor(uint8_t *rdram, PS2Runtime *runtime, uint32_t vaListAddr)
            : m_rdram(rdram), m_runtime(runtime), m_curr(vaListAddr)
        {
        }

        uint32_t nextU32()
        {
            uint32_t value = 0;
            (void)tryReadWordFromGuest(m_rdram, m_runtime, m_curr, value);
            m_curr += 4;
            return value;
        }

        uint64_t nextU64()
        {
            m_curr = (m_curr + 7u) & ~7u;
            const uint64_t low = nextU32();
            const uint64_t high = nextU32();
            return low | (high << 32);
        }

    private:
        uint8_t *m_rdram;
        PS2Runtime *m_runtime;
        uint32_t m_curr = 0;
    };

    template <typename NextU32Fn, typename NextU64Fn, typename ReadStringFn>
    std::string formatPs2StringCore(uint8_t *rdram, const char *format, NextU32Fn nextU32, NextU64Fn nextU64, ReadStringFn readString)
    {
        if (!format)
        {
            return {};
        }

        std::string out;
        out.reserve(std::strlen(format) + 32);
        const char *p = format;

        while (*p)
        {
            if (*p != '%')
            {
                out.push_back(*p++);
                continue;
            }

            const char *specStart = p++;
            if (*p == '%')
            {
                out.push_back('%');
                ++p;
                continue;
            }

            int parsedWidth = -1;
            int parsedPrecision = -1;

            while (*p && std::strchr("-+ #0", *p))
            {
                ++p;
            }

            if (*p == '*')
            {
                parsedWidth = static_cast<int32_t>(nextU32());
                ++p;
            }
            else
            {
                if (*p && std::isdigit(static_cast<unsigned char>(*p)))
                {
                    parsedWidth = 0;
                }
                while (*p && std::isdigit(static_cast<unsigned char>(*p)))
                {
                    parsedWidth = (parsedWidth * 10) + (*p - '0');
                    ++p;
                }
            }

            if (*p == '.')
            {
                ++p;
                if (*p == '*')
                {
                    parsedPrecision = static_cast<int32_t>(nextU32());
                    ++p;
                }
                else
                {
                    parsedPrecision = 0;
                    while (*p && std::isdigit(static_cast<unsigned char>(*p)))
                    {
                        parsedPrecision = (parsedPrecision * 10) + (*p - '0');
                        ++p;
                    }
                }
            }
            if (parsedPrecision < 0)
            {
                parsedPrecision = -1;
            }
            (void)parsedWidth;

            enum class LengthMod
            {
                None,
                H,
                HH,
                L,
                LL,
                J,
                Z,
                T,
                BigL
            };

            LengthMod length = LengthMod::None;
            if (*p == 'h')
            {
                ++p;
                if (*p == 'h')
                {
                    ++p;
                    length = LengthMod::HH;
                }
                else
                {
                    length = LengthMod::H;
                }
            }
            else if (*p == 'l')
            {
                ++p;
                if (*p == 'l')
                {
                    ++p;
                    length = LengthMod::LL;
                }
                else
                {
                    length = LengthMod::L;
                }
            }
            else if (*p == 'j')
            {
                ++p;
                length = LengthMod::J;
            }
            else if (*p == 'z')
            {
                ++p;
                length = LengthMod::Z;
            }
            else if (*p == 't')
            {
                ++p;
                length = LengthMod::T;
            }
            else if (*p == 'L')
            {
                ++p;
                length = LengthMod::BigL;
            }

            if (*p == '\0')
            {
                out.append(specStart);
                break;
            }

            const bool use64Integer = (length == LengthMod::LL || length == LengthMod::J);
            auto readUnsignedInteger = [&]() -> uint64_t
            {
                return use64Integer ? nextU64() : static_cast<uint64_t>(nextU32());
            };
            auto readSignedInteger = [&]() -> int64_t
            {
                if (use64Integer)
                {
                    return static_cast<int64_t>(nextU64());
                }
                return static_cast<int64_t>(static_cast<int32_t>(nextU32()));
            };

            const char spec = *p++;
            switch (spec)
            {
            case 's':
            {
                const uint32_t strAddr = nextU32();
                if (strAddr == 0)
                {
                    out.append("(null)");
                }
                else
                {
                    std::string str = readString(strAddr);
                    if (parsedPrecision >= 0 &&
                        str.size() > static_cast<size_t>(parsedPrecision))
                    {
                        str.resize(static_cast<size_t>(parsedPrecision));
                    }
                    out.append(str);
                }
                break;
            }
            case 'c':
            {
                const char ch = static_cast<char>(nextU32() & 0xFF);
                out.push_back(ch);
                break;
            }
            case 'd':
            case 'i':
                out.append(std::to_string(readSignedInteger()));
                break;
            case 'u':
                out.append(std::to_string(readUnsignedInteger()));
                break;
            case 'x':
            case 'X':
            {
                std::ostringstream ss;
                if (spec == 'X')
                {
                    ss.setf(std::ios::uppercase);
                }
                ss << std::hex << readUnsignedInteger();
                out.append(ss.str());
                break;
            }
            case 'o':
            {
                std::ostringstream ss;
                ss << std::oct << readUnsignedInteger();
                out.append(ss.str());
                break;
            }
            case 'p':
            {
                std::ostringstream ss;
                ss << "0x" << std::hex << nextU32();
                out.append(ss.str());
                break;
            }
            case 'f':
            case 'F':
            case 'e':
            case 'E':
            case 'g':
            case 'G':
            case 'a':
            case 'A':
            {
                const uint64_t bits = nextU64();
                double value = 0.0;
                std::memcpy(&value, &bits, sizeof(value));
                char numBuf[128];
                std::snprintf(numBuf, sizeof(numBuf), "%g", value);
                out.append(numBuf);
                break;
            }
            case 'n':
            {
                // Avoid arbitrary guest memory mutation through %n in stub formatting.
                (void)nextU32();
                break;
            }
            default:
                out.append(specStart, p - specStart);
                break;
            }
        }

        return out;
    }

    std::string formatPs2StringWithArgs(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, const char *format, int fixedArgs)
    {
        Ps2VarArgCursor cursor(rdram, ctx, runtime, fixedArgs);
        return formatPs2StringCore(
            rdram,
            format,
            [&cursor]()
            { return cursor.nextU32(); },
            [&cursor]()
            { return cursor.nextU64(); },
            [rdram, runtime](uint32_t addr)
            { return readPs2CStringBounded(rdram, runtime, addr); });
    }

    std::string formatPs2StringWithVaList(uint8_t *rdram, PS2Runtime *runtime, const char *format, uint32_t vaListAddr)
    {
        Ps2VaListCursor cursor(rdram, runtime, vaListAddr);
        return formatPs2StringCore(
            rdram,
            format,
            [&cursor]()
            { return cursor.nextU32(); },
            [&cursor]()
            { return cursor.nextU64(); },
            [rdram, runtime](uint32_t addr)
            { return readPs2CStringBounded(rdram, runtime, addr); });
    }

    constexpr uint32_t kMaxStubWarningsPerName = 8;
    std::unordered_map<std::string, uint32_t> g_stubWarningCount;
    std::mutex g_stubWarningMutex;
    constexpr uint32_t kMaxPrintfLogs = 200;
    constexpr size_t kMaxFormattedOutputBytes = 4096;
    uint32_t g_printfLogCount = 0;
    std::mutex g_printfLogMutex;

    constexpr std::array<uint32_t, 10> kDmaChannelBases = {
        0x10008000u, 0x10009000u, 0x1000A000u, 0x1000B000u, 0x1000B400u,
        0x1000C000u, 0x1000C400u, 0x1000C800u, 0x1000D000u, 0x1000D400u};
    std::mutex g_dmaStubMutex;
    std::unordered_map<uint32_t, uint32_t> g_dmaPendingPolls;
    uint32_t g_dmaStubLogCount = 0;
    constexpr uint32_t kMaxDmaStubLogs = 64;

    bool isKnownDmaChannelBase(uint32_t value)
    {
        return std::find(kDmaChannelBases.begin(), kDmaChannelBases.end(), value) != kDmaChannelBases.end();
    }

    uint32_t toDmaPhys(uint32_t addr)
    {
        return addr & 0x1FFFFFFFu;
    }

    uint32_t normalizeQwcFromArg(uint32_t value)
    {
        if (value == 0)
        {
            return 0;
        }
        if (value > 0xFFFFu)
        {
            return std::min<uint32_t>((value + 15u) >> 4u, 0xFFFFu);
        }
        return value & 0xFFFFu;
    }

    struct ParsedDmaTag
    {
        bool valid = false;
        uint32_t qwc = 0;
        uint32_t id = 0;
        uint32_t addr = 0;
    };

    ParsedDmaTag tryParseDmaTag(uint8_t *rdram, uint32_t guestAddr)
    {
        ParsedDmaTag out;
        if (guestAddr == 0)
        {
            return out;
        }

        const uint8_t *ptr = getConstMemPtr(rdram, guestAddr);
        if (!ptr)
        {
            return out;
        }

        uint64_t tag = 0;
        std::memcpy(&tag, ptr, sizeof(tag));
        out.valid = true;
        out.qwc = static_cast<uint32_t>(tag & 0xFFFFu);
        out.id = static_cast<uint32_t>((tag >> 28) & 0x7u);
        out.addr = static_cast<uint32_t>((tag >> 32) & 0x7FFFFFFFu);
        return out;
    }

    uint32_t resolveDmaChannelBase(uint8_t *rdram, uint32_t chanArg)
    {
        if (isKnownDmaChannelBase(chanArg))
        {
            return chanArg;
        }
        if (chanArg < kDmaChannelBases.size())
        {
            return kDmaChannelBases[chanArg];
        }

        const uint32_t masked = chanArg & 0xFFFFFF00u;
        if (isKnownDmaChannelBase(masked))
        {
            return masked;
        }

        uint32_t candidate0 = 0;
        if (!tryReadWordFromRdram(rdram, chanArg, candidate0))
        {
            return 0;
        }
        if (isKnownDmaChannelBase(candidate0))
        {
            return candidate0;
        }

        uint32_t candidate1 = 0;
        if (!tryReadWordFromRdram(rdram, chanArg + 4u, candidate1))
        {
            return 0;
        }
        if (isKnownDmaChannelBase(candidate1))
        {
            return candidate1;
        }

        return 0;
    }

    int32_t submitDmaSend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, bool preferNormalCount)
    {
        if (!runtime)
        {
            return -1;
        }

        const uint32_t chanArg = getRegU32(ctx, 4);
        const uint32_t payloadArg = getRegU32(ctx, 5);
        const uint32_t countArg = getRegU32(ctx, 6);
        const uint32_t channelBase = resolveDmaChannelBase(rdram, chanArg);
        if (channelBase == 0)
        {
            return -1;
        }

        const uint32_t payloadPhys = toDmaPhys(payloadArg);
        uint32_t madr = 0;
        uint32_t qwc = 0;
        uint32_t tadr = payloadPhys;
        uint32_t chcr = 0x00000181u; // DIR=1, TIE=1, STR=1 (normal mode).

        if (preferNormalCount)
        {
            qwc = normalizeQwcFromArg(countArg);
            madr = payloadPhys;
        }
        else
        {
            const ParsedDmaTag tag = tryParseDmaTag(rdram, payloadPhys);
            if (tag.valid && tag.qwc != 0)
            {
                qwc = tag.qwc;
                switch (tag.id)
                {
                case 0: // REFE
                case 3: // REF
                case 4: // REFS
                    madr = toDmaPhys(tag.addr);
                    break;
                default:
                    // CNT/NEXT/CALL/RET-style tags carry payload inline after the tag.
                    madr = toDmaPhys(payloadPhys + 0x10u);
                    break;
                }
            }
            else
            {
                // Fall back to chain mode so the runtime DMA path can walk TADR.
                chcr = 0x00000185u; // MODE=1 chain, DIR=1, TIE=1, STR=1.
            }
        }

        PS2Memory &mem = runtime->memory();
        mem.writeIORegister(channelBase + 0x20u, qwc & 0xFFFFu);
        mem.writeIORegister(channelBase + 0x10u, madr);
        mem.writeIORegister(channelBase + 0x30u, tadr);
        mem.writeIORegister(channelBase + 0x00u, chcr);

        std::lock_guard<std::mutex> lock(g_dmaStubMutex);
        g_dmaPendingPolls[channelBase] = 1;
        if (g_dmaStubLogCount < kMaxDmaStubLogs)
        {
            std::cout << "[sceDmaSend] ch=0x" << std::hex << channelBase
                      << " madr=0x" << madr
                      << " qwc=0x" << qwc
                      << " tadr=0x" << tadr
                      << " chcr=0x" << chcr << std::dec << std::endl;
            ++g_dmaStubLogCount;
        }

        return 0;
    }

    int32_t submitDmaSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (!runtime)
        {
            return -1;
        }

        const uint32_t chanArg = getRegU32(ctx, 4);
        const uint32_t mode = getRegU32(ctx, 5);
        const uint32_t channelBase = resolveDmaChannelBase(rdram, chanArg);
        if (channelBase == 0)
        {
            return -1;
        }

        bool modelBusy = false;
        {
            std::lock_guard<std::mutex> lock(g_dmaStubMutex);
            auto it = g_dmaPendingPolls.find(channelBase);
            if (it != g_dmaPendingPolls.end() && it->second > 0)
            {
                modelBusy = true;
                if (mode != 0)
                {
                    --it->second;
                    if (it->second == 0)
                    {
                        g_dmaPendingPolls.erase(it);
                    }
                }
                else
                {
                    // Blocking mode: complete immediately in this runtime.
                    g_dmaPendingPolls.erase(it);
                }
            }
        }

        const uint32_t chcr = runtime->memory().readIORegister(channelBase + 0x00u);
        const bool hwBusy = (chcr & 0x100u) != 0;
        return ((modelBusy || hwBusy) && mode != 0) ? 1 : 0;
    }

}

namespace
{
    struct GsGParam
    {
        uint8_t interlace;
        uint8_t omode;
        uint8_t ffmode;
        uint8_t version;
    };

    struct GsDispEnvMem
    {
        uint64_t display;
        uint64_t dispfb;
    };

    struct GsImageMem
    {
        uint16_t x;
        uint16_t y;
        uint16_t width;
        uint16_t height;
        uint16_t vram_addr;
        uint8_t vram_width;
        uint8_t psm;
    };

#pragma pack(push, 1)
    struct GsDrawEnvMem
    {
        uint16_t offset_x;
        uint16_t offset_y;
        uint16_t clip_x;
        uint16_t clip_y;
        uint16_t clip_w;
        uint16_t clip_h;
        uint16_t vram_addr;
        uint8_t fbw;
        uint8_t psm;
        uint16_t vram_x;
        uint16_t vram_y;
        uint32_t draw_mask;
        uint8_t auto_clear;
        uint8_t pad[3];
        uint8_t bg_r;
        uint8_t bg_g;
        uint8_t bg_b;
        uint8_t bg_a;
        float bg_q;
    };
#pragma pack(pop)

    static_assert(sizeof(GsImageMem) == 12, "GsImageMem size mismatch");
    static_assert(sizeof(GsDrawEnvMem) == 36, "GsDrawEnvMem size mismatch");

    constexpr uint32_t kGsParamScratchOffset = 0x100;
    GsGParam g_gparam{1, 2, 1, 3}; // Default: interlaced NTSC, frame mode.

    static uint64_t makePmode(uint32_t en1, uint32_t en2, uint32_t mmod, uint32_t amod, uint32_t slbg, uint32_t alp)
    {
        return (static_cast<uint64_t>(en1 & 1) << 0) |
               (static_cast<uint64_t>(en2 & 1) << 1) |
               (static_cast<uint64_t>(1) << 2) |
               (static_cast<uint64_t>(mmod & 1) << 5) |
               (static_cast<uint64_t>(amod & 1) << 6) |
               (static_cast<uint64_t>(slbg & 1) << 7) |
               (static_cast<uint64_t>(alp & 0xFF) << 8);
    }

    static uint64_t makeDispFb(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t dbx, uint32_t dby)
    {
        return (static_cast<uint64_t>(fbp & 0x1FF) << 0) |
               (static_cast<uint64_t>(fbw & 0x3F) << 9) |
               (static_cast<uint64_t>(psm & 0x1F) << 15) |
               (static_cast<uint64_t>(dbx & 0x7FF) << 32) |
               (static_cast<uint64_t>(dby & 0x7FF) << 43);
    }

    static uint64_t makeDisplay(uint32_t dx, uint32_t dy, uint32_t magh, uint32_t magv, uint32_t dw, uint32_t dh)
    {
        return (static_cast<uint64_t>(dx & 0x0FFF) << 0) |
               (static_cast<uint64_t>(dy & 0x07FF) << 12) |
               (static_cast<uint64_t>(magh & 0x0F) << 23) |
               (static_cast<uint64_t>(magv & 0x03) << 27) |
               (static_cast<uint64_t>(dw & 0x0FFF) << 32) |
               (static_cast<uint64_t>(dh & 0x07FF) << 44);
    }

    static uint32_t readStackU32(uint8_t *rdram, R5900Context *ctx, uint32_t offset)
    {
        uint32_t sp = getRegU32(ctx, 29);
        const uint8_t *ptr = getConstMemPtr(rdram, sp + offset);
        if (!ptr)
            return 0;
        uint32_t value = 0;
        std::memcpy(&value, ptr, sizeof(value));
        return value;
    }

    static uint32_t bytesForPixels(uint8_t psm, uint32_t pixelCount)
    {
        const uint64_t pixels = static_cast<uint64_t>(pixelCount);
        uint64_t bytes = 0;
        switch (psm)
        {
        case 0:  // PSMCT32
        case 1:  // PSMCT24 (treat as 32)
        case 27: // PSMT8H (packed in 32-bit lanes)
        case 36: // PSMT4HL (packed in 32-bit lanes)
        case 44: // PSMT4HH (packed in 32-bit lanes)
            bytes = pixels * 4ull;
            break;
        case 2:  // PSMCT16
        case 10: // PSMCT16S
            bytes = pixels * 2ull;
            break;
        case 19: // PSMT8
            bytes = pixels;
            break;
        case 20: // PSMT4
            bytes = (pixels + 1ull) / 2ull;
            break;
        default:
            bytes = pixels * 4ull;
            break;
        }
        if (bytes > 0xFFFFFFFFull)
        {
            return 0xFFFFFFFFu;
        }
        return static_cast<uint32_t>(bytes);
    }

    struct GsSetDefImageArgs
    {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t vramAddr = 0;
        uint32_t vramWidth = 0;
        uint32_t psm = 0;
    };

    static GsSetDefImageArgs decodeGsSetDefImageArgs(uint8_t *rdram, R5900Context *ctx)
    {
        GsSetDefImageArgs decoded{};

        const uint32_t reg8 = getRegU32(ctx, 8);
        const uint32_t reg9 = getRegU32(ctx, 9);
        const uint32_t reg10 = getRegU32(ctx, 10);
        const uint32_t reg11 = getRegU32(ctx, 11);

        const uint32_t stack0 = readStackU32(rdram, ctx, 16);
        const uint32_t stack1 = readStackU32(rdram, ctx, 20);
        const uint32_t stack2 = readStackU32(rdram, ctx, 24);
        const uint32_t stack3 = readStackU32(rdram, ctx, 28);

        const bool looksLikeCanonicalRegs = (reg10 != 0u || reg11 != 0u);
        const bool looksLikeCanonicalStack = (stack2 != 0u || stack3 != 0u);

        if (looksLikeCanonicalRegs || looksLikeCanonicalStack)
        {
            decoded.vramAddr = getRegU32(ctx, 5);
            decoded.vramWidth = getRegU32(ctx, 6);
            decoded.psm = getRegU32(ctx, 7);

            if (looksLikeCanonicalRegs)
            {
                decoded.x = reg8;
                decoded.y = reg9;
                decoded.width = reg10;
                decoded.height = reg11;
            }
            else
            {
                decoded.x = stack0;
                decoded.y = stack1;
                decoded.width = stack2;
                decoded.height = stack3;
            }
            return decoded;
        }

        // Legacy code
        // a1=x, a2=y, a3=w, stack/reg extension for h/vram/fbw/psm.
        decoded.x = getRegU32(ctx, 5);
        decoded.y = getRegU32(ctx, 6);
        decoded.width = getRegU32(ctx, 7);
        decoded.height = stack0 != 0u ? stack0 : reg8;
        decoded.vramAddr = stack1 != 0u ? stack1 : reg9;
        decoded.vramWidth = stack2 != 0u ? stack2 : reg10;
        decoded.psm = stack3 != 0u ? stack3 : reg11;
        return decoded;
    }

    static bool readGsImage(uint8_t *rdram, uint32_t addr, GsImageMem &out)
    {
        const uint8_t *ptr = getConstMemPtr(rdram, addr);
        if (!ptr)
            return false;
        std::memcpy(&out, ptr, sizeof(out));
        return true;
    }

    static bool writeGsImage(uint8_t *rdram, uint32_t addr, const GsImageMem &img)
    {
        uint8_t *ptr = getMemPtr(rdram, addr);
        if (!ptr)
            return false;
        std::memcpy(ptr, &img, sizeof(img));
        return true;
    }

    static bool writeGsDispEnv(uint8_t *rdram, uint32_t addr, uint64_t display, uint64_t dispfb)
    {
        uint8_t *ptr = getMemPtr(rdram, addr);
        if (!ptr)
            return false;
        GsDispEnvMem env{display, dispfb};
        std::memcpy(ptr, &env, sizeof(env));
        return true;
    }

    static bool readGsDispEnv(uint8_t *rdram, uint32_t addr, GsDispEnvMem &out)
    {
        const uint8_t *ptr = getConstMemPtr(rdram, addr);
        if (!ptr)
            return false;
        std::memcpy(&out, ptr, sizeof(out));
        return true;
    }

    static uint32_t writeGsGParamToScratch(PS2Runtime *runtime)
    {
        if (!runtime)
            return 0;
        uint8_t *scratch = runtime->memory().getScratchpad();
        if (!scratch)
            return 0;
        std::memcpy(scratch + kGsParamScratchOffset, &g_gparam, sizeof(g_gparam));
        return PS2_SCRATCHPAD_BASE + kGsParamScratchOffset;
    }
}

namespace ps2_stubs
{

    void malloc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t size = getRegU32(ctx, 4); // $a0
        const uint32_t guestAddr = runtime ? runtime->guestMalloc(size) : 0u;
        setReturnU32(ctx, guestAddr);
    }

    void free(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t guestAddr = getRegU32(ctx, 4); // $a0
        if (runtime && guestAddr != 0u)
        {
            runtime->guestFree(guestAddr);
        }
    }

    void calloc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t count = getRegU32(ctx, 4); // $a0
        const uint32_t size = getRegU32(ctx, 5);  // $a1
        const uint32_t guestAddr = runtime ? runtime->guestCalloc(count, size) : 0u;
        setReturnU32(ctx, guestAddr);
    }

    void realloc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t oldGuestAddr = getRegU32(ctx, 4); // $a0
        const uint32_t newSize = getRegU32(ctx, 5);      // $a1
        const uint32_t newGuestAddr = runtime ? runtime->guestRealloc(oldGuestAddr, newSize) : 0u;
        setReturnU32(ctx, newGuestAddr);
    }

    void memcpy(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t destAddr = getRegU32(ctx, 4); // $a0
        uint32_t srcAddr = getRegU32(ctx, 5);  // $a1
        size_t size = getRegU32(ctx, 6);       // $a2

        uint8_t *hostDest = getMemPtr(rdram, destAddr);
        const uint8_t *hostSrc = getConstMemPtr(rdram, srcAddr);

        if (hostDest && hostSrc)
        {
            ::memcpy(hostDest, hostSrc, size);
            ps2TraceGuestRangeWrite(rdram, destAddr, static_cast<uint32_t>(size), "memcpy", ctx);
        }
        else
        {
            std::cerr << "memcpy error: Attempted copy involving non-RDRAM address (or invalid RDRAM address)."
                      << " Dest: 0x" << std::hex << destAddr << " (host ptr valid: " << (hostDest != nullptr) << ")"
                      << ", Src: 0x" << srcAddr << " (host ptr valid: " << (hostSrc != nullptr) << ")" << std::dec
                      << ", Size: " << size << std::endl;
        }

        // returns dest pointer ($v0 = $a0)
        ctx->r[2] = ctx->r[4];
    }

    void memset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t destAddr = getRegU32(ctx, 4);       // $a0
        int value = (int)(getRegU32(ctx, 5) & 0xFF); // $a1 (char value)
        uint32_t size = getRegU32(ctx, 6);           // $a2

        uint8_t *hostDest = getMemPtr(rdram, destAddr);

        if (hostDest)
        {
            ::memset(hostDest, value, size);
            ps2TraceGuestRangeWrite(rdram, destAddr, size, "memset", ctx);
        }
        else
        {
            std::cerr << "memset error: Invalid address provided." << std::endl;
        }

        // returns dest pointer ($v0 = $a0)
        ctx->r[2] = ctx->r[4];
    }

    void memmove(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t destAddr = getRegU32(ctx, 4); // $a0
        uint32_t srcAddr = getRegU32(ctx, 5);  // $a1
        size_t size = getRegU32(ctx, 6);       // $a2

        uint8_t *hostDest = getMemPtr(rdram, destAddr);
        const uint8_t *hostSrc = getConstMemPtr(rdram, srcAddr);

        if (hostDest && hostSrc)
        {
            ::memmove(hostDest, hostSrc, size);
            ps2TraceGuestRangeWrite(rdram, destAddr, static_cast<uint32_t>(size), "memmove", ctx);
        }
        else
        {
            std::cerr << "memmove error: Attempted move involving potentially invalid RDRAM address."
                      << " Dest: 0x" << std::hex << destAddr << " (host ptr valid: " << (hostDest != nullptr) << ")"
                      << ", Src: 0x" << srcAddr << " (host ptr valid: " << (hostSrc != nullptr) << ")" << std::dec
                      << ", Size: " << size << std::endl;
        }

        // returns dest pointer ($v0 = $a0)
        ctx->r[2] = ctx->r[4];
    }

    void memcmp(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t ptr1Addr = getRegU32(ctx, 4); // $a0
        uint32_t ptr2Addr = getRegU32(ctx, 5); // $a1
        uint32_t size = getRegU32(ctx, 6);     // $a2

        const uint8_t *hostPtr1 = getConstMemPtr(rdram, ptr1Addr);
        const uint8_t *hostPtr2 = getConstMemPtr(rdram, ptr2Addr);
        int result = 0;

        if (hostPtr1 && hostPtr2)
        {
            result = ::memcmp(hostPtr1, hostPtr2, size);
        }
        else
        {
            std::cerr << "memcmp error: Invalid address provided."
                      << " Ptr1: 0x" << std::hex << ptr1Addr << " (host ptr valid: " << (hostPtr1 != nullptr) << ")"
                      << ", Ptr2: 0x" << ptr2Addr << " (host ptr valid: " << (hostPtr2 != nullptr) << ")" << std::dec
                      << std::endl;

            result = (hostPtr1 == nullptr) - (hostPtr2 == nullptr);
            if (result == 0)
                result = 1; // If both null, still different? Or 0?
        }
        setReturnS32(ctx, result);
    }

    void strcpy(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t destAddr = getRegU32(ctx, 4); // $a0
        uint32_t srcAddr = getRegU32(ctx, 5);  // $a1

        char *hostDest = reinterpret_cast<char *>(getMemPtr(rdram, destAddr));
        const char *hostSrc = reinterpret_cast<const char *>(getConstMemPtr(rdram, srcAddr));

        if (hostDest && hostSrc)
        {
            ::strcpy(hostDest, hostSrc);
            ps2TraceGuestRangeWrite(rdram, destAddr, static_cast<uint32_t>(::strlen(hostSrc) + 1u), "strcpy", ctx);
        }
        else
        {
            std::cerr << "strcpy error: Invalid address provided."
                      << " Dest: 0x" << std::hex << destAddr << " (host ptr valid: " << (hostDest != nullptr) << ")"
                      << ", Src: 0x" << srcAddr << " (host ptr valid: " << (hostSrc != nullptr) << ")" << std::dec
                      << std::endl;
        }

        // returns dest pointer ($v0 = $a0)
        ctx->r[2] = ctx->r[4];
    }

    void strncpy(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t destAddr = getRegU32(ctx, 4); // $a0
        uint32_t srcAddr = getRegU32(ctx, 5);  // $a1
        uint32_t size = getRegU32(ctx, 6);     // $a2

        char *hostDest = reinterpret_cast<char *>(getMemPtr(rdram, destAddr));
        const char *hostSrc = reinterpret_cast<const char *>(getConstMemPtr(rdram, srcAddr));

        if (hostDest && hostSrc)
        {
            ::strncpy(hostDest, hostSrc, size);
            ps2TraceGuestRangeWrite(rdram, destAddr, size, "strncpy", ctx);
        }
        else
        {
            std::cerr << "strncpy error: Invalid address provided."
                      << " Dest: 0x" << std::hex << destAddr << " (host ptr valid: " << (hostDest != nullptr) << ")"
                      << ", Src: 0x" << srcAddr << " (host ptr valid: " << (hostSrc != nullptr) << ")" << std::dec
                      << std::endl;
        }
        // returns dest pointer ($v0 = $a0)
        ctx->r[2] = ctx->r[4];
    }

    void strlen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t strAddr = getRegU32(ctx, 4); // $a0
        const char *hostStr = reinterpret_cast<const char *>(getConstMemPtr(rdram, strAddr));
        size_t len = 0;

        if (hostStr)
        {
            len = ::strlen(hostStr);
        }
        else
        {
            std::cerr << "strlen error: Invalid address provided: 0x" << std::hex << strAddr << std::dec << std::endl;
        }
        setReturnU32(ctx, (uint32_t)len);
    }

    void strcmp(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t str1Addr = getRegU32(ctx, 4); // $a0
        uint32_t str2Addr = getRegU32(ctx, 5); // $a1

        const char *hostStr1 = reinterpret_cast<const char *>(getConstMemPtr(rdram, str1Addr));
        const char *hostStr2 = reinterpret_cast<const char *>(getConstMemPtr(rdram, str2Addr));
        int result = 0;

        if (hostStr1 && hostStr2)
        {
            result = ::strcmp(hostStr1, hostStr2);
        }
        else
        {
            std::cerr << "strcmp error: Invalid address provided."
                      << " Str1: 0x" << std::hex << str1Addr << " (host ptr valid: " << (hostStr1 != nullptr) << ")"
                      << ", Str2: 0x" << str2Addr << " (host ptr valid: " << (hostStr2 != nullptr) << ")" << std::dec
                      << std::endl;
            // Return non-zero on error, consistent with memcmp error handling
            result = (hostStr1 == nullptr) - (hostStr2 == nullptr);
            if (result == 0 && hostStr1 == nullptr)
                result = 1; // Both null -> treat as different? Or 0? Let's say different.
        }
        setReturnS32(ctx, result);
    }

    void strncmp(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t str1Addr = getRegU32(ctx, 4); // $a0
        uint32_t str2Addr = getRegU32(ctx, 5); // $a1
        uint32_t size = getRegU32(ctx, 6);     // $a2

        const char *hostStr1 = reinterpret_cast<const char *>(getConstMemPtr(rdram, str1Addr));
        const char *hostStr2 = reinterpret_cast<const char *>(getConstMemPtr(rdram, str2Addr));
        int result = 0;

        if (hostStr1 && hostStr2)
        {
            result = ::strncmp(hostStr1, hostStr2, size);
        }
        else
        {
            std::cerr << "strncmp error: Invalid address provided."
                      << " Str1: 0x" << std::hex << str1Addr << " (host ptr valid: " << (hostStr1 != nullptr) << ")"
                      << ", Str2: 0x" << str2Addr << " (host ptr valid: " << (hostStr2 != nullptr) << ")" << std::dec
                      << std::endl;
            result = (hostStr1 == nullptr) - (hostStr2 == nullptr);
            if (result == 0 && hostStr1 == nullptr)
                result = 1; // Both null -> different
        }
        setReturnS32(ctx, result);
    }

    void strcat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t destAddr = getRegU32(ctx, 4); // $a0
        uint32_t srcAddr = getRegU32(ctx, 5);  // $a1

        char *hostDest = reinterpret_cast<char *>(getMemPtr(rdram, destAddr));
        const char *hostSrc = reinterpret_cast<const char *>(getConstMemPtr(rdram, srcAddr));

        if (hostDest && hostSrc)
        {
            ::strcat(hostDest, hostSrc);
        }
        else
        {
            std::cerr << "strcat error: Invalid address provided."
                      << " Dest: 0x" << std::hex << destAddr << " (host ptr valid: " << (hostDest != nullptr) << ")"
                      << ", Src: 0x" << srcAddr << " (host ptr valid: " << (hostSrc != nullptr) << ")" << std::dec
                      << std::endl;
        }

        // returns dest pointer ($v0 = $a0)
        ctx->r[2] = ctx->r[4];
    }

    void strncat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t destAddr = getRegU32(ctx, 4); // $a0
        uint32_t srcAddr = getRegU32(ctx, 5);  // $a1
        uint32_t size = getRegU32(ctx, 6);     // $a2

        char *hostDest = reinterpret_cast<char *>(getMemPtr(rdram, destAddr));
        const char *hostSrc = reinterpret_cast<const char *>(getConstMemPtr(rdram, srcAddr));

        if (hostDest && hostSrc)
        {
            ::strncat(hostDest, hostSrc, size);
        }
        else
        {
            std::cerr << "strncat error: Invalid address provided."
                      << " Dest: 0x" << std::hex << destAddr << " (host ptr valid: " << (hostDest != nullptr) << ")"
                      << ", Src: 0x" << srcAddr << " (host ptr valid: " << (hostSrc != nullptr) << ")" << std::dec
                      << std::endl;
        }

        // returns dest pointer ($v0 = $a0)
        ctx->r[2] = ctx->r[4];
    }

    void strchr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t strAddr = getRegU32(ctx, 4);            // $a0
        int char_code = (int)(getRegU32(ctx, 5) & 0xFF); // $a1 (char value)

        const char *hostStr = reinterpret_cast<const char *>(getConstMemPtr(rdram, strAddr));
        char *foundPtr = nullptr;
        uint32_t resultAddr = 0;

        if (hostStr)
        {
            foundPtr = ::strchr(const_cast<char *>(hostStr), char_code);
            if (foundPtr)
            {
                resultAddr = hostPtrToPs2Addr(rdram, foundPtr);
            }
        }
        else
        {
            std::cerr << "strchr error: Invalid address provided: 0x" << std::hex << strAddr << std::dec << std::endl;
        }

        // returns PS2 address or 0 (NULL)
        setReturnU32(ctx, resultAddr);
    }

    void strrchr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t strAddr = getRegU32(ctx, 4);            // $a0
        int char_code = (int)(getRegU32(ctx, 5) & 0xFF); // $a1 (char value)

        const char *hostStr = reinterpret_cast<const char *>(getConstMemPtr(rdram, strAddr));
        char *foundPtr = nullptr;
        uint32_t resultAddr = 0;

        if (hostStr)
        {
            foundPtr = ::strrchr(const_cast<char *>(hostStr), char_code); // Use const_cast carefully
            if (foundPtr)
            {
                resultAddr = hostPtrToPs2Addr(rdram, foundPtr);
            }
        }
        else
        {
            std::cerr << "strrchr error: Invalid address provided: 0x" << std::hex << strAddr << std::dec << std::endl;
        }

        // returns PS2 address or 0 (NULL)
        setReturnU32(ctx, resultAddr);
    }

    void strstr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t haystackAddr = getRegU32(ctx, 4); // $a0
        uint32_t needleAddr = getRegU32(ctx, 5);   // $a1

        const char *hostHaystack = reinterpret_cast<const char *>(getConstMemPtr(rdram, haystackAddr));
        const char *hostNeedle = reinterpret_cast<const char *>(getConstMemPtr(rdram, needleAddr));
        char *foundPtr = nullptr;
        uint32_t resultAddr = 0;

        if (hostHaystack && hostNeedle)
        {
            foundPtr = ::strstr(const_cast<char *>(hostHaystack), hostNeedle);
            if (foundPtr)
            {
                resultAddr = hostPtrToPs2Addr(rdram, foundPtr);
            }
        }
        else
        {
            std::cerr << "strstr error: Invalid address provided."
                      << " Haystack: 0x" << std::hex << haystackAddr << " (host ptr valid: " << (hostHaystack != nullptr) << ")"
                      << ", Needle: 0x" << needleAddr << " (host ptr valid: " << (hostNeedle != nullptr) << ")" << std::dec
                      << std::endl;
        }

        // returns PS2 address or 0 (NULL)
        setReturnU32(ctx, resultAddr);
    }

    void printf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t format_addr = getRegU32(ctx, 4); // $a0
        const std::string formatOwned = readPs2CStringBounded(rdram, runtime, format_addr, 1024);
        int ret = -1;

        if (format_addr != 0)
        {
            std::string rendered = formatPs2StringWithArgs(rdram, ctx, runtime, formatOwned.c_str(), 1);
            if (rendered.size() > 2048)
            {
                rendered.resize(2048);
            }
            const std::string logLine = sanitizeForLog(rendered);
            uint32_t count = 0;
            {
                std::lock_guard<std::mutex> lock(g_printfLogMutex);
                count = ++g_printfLogCount;
            }
            if (count <= kMaxPrintfLogs)
            {
                std::cout << "PS2 printf: " << logLine;
                std::cout << std::flush;
            }
            else if (count == kMaxPrintfLogs + 1)
            {
                std::cerr << "PS2 printf logging suppressed after " << kMaxPrintfLogs << " lines" << std::endl;
            }
            ret = static_cast<int>(rendered.size());
        }
        else
        {
            std::cerr << "printf error: Invalid format string address provided: 0x" << std::hex << format_addr << std::dec << std::endl;
        }

        // returns the number of characters written, or negative on error.
        setReturnS32(ctx, ret);
    }

    void sprintf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t str_addr = getRegU32(ctx, 4);    // $a0
        uint32_t format_addr = getRegU32(ctx, 5); // $a1

        const std::string formatOwned = readPs2CStringBounded(rdram, runtime, format_addr, 1024);
        int ret = -1;

        if (format_addr != 0)
        {
            const uint32_t watchBase = ps2PathWatchPhysAddr();
            const uint32_t watchEnd = watchBase + PS2_PATH_WATCH_BYTES;
            const uint32_t dest = str_addr & PS2_RAM_MASK;
            const bool touchesWatch = dest < watchEnd && dest >= watchBase;
            static uint32_t watchSprintfLogCount = 0;
            if (touchesWatch && watchSprintfLogCount < 64u)
            {
                const uint32_t arg0 = getRegU32(ctx, 6);
                const uint32_t arg1 = getRegU32(ctx, 7);
                std::cout << "[watch:sprintf] dest=0x" << std::hex << str_addr
                          << " fmt@0x" << format_addr
                          << " arg0=0x" << arg0
                          << " arg1=0x" << arg1
                          << " fmt=\"" << sanitizeForLog(readPs2CStringBounded(rdram, runtime, format_addr, 64)) << "\""
                          << " s0=\"" << sanitizeForLog(readPs2CStringBounded(rdram, runtime, arg0, 64)) << "\""
                          << " s1=\"" << sanitizeForLog(readPs2CStringBounded(rdram, runtime, arg1, 64)) << "\""
                          << std::dec << std::endl;
                ++watchSprintfLogCount;
            }

            std::string rendered = formatPs2StringWithArgs(rdram, ctx, runtime, formatOwned.c_str(), 2);
            if (rendered.size() >= kMaxFormattedOutputBytes)
            {
                rendered.resize(kMaxFormattedOutputBytes - 1);
            }
            const size_t writeLen = rendered.size() + 1u;
            if (writeGuestBytes(rdram, runtime, str_addr, reinterpret_cast<const uint8_t *>(rendered.c_str()), writeLen))
            {
                ps2TraceGuestRangeWrite(rdram, str_addr, static_cast<uint32_t>(writeLen), "sprintf", ctx);
                ret = static_cast<int>(rendered.size());
            }
            else
            {
                std::cerr << "sprintf error: Failed to write destination buffer at 0x"
                          << std::hex << str_addr << std::dec << std::endl;
            }
        }
        else
        {
            std::cerr << "sprintf error: Invalid format address provided."
                      << " Dest: 0x" << std::hex << str_addr
                      << ", Format: 0x" << format_addr << std::dec
                      << std::endl;
        }

        // returns the number of characters written (excluding null), or negative on error.
        setReturnS32(ctx, ret);
    }

    void snprintf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t str_addr = getRegU32(ctx, 4);    // $a0
        size_t size = getRegU32(ctx, 5);          // $a1
        uint32_t format_addr = getRegU32(ctx, 6); // $a2
        const std::string formatOwned = readPs2CStringBounded(rdram, runtime, format_addr, 1024);
        int ret = -1;

        if (format_addr != 0)
        {
            std::string rendered = formatPs2StringWithArgs(rdram, ctx, runtime, formatOwned.c_str(), 3);
            ret = static_cast<int>(rendered.size());

            if (size > 0)
            {
                const size_t copyLen = std::min<size_t>(size - 1, rendered.size());
                std::vector<uint8_t> output(copyLen + 1u, 0u);
                if (copyLen > 0u)
                {
                    std::memcpy(output.data(), rendered.data(), copyLen);
                }
                if (writeGuestBytes(rdram, runtime, str_addr, output.data(), output.size()))
                {
                    ps2TraceGuestRangeWrite(rdram, str_addr, static_cast<uint32_t>(output.size()), "snprintf", ctx);
                }
                else
                {
                    std::cerr << "snprintf error: Failed to write destination buffer at 0x"
                              << std::hex << str_addr << std::dec << std::endl;
                    ret = -1;
                }
            }
        }
        else
        {
            std::cerr << "snprintf error: Invalid address provided or size is zero."
                      << " Dest: 0x" << std::hex << str_addr
                      << ", Format: 0x" << format_addr << std::dec
                      << ", Size: " << size << std::endl;
        }

        // returns the number of characters that *would* have been written
        // if size was large enough (excluding null), or negative on error.
        setReturnS32(ctx, ret);
    }

    void puts(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t strAddr = getRegU32(ctx, 4); // $a0
        const char *hostStr = reinterpret_cast<const char *>(getConstMemPtr(rdram, strAddr));
        int result = EOF;

        if (hostStr)
        {
            result = std::puts(hostStr); // std::puts adds a newline
            std::fflush(stdout);         // Ensure output appears
        }
        else
        {
            std::cerr << "puts error: Invalid address provided: 0x" << std::hex << strAddr << std::dec << std::endl;
        }

        // returns non-negative on success, EOF on error.
        setReturnS32(ctx, result >= 0 ? 0 : -1); // PS2 might expect 0/-1 rather than EOF
    }

    void fopen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4); // $a0
        uint32_t modeAddr = getRegU32(ctx, 5); // $a1

        const char *hostPath = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
        const char *hostMode = reinterpret_cast<const char *>(getConstMemPtr(rdram, modeAddr));
        uint32_t file_handle = 0;

        if (hostPath && hostMode)
        {
            // TODO: Add translation for PS2 paths like mc0:, host:, cdrom:, etc.
            // treating as direct host path
            std::cout << "ps2_stub fopen: path='" << hostPath << "', mode='" << hostMode << "'" << std::endl;
            FILE *fp = ::fopen(hostPath, hostMode);
            if (fp)
            {
                std::lock_guard<std::mutex> lock(g_file_mutex);
                file_handle = generate_file_handle();
                g_file_map[file_handle] = fp;
                std::cout << "  -> handle=0x" << std::hex << file_handle << std::dec << std::endl;
            }
            else
            {
                std::cerr << "ps2_stub fopen error: Failed to open '" << hostPath << "' with mode '" << hostMode << "'. Error: " << strerror(errno) << std::endl;
            }
        }
        else
        {
            std::cerr << "fopen error: Invalid address provided for path or mode."
                      << " Path: 0x" << std::hex << pathAddr << " (host ptr valid: " << (hostPath != nullptr) << ")"
                      << ", Mode: 0x" << modeAddr << " (host ptr valid: " << (hostMode != nullptr) << ")" << std::dec
                      << std::endl;
        }
        // returns a file handle (non-zero) on success, or NULL (0) on error.
        setReturnU32(ctx, file_handle);
    }

    void fclose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t file_handle = getRegU32(ctx, 4); // $a0
        int ret = EOF;                            // Default to error

        if (file_handle != 0)
        {
            std::lock_guard<std::mutex> lock(g_file_mutex);
            auto it = g_file_map.find(file_handle);
            if (it != g_file_map.end())
            {
                FILE *fp = it->second;
                ret = ::fclose(fp);
                g_file_map.erase(it);
            }
            else
            {
                std::cerr << "ps2_stub fclose error: Invalid file handle 0x" << std::hex << file_handle << std::dec << std::endl;
            }
        }
        else
        {
            // Closing NULL handle in Standard C defines this as no-op
            ret = 0;
        }

        // returns 0 on success, EOF on error.
        setReturnS32(ctx, ret);
    }

    void fread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t ptrAddr = getRegU32(ctx, 4);     // $a0 (buffer)
        uint32_t size = getRegU32(ctx, 5);        // $a1 (element size)
        uint32_t count = getRegU32(ctx, 6);       // $a2 (number of elements)
        uint32_t file_handle = getRegU32(ctx, 7); // $a3 (file handle)
        size_t items_read = 0;

        uint8_t *hostPtr = getMemPtr(rdram, ptrAddr);
        FILE *fp = get_file_ptr(file_handle);

        if (hostPtr && fp && size > 0 && count > 0)
        {
            items_read = ::fread(hostPtr, size, count, fp);
        }
        else
        {
            std::cerr << "fread error: Invalid arguments."
                      << " Ptr: 0x" << std::hex << ptrAddr << " (host ptr valid: " << (hostPtr != nullptr) << ")"
                      << ", Handle: 0x" << file_handle << " (file valid: " << (fp != nullptr) << ")" << std::dec
                      << ", Size: " << size << ", Count: " << count << std::endl;
        }
        // returns the number of items successfully read.
        setReturnU32(ctx, (uint32_t)items_read);
    }

    void fwrite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t ptrAddr = getRegU32(ctx, 4);     // $a0 (buffer)
        uint32_t size = getRegU32(ctx, 5);        // $a1 (element size)
        uint32_t count = getRegU32(ctx, 6);       // $a2 (number of elements)
        uint32_t file_handle = getRegU32(ctx, 7); // $a3 (file handle)
        size_t items_written = 0;

        const uint8_t *hostPtr = getConstMemPtr(rdram, ptrAddr);
        FILE *fp = get_file_ptr(file_handle);

        if (hostPtr && fp && size > 0 && count > 0)
        {
            items_written = ::fwrite(hostPtr, size, count, fp);
        }
        else
        {
            std::cerr << "fwrite error: Invalid arguments."
                      << " Ptr: 0x" << std::hex << ptrAddr << " (host ptr valid: " << (hostPtr != nullptr) << ")"
                      << ", Handle: 0x" << file_handle << " (file valid: " << (fp != nullptr) << ")" << std::dec
                      << ", Size: " << size << ", Count: " << count << std::endl;
        }
        // returns the number of items successfully written.
        setReturnU32(ctx, (uint32_t)items_written);
    }

    void fprintf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t file_handle = getRegU32(ctx, 4); // $a0
        uint32_t format_addr = getRegU32(ctx, 5); // $a1
        FILE *fp = get_file_ptr(file_handle);
        const std::string formatOwned = readPs2CStringBounded(rdram, runtime, format_addr, 1024);
        int ret = -1;

        if (fp && format_addr != 0)
        {
            std::string rendered = formatPs2StringWithArgs(rdram, ctx, runtime, formatOwned.c_str(), 2);
            ret = std::fprintf(fp, "%s", rendered.c_str());
        }
        else
        {
            std::cerr << "fprintf error: Invalid file handle or format address."
                      << " Handle: 0x" << std::hex << file_handle << " (file valid: " << (fp != nullptr) << ")"
                      << ", Format: 0x" << format_addr << std::dec
                      << std::endl;
        }

        // returns the number of characters written, or negative on error.
        setReturnS32(ctx, ret);
    }

    void fseek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t file_handle = getRegU32(ctx, 4); // $a0
        long offset = (long)getRegU32(ctx, 5);    // $a1 (Note: might need 64-bit for large files?)
        int whence = (int)getRegU32(ctx, 6);      // $a2 (SEEK_SET, SEEK_CUR, SEEK_END)
        int ret = -1;                             // Default error

        FILE *fp = get_file_ptr(file_handle);

        if (fp)
        {
            // Ensure whence is valid (0, 1, 2)
            if (whence >= 0 && whence <= 2)
            {
                ret = ::fseek(fp, offset, whence);
            }
            else
            {
                std::cerr << "fseek error: Invalid whence value: " << whence << std::endl;
            }
        }
        else
        {
            std::cerr << "fseek error: Invalid file handle 0x" << std::hex << file_handle << std::dec << std::endl;
        }

        // returns 0 on success, non-zero on error.
        setReturnS32(ctx, ret);
    }

    void ftell(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t file_handle = getRegU32(ctx, 4); // $a0
        long ret = -1L;

        FILE *fp = get_file_ptr(file_handle);

        if (fp)
        {
            ret = ::ftell(fp);
        }
        else
        {
            std::cerr << "ftell error: Invalid file handle 0x" << std::hex << file_handle << std::dec << std::endl;
        }

        // returns the current position, or -1L on error.
        if (ret > 0xFFFFFFFFL || ret < 0)
        {
            setReturnS32(ctx, -1);
        }
        else
        {
            setReturnU32(ctx, (uint32_t)ret);
        }
    }

    void fflush(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t file_handle = getRegU32(ctx, 4); // $a0
        int ret = EOF;                            // Default error

        // If handle is 0 fflush flushes *all* output streams.
        if (file_handle == 0)
        {
            ret = ::fflush(NULL);
        }
        else
        {
            FILE *fp = get_file_ptr(file_handle);
            if (fp)
            {
                ret = ::fflush(fp);
            }
            else
            {
                std::cerr << "fflush error: Invalid file handle 0x" << std::hex << file_handle << std::dec << std::endl;
            }
        }
        // returns 0 on success, EOF on error.
        setReturnS32(ctx, ret);
    }

    void sqrt(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::sqrtf(arg);
    }

    void sin(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::sinf(arg);
    }

    void __kernel_sinf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const float x = ctx->f[12];
        const float y = ctx->f[13];
        const int32_t iy = static_cast<int32_t>(getRegU32(ctx, 4));
        ctx->f[0] = ::sinf(x + (iy != 0 ? y : 0.0f));
    }

    void cos(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::cosf(arg);
    }

    void __kernel_cosf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const float x = ctx->f[12];
        const float y = ctx->f[13];
        ctx->f[0] = ::cosf(x + y);
    }

    void __ieee754_rem_pio2f(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const float x = ctx->f[12];
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kHalfPi = kPi * 0.5f;
        constexpr float kInvHalfPi = 2.0f / kPi;
        const int32_t n = static_cast<int32_t>(std::nearbyintf(x * kInvHalfPi));
        const float y0 = x - (static_cast<float>(n) * kHalfPi);
        const float y1 = 0.0f;

        const uint32_t yOutAddr = getRegU32(ctx, 4);
        if (float *yOut0 = reinterpret_cast<float *>(getMemPtr(rdram, yOutAddr)); yOut0)
        {
            *yOut0 = y0;
        }
        if (float *yOut1 = reinterpret_cast<float *>(getMemPtr(rdram, yOutAddr + 4)); yOut1)
        {
            *yOut1 = y1;
        }

        setReturnS32(ctx, n);
    }

    void tan(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::tanf(arg);
    }

    void atan2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        float y = ctx->f[12];
        float x = ctx->f[14];
        ctx->f[0] = ::atan2f(y, x);
    }

    void pow(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        float base = ctx->f[12];
        float exp = ctx->f[14];
        ctx->f[0] = ::powf(base, exp);
    }

    void exp(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::expf(arg);
    }

    void log(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::logf(arg);
    }

    void log10(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::log10f(arg);
    }

    void ceil(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::ceilf(arg);
    }

    void floor(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::floorf(arg);
    }

    void fabs(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::fabsf(arg);
    }

    void sceCdRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t lbn = getRegU32(ctx, 4);     // $a0 - logical block number
        uint32_t sectors = getRegU32(ctx, 5); // $a1 - sector count
        uint32_t buf = getRegU32(ctx, 6);     // $a2 - destination buffer in RDRAM

        uint32_t offset = buf & PS2_RAM_MASK;
        size_t bytes = static_cast<size_t>(sectors) * kCdSectorSize;
        if (bytes > 0)
        {
            const size_t maxBytes = PS2_RAM_SIZE - offset;
            if (bytes > maxBytes)
            {
                bytes = maxBytes;
            }
        }

        uint8_t *dst = rdram + offset;
        bool ok = true;
        if (bytes > 0)
        {
            ok = readCdSectors(lbn, sectors, dst, bytes);
            if (!ok)
            {
                std::memset(dst, 0, bytes);
            }
        }

        if (ok)
        {
            g_cdStreamingLbn = lbn + sectors;
            setReturnS32(ctx, 1); // command accepted/success
        }
        else
        {
            setReturnS32(ctx, 0);
        }
    }

    void sceCdSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0); // 0 = completed/not busy
    }

    void sceCdGetError(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, g_lastCdError);
    }

    void syRtcInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub syRtcInit" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void _builtin_set_imask(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub _builtin_set_imask" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void syFree(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub syFree" << std::endl;
            ++logCount;
        }

        const uint32_t guestAddr = getRegU32(ctx, 4); // $a0
        if (runtime && guestAddr != 0u)
        {
            runtime->guestFree(guestAddr);
        }

        setReturnS32(ctx, 0);
    }

    void syMalloc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t requestedSize = getRegU32(ctx, 4); // $a0
        uint32_t resultAddr = 0u;

        if (runtime && requestedSize != 0u)
        {
            // Match game expectation for allocator alignment while keeping pointers in EE RAM.
            resultAddr = runtime->guestMalloc(requestedSize, 64u);
        }

        static int logCount = 0;
        if (logCount < 16)
        {
            std::cout << "ps2_stub syMalloc"
                      << " size=0x" << std::hex << requestedSize
                      << " -> 0x" << resultAddr
                      << std::dec << std::endl;
            ++logCount;
        }

        setReturnU32(ctx, resultAddr);
    }

    void InitSdcParameter(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub InitSdcParameter" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void Ps2_pad_actuater(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub Ps2_pad_actuater" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void syMallocInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (runtime)
        {
            const uint32_t heapBase = getRegU32(ctx, 4); // $a0
            const uint32_t heapSize = getRegU32(ctx, 5); // $a1 (optional size)

            constexpr uint32_t kHeapBaseFloor = 0x00100000u;
            uint32_t normalizedBase = heapBase;
            if (normalizedBase >= 0x80000000u && normalizedBase < 0xC0000000u)
            {
                normalizedBase &= 0x1FFFFFFFu;
            }
            else if (normalizedBase >= PS2_RAM_SIZE)
            {
                normalizedBase &= PS2_RAM_MASK;
            }

            const bool suspiciousKsegBase = (heapBase & 0xE0000000u) == 0x80000000u && normalizedBase < kHeapBaseFloor;
            if (normalizedBase == 0u || suspiciousKsegBase)
            {
                // Keep the ELF-driven suggestion instead of collapsing heap to low memory.
                normalizedBase = runtime->guestHeapBase();
            }

            // Treat absurd "size" values as unspecified limit.
            uint32_t heapLimit = 0u;
            if (heapSize != 0u && heapSize <= PS2_RAM_SIZE && normalizedBase < PS2_RAM_SIZE)
            {
                const uint64_t candidateLimit = static_cast<uint64_t>(normalizedBase) + static_cast<uint64_t>(heapSize);
                heapLimit = static_cast<uint32_t>(std::min<uint64_t>(candidateLimit, PS2_RAM_SIZE));
            }
            runtime->configureGuestHeap(normalizedBase, heapLimit);
            if (logCount < 8)
            {
                std::cout << "ps2_stub syMallocInit"
                          << " reqBase=0x" << std::hex << heapBase
                          << " reqSize=0x" << heapSize
                          << " normBase=0x" << normalizedBase
                          << " reqLimit=0x" << heapLimit
                          << " finalBase=0x" << runtime->guestHeapBase()
                          << " finalEnd=0x" << runtime->guestHeapEnd()
                          << std::dec << std::endl;
                ++logCount;
            }
        }
        else if (logCount < 8)
        {
            std::cout << "ps2_stub syMallocInit" << std::endl;
            ++logCount;
        }

        setReturnS32(ctx, 0);
    }

    void syHwInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub syHwInit" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void syHwInit2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub syHwInit2" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void InitGdSystemEx(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub InitGdSystemEx" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void pdInitPeripheral(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub pdInitPeripheral" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void pdGetPeripheral(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub pdGetPeripheral" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void Ps2SwapDBuff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub Ps2SwapDBuff" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void InitReadKeyEx(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub InitReadKeyEx" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void SetRepeatKeyTimer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub SetRepeatKeyTimer" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void StopFxProgram(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub StopFxProgram" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void sndr_trans_func(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub sndr_trans_func (noop)" << std::endl;
            ++logCount;
        }

        // For now just clear the snd busy flag used by sdMultiUnitDownload/SysServer loops.
        constexpr uint32_t kSndBusyAddr = 0x01E0E170;
        if (rdram)
        {
            uint32_t offset = kSndBusyAddr & PS2_RAM_MASK;
            if (offset + sizeof(uint32_t) <= PS2_RAM_SIZE)
            {
                *reinterpret_cast<uint32_t *>(rdram + offset) = 0;
            }
        }

        setReturnS32(ctx, 0);
    }

    void sdDrvInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub sdDrvInit (noop)" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void ADXF_LoadPartitionNw(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub ADXF_LoadPartitionNw (noop)" << std::endl;
            ++logCount;
        }
        // Return success to keep the ADX partition setup moving.
        setReturnS32(ctx, 0);
    }

    void sdSndStopAll(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub sdSndStopAll" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void sdSysFinish(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub sdSysFinish" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void ADXT_Init(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub ADXT_Init" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void ADXT_SetNumRetry(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub ADXT_SetNumRetry" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void cvFsSetDefDev(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub cvFsSetDefDev" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void _calloc_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t count = getRegU32(ctx, 5); // $a1
        const uint32_t size = getRegU32(ctx, 6);  // $a2
        const uint32_t guestAddr = runtime ? runtime->guestCalloc(count, size) : 0u;
        setReturnU32(ctx, guestAddr);
    }

    void _free_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t guestAddr = getRegU32(ctx, 5); // $a1
        if (runtime && guestAddr != 0u)
        {
            runtime->guestFree(guestAddr);
        }
    }

    void _malloc_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t size = getRegU32(ctx, 5); // $a1
        const uint32_t guestAddr = runtime ? runtime->guestMalloc(size) : 0u;
        setReturnU32(ctx, guestAddr);
    }

    void _malloc_trim_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void _mbtowc_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_mbtowc_r", rdram, ctx, runtime);
    }

    void _printf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        printf(rdram, ctx, runtime);
    }

    void _printf_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t format_addr = getRegU32(ctx, 5); // $a1
        const std::string formatOwned = readPs2CStringBounded(rdram, runtime, format_addr, 1024);
        int ret = -1;

        if (format_addr != 0)
        {
            std::string rendered = formatPs2StringWithArgs(rdram, ctx, runtime, formatOwned.c_str(), 2);
            if (rendered.size() > 2048)
            {
                rendered.resize(2048);
            }
            const std::string logLine = sanitizeForLog(rendered);
            uint32_t count = 0;
            {
                std::lock_guard<std::mutex> lock(g_printfLogMutex);
                count = ++g_printfLogCount;
            }
            if (count <= kMaxPrintfLogs)
            {
                std::cout << "PS2 printf: " << logLine;
                std::cout << std::flush;
            }
            else if (count == kMaxPrintfLogs + 1)
            {
                std::cerr << "PS2 printf logging suppressed after " << kMaxPrintfLogs << " lines" << std::endl;
            }
            ret = static_cast<int>(rendered.size());
        }
        else
        {
            std::cerr << "_printf_r error: Invalid format string address provided: 0x" << std::hex << format_addr << std::dec << std::endl;
        }

        setReturnS32(ctx, ret);
    }

    void _sceCdRI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceCdRI", rdram, ctx, runtime);
    }

    void _sceCdRM(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceCdRM", rdram, ctx, runtime);
    }

    void _sceFsDbChk(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceFsDbChk", rdram, ctx, runtime);
    }

    void _sceFsIntrSigSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceFsIntrSigSema", rdram, ctx, runtime);
    }

    void _sceFsSemExit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceFsSemExit", rdram, ctx, runtime);
    }

    void _sceFsSemInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceFsSemInit", rdram, ctx, runtime);
    }

    void _sceFsSigSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceFsSigSema", rdram, ctx, runtime);
    }

    void _sceIDC(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceIDC", rdram, ctx, runtime);
    }

    void _sceMpegFlush(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceMpegFlush", rdram, ctx, runtime);
    }

    void _sceRpcFreePacket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceRpcFreePacket", rdram, ctx, runtime);
    }

    void _sceRpcGetFPacket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceRpcGetFPacket", rdram, ctx, runtime);
    }

    void _sceRpcGetFPacket2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceRpcGetFPacket2", rdram, ctx, runtime);
    }

    void _sceSDC(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceSDC", rdram, ctx, runtime);
    }

    void _sceSifCmdIntrHdlr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceSifCmdIntrHdlr", rdram, ctx, runtime);
    }

    void _sceSifLoadElfPart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::SifLoadElfPart(rdram, ctx, runtime);
    }

    void _sceSifLoadModule(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceSifLoadModule", rdram, ctx, runtime);
    }

    void _sceSifSendCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceSifSendCmd", rdram, ctx, runtime);
    }

    void _sceVu0ecossin(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceVu0ecossin", rdram, ctx, runtime);
    }

    void abs(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("abs", rdram, ctx, runtime);
    }

    void atan(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("atan", rdram, ctx, runtime);
    }

    void close(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::fioClose(rdram, ctx, runtime);
    }

    void DmaAddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("DmaAddr", rdram, ctx, runtime);
    }

    void exit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("exit", rdram, ctx, runtime);
    }

    void fstat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t statAddr = getRegU32(ctx, 5);
        if (uint8_t *statBuf = getMemPtr(rdram, statAddr))
        {
            std::memset(statBuf, 0, 128);
            setReturnS32(ctx, 0);
            return;
        }
        setReturnS32(ctx, -1);
    }

    void getpid(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("getpid", rdram, ctx, runtime);
    }

    void iopGetArea(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("iopGetArea", rdram, ctx, runtime);
    }

    void lseek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::fioLseek(rdram, ctx, runtime);
    }

    void mcCallMessageTypeSe(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcCallMessageTypeSe", rdram, ctx, runtime);
    }

    void mcCheckReadStartConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcCheckReadStartConfigFile", rdram, ctx, runtime);
    }

    void mcCheckReadStartSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcCheckReadStartSaveFile", rdram, ctx, runtime);
    }

    void mcCheckWriteStartConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcCheckWriteStartConfigFile", rdram, ctx, runtime);
    }

    void mcCheckWriteStartSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcCheckWriteStartSaveFile", rdram, ctx, runtime);
    }

    void mcCreateConfigInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcCreateConfigInit", rdram, ctx, runtime);
    }

    void mcCreateFileSelectWindow(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcCreateFileSelectWindow", rdram, ctx, runtime);
    }

    void mcCreateIconInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcCreateIconInit", rdram, ctx, runtime);
    }

    void mcCreateSaveFileInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcCreateSaveFileInit", rdram, ctx, runtime);
    }

    void mcDispFileName(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcDispFileName", rdram, ctx, runtime);
    }

    void mcDispFileNumber(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcDispFileNumber", rdram, ctx, runtime);
    }

    void mcDisplayFileSelectWindow(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcDisplayFileSelectWindow", rdram, ctx, runtime);
    }

    void mcDisplaySelectFileInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcDisplaySelectFileInfo", rdram, ctx, runtime);
    }

    void mcDisplaySelectFileInfoMesCount(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcDisplaySelectFileInfoMesCount", rdram, ctx, runtime);
    }

    void mcDispWindowCurSol(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcDispWindowCurSol", rdram, ctx, runtime);
    }

    void mcDispWindowFoundtion(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcDispWindowFoundtion", rdram, ctx, runtime);
    }

    void mceGetInfoApdx(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mceGetInfoApdx", rdram, ctx, runtime);
    }

    void mceIntrReadFixAlign(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mceIntrReadFixAlign", rdram, ctx, runtime);
    }

    void mceStorePwd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mceStorePwd", rdram, ctx, runtime);
    }

    void mcGetConfigCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcGetConfigCapacitySize", rdram, ctx, runtime);
    }

    void mcGetFileSelectWindowCursol(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcGetFileSelectWindowCursol", rdram, ctx, runtime);
    }

    void mcGetFreeCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcGetFreeCapacitySize", rdram, ctx, runtime);
    }

    void mcGetIconCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcGetIconCapacitySize", rdram, ctx, runtime);
    }

    void mcGetIconFileCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcGetIconFileCapacitySize", rdram, ctx, runtime);
    }

    void mcGetPortSelectDirInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcGetPortSelectDirInfo", rdram, ctx, runtime);
    }

    void mcGetSaveFileCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcGetSaveFileCapacitySize", rdram, ctx, runtime);
    }

    void mcGetStringEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcGetStringEnd", rdram, ctx, runtime);
    }

    void mcMoveFileSelectWindowCursor(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcMoveFileSelectWindowCursor", rdram, ctx, runtime);
    }

    void mcNewCreateConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcNewCreateConfigFile", rdram, ctx, runtime);
    }

    void mcNewCreateIcon(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcNewCreateIcon", rdram, ctx, runtime);
    }

    void mcNewCreateSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcNewCreateSaveFile", rdram, ctx, runtime);
    }

    void mcReadIconData(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcReadIconData", rdram, ctx, runtime);
    }

    void mcReadStartConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcReadStartConfigFile", rdram, ctx, runtime);
    }

    void mcReadStartSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcReadStartSaveFile", rdram, ctx, runtime);
    }

    void mcSelectFileInfoInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcSelectFileInfoInit", rdram, ctx, runtime);
    }

    void mcSelectSaveFileCheck(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcSelectSaveFileCheck", rdram, ctx, runtime);
    }

    void mcSetFileSelectWindowCursol(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcSetFileSelectWindowCursol", rdram, ctx, runtime);
    }

    void mcSetFileSelectWindowCursolInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcSetFileSelectWindowCursolInit", rdram, ctx, runtime);
    }

    void mcSetStringSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcSetStringSaveFile", rdram, ctx, runtime);
    }

    void mcSetTyepWriteMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcSetTyepWriteMode", rdram, ctx, runtime);
    }

    void mcWriteIconData(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcWriteIconData", rdram, ctx, runtime);
    }

    void mcWriteStartConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcWriteStartConfigFile", rdram, ctx, runtime);
    }

    void mcWriteStartSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcWriteStartSaveFile", rdram, ctx, runtime);
    }

    void memchr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("memchr", rdram, ctx, runtime);
    }

    void open(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::fioOpen(rdram, ctx, runtime);
    }

    void Pad_init(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void Pad_set(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("Pad_set", rdram, ctx, runtime);
    }

    void rand(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("rand", rdram, ctx, runtime);
    }

    void read(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::fioRead(rdram, ctx, runtime);
    }

    void sceCdApplyNCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void sceCdBreak(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void sceCdCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceCdChangeThreadPriority(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void sceCdDelayThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceCdDiskReady(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 2);
    }

    void sceCdGetDiskType(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // SCECdPS2DVD
        setReturnS32(ctx, 0x14);
    }

    void sceCdGetReadPos(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnU32(ctx, g_cdStreamingLbn);
    }

    void sceCdGetToc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t tocAddr = getRegU32(ctx, 4);
        if (uint8_t *toc = getMemPtr(rdram, tocAddr))
        {
            std::memset(toc, 0, 1024);
        }
        setReturnS32(ctx, 1);
    }

    void sceCdInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_cdInitialized = true;
        g_lastCdError = 0;
        setReturnS32(ctx, 1);
    }

    void sceCdInitEeCB(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void sceCdIntToPos(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t lsn = getRegU32(ctx, 4);
        uint32_t posAddr = getRegU32(ctx, 5);
        uint8_t *pos = getMemPtr(rdram, posAddr);
        if (!pos)
        {
            setReturnS32(ctx, 0);
            return;
        }

        uint32_t adjusted = lsn + 150;
        const uint32_t minutes = adjusted / (60 * 75);
        adjusted %= (60 * 75);
        const uint32_t seconds = adjusted / 75;
        const uint32_t sectors = adjusted % 75;

        pos[0] = toBcd(minutes);
        pos[1] = toBcd(seconds);
        pos[2] = toBcd(sectors);
        pos[3] = 0;
        setReturnS32(ctx, 1);
    }

    void sceCdMmode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_cdMode = getRegU32(ctx, 4);
        setReturnS32(ctx, 1);
    }

    void sceCdNcmdDiskReady(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 2);
    }

    void sceCdPause(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void sceCdPosToInt(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t posAddr = getRegU32(ctx, 4);
        const uint8_t *pos = getConstMemPtr(rdram, posAddr);
        if (!pos)
        {
            setReturnS32(ctx, -1);
            return;
        }

        const uint32_t minutes = fromBcd(pos[0]);
        const uint32_t seconds = fromBcd(pos[1]);
        const uint32_t sectors = fromBcd(pos[2]);
        const uint32_t absolute = (minutes * 60 * 75) + (seconds * 75) + sectors;
        const int32_t lsn = static_cast<int32_t>(absolute) - 150;
        setReturnS32(ctx, lsn);
    }

    void sceCdReadChain(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t chainAddr = getRegU32(ctx, 4);
        bool ok = true;

        for (int i = 0; i < 64; ++i)
        {
            uint32_t *entry = reinterpret_cast<uint32_t *>(getMemPtr(rdram, chainAddr + (i * 16)));
            if (!entry)
            {
                ok = false;
                break;
            }

            const uint32_t lbn = entry[0];
            const uint32_t sectors = entry[1];
            const uint32_t buf = entry[2];
            if (lbn == 0xFFFFFFFFu || sectors == 0)
            {
                break;
            }

            uint32_t offset = buf & PS2_RAM_MASK;
            size_t bytes = static_cast<size_t>(sectors) * kCdSectorSize;
            const size_t maxBytes = PS2_RAM_SIZE - offset;
            if (bytes > maxBytes)
            {
                bytes = maxBytes;
            }

            if (!readCdSectors(lbn, sectors, rdram + offset, bytes))
            {
                ok = false;
                break;
            }

            g_cdStreamingLbn = lbn + sectors;
        }

        setReturnS32(ctx, ok ? 1 : 0);
    }

    void sceCdReadClock(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t clockAddr = getRegU32(ctx, 4);
        uint8_t *clockData = getMemPtr(rdram, clockAddr);
        if (!clockData)
        {
            setReturnS32(ctx, 0);
            return;
        }

        std::time_t now = std::time(nullptr);
        std::tm localTm{};
#ifdef _WIN32
        localtime_s(&localTm, &now);
#else
        localtime_r(&now, &localTm);
#endif

        // sceCdCLOCK format (BCD fields).
        clockData[0] = 0;
        clockData[1] = toBcd(static_cast<uint32_t>(localTm.tm_sec));
        clockData[2] = toBcd(static_cast<uint32_t>(localTm.tm_min));
        clockData[3] = toBcd(static_cast<uint32_t>(localTm.tm_hour));
        clockData[4] = 0;
        clockData[5] = toBcd(static_cast<uint32_t>(localTm.tm_mday));
        clockData[6] = toBcd(static_cast<uint32_t>(localTm.tm_mon + 1));
        clockData[7] = toBcd(static_cast<uint32_t>((localTm.tm_year + 1900) % 100));
        setReturnS32(ctx, 1);
    }

    void sceCdReadIOPm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        sceCdRead(rdram, ctx, runtime);
    }

    void sceCdSearchFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t fileAddr = getRegU32(ctx, 4);
        uint32_t pathAddr = getRegU32(ctx, 5);
        const std::string path = readPs2CStringBounded(rdram, pathAddr, 260);
        const std::string normalizedPath = normalizeCdPathNoPrefix(path);
        static uint32_t traceCount = 0;
        const uint32_t callerRa = getRegU32(ctx, 31);
        const bool shouldTrace = (traceCount < 128u) || ((traceCount % 512u) == 0u);
        if (shouldTrace)
        {
            std::cout << "[sceCdSearchFile] pc=0x" << std::hex << ctx->pc
                      << " ra=0x" << callerRa
                      << " file=0x" << fileAddr
                      << " pathAddr=0x" << pathAddr
                      << " path=\"" << sanitizeForLog(path) << "\""
                      << std::dec << std::endl;
        }
        ++traceCount;

        if (path.empty())
        {
            static uint32_t emptyPathCount = 0;
            if (emptyPathCount < 64 || (emptyPathCount % 512u) == 0u)
            {
                std::ostringstream preview;
                preview << std::hex;
                for (uint32_t i = 0; i < 16; ++i)
                {
                    const uint8_t byte = *getConstMemPtr(rdram, pathAddr + i);
                    preview << (i == 0 ? "" : " ") << static_cast<uint32_t>(byte);
                }
                std::cerr << "[sceCdSearchFile] empty path at 0x" << std::hex << pathAddr
                          << " preview=" << preview.str()
                          << " ra=0x" << callerRa << std::dec << std::endl;
            }
            ++emptyPathCount;
            g_lastCdError = -1;
            setReturnS32(ctx, 0);
            return;
        }

        if (normalizedPath.empty())
        {
            static uint32_t emptyNormalizedCount = 0;
            if (emptyNormalizedCount < 64u || (emptyNormalizedCount % 512u) == 0u)
            {
                std::cerr << "sceCdSearchFile failed: " << sanitizeForLog(path)
                          << " (normalized path is empty, root: " << getCdRootPath().string() << ")"
                          << std::endl;
            }
            ++emptyNormalizedCount;
            g_lastCdError = -1;
            setReturnS32(ctx, 0);
            return;
        }

        CdFileEntry entry;
        bool found = registerCdFile(path, entry);
        CdFileEntry resolvedEntry = entry;
        std::string resolvedPath;
        bool usedRemapFallback = false;

        // Remap is fallback-only: if the requested .IDX exists, keep it.
        // This avoids feeding AFS payload sectors to code that expects IDX metadata.
        if (!found)
        {
            const CdFileEntry missingEntry{};
            if (tryRemapGdInitSearchToAfs(path, callerRa, missingEntry, resolvedEntry, resolvedPath))
            {
                found = true;
                usedRemapFallback = true;
            }
        }

        if (!found)
        {
            static std::string lastFailedPath;
            static uint32_t samePathFailCount = 0;
            if (path == lastFailedPath)
            {
                ++samePathFailCount;
            }
            else
            {
                lastFailedPath = path;
                samePathFailCount = 1;
            }

            if (samePathFailCount <= 16u || (samePathFailCount % 512u) == 0u)
            {
                std::cerr << "sceCdSearchFile failed: " << sanitizeForLog(path)
                          << " (root: " << getCdRootPath().string()
                          << ", repeat=" << samePathFailCount << ")" << std::endl;
            }
            setReturnS32(ctx, 0);
            return;
        }

        if (usedRemapFallback)
        {
            std::cout << "[sceCdSearchFile] remap gd-init search \"" << sanitizeForLog(path)
                      << "\" -> \"" << sanitizeForLog(resolvedPath) << "\"" << std::endl;
        }

        if (!writeCdSearchResult(rdram, fileAddr, path, resolvedEntry))
        {
            g_lastCdError = -1;
            setReturnS32(ctx, 0);
            return;
        }

        g_cdStreamingLbn = resolvedEntry.baseLbn;
        if (shouldTrace)
        {
            std::cout << "[sceCdSearchFile:ok] path=\"" << sanitizeForLog(path)
                      << "\" lsn=0x" << std::hex << resolvedEntry.baseLbn
                      << " size=0x" << resolvedEntry.sizeBytes
                      << " sectors=0x" << resolvedEntry.sectors
                      << std::dec << std::endl;
        }
        setReturnS32(ctx, 1);
    }

    void sceCdSeek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_cdStreamingLbn = getRegU32(ctx, 4);
        setReturnS32(ctx, 1);
    }

    void sceCdStandby(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void sceCdStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, g_cdInitialized ? 6 : 0);
    }

    void sceCdStInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void sceCdStop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void sceCdStPause(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void sceCdStRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t sectors = getRegU32(ctx, 4);
        uint32_t buf = getRegU32(ctx, 5);
        uint32_t errAddr = getRegU32(ctx, 7);

        uint32_t offset = buf & PS2_RAM_MASK;
        size_t bytes = static_cast<size_t>(sectors) * kCdSectorSize;
        const size_t maxBytes = PS2_RAM_SIZE - offset;
        if (bytes > maxBytes)
        {
            bytes = maxBytes;
        }

        const bool ok = readCdSectors(g_cdStreamingLbn, sectors, rdram + offset, bytes);
        if (ok)
        {
            g_cdStreamingLbn += sectors;
        }

        if (int32_t *err = reinterpret_cast<int32_t *>(getMemPtr(rdram, errAddr)); err)
        {
            *err = ok ? 0 : g_lastCdError;
        }

        setReturnS32(ctx, ok ? static_cast<int32_t>(sectors) : 0);
    }

    void sceCdStream(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void sceCdStResume(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void sceCdStSeek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_cdStreamingLbn = getRegU32(ctx, 4);
        setReturnS32(ctx, 1);
    }

    void sceCdStSeekF(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_cdStreamingLbn = getRegU32(ctx, 4);
        setReturnS32(ctx, 1);
    }

    void sceCdStStart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_cdStreamingLbn = getRegU32(ctx, 4);
        setReturnS32(ctx, 1);
    }

    void sceCdStStat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceCdStStop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void sceCdSyncS(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceCdTrayReq(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t statusPtr = getRegU32(ctx, 5);
        if (uint32_t *status = reinterpret_cast<uint32_t *>(getMemPtr(rdram, statusPtr)); status)
        {
            *status = 0;
        }
        setReturnS32(ctx, 1);
    }

    void sceClose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::fioClose(rdram, ctx, runtime);
    }

    void sceDeci2Close(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDeci2Close", rdram, ctx, runtime);
    }

    void sceDeci2ExLock(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDeci2ExLock", rdram, ctx, runtime);
    }

    void sceDeci2ExRecv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDeci2ExRecv", rdram, ctx, runtime);
    }

    void sceDeci2ExReqSend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDeci2ExReqSend", rdram, ctx, runtime);
    }

    void sceDeci2ExSend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDeci2ExSend", rdram, ctx, runtime);
    }

    void sceDeci2ExUnLock(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDeci2ExUnLock", rdram, ctx, runtime);
    }

    void sceDeci2Open(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDeci2Open", rdram, ctx, runtime);
    }

    void sceDeci2Poll(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDeci2Poll", rdram, ctx, runtime);
    }

    void sceDeci2ReqSend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDeci2ReqSend", rdram, ctx, runtime);
    }

    void sceDmaCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaCallback", rdram, ctx, runtime);
    }

    void sceDmaDebug(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaDebug", rdram, ctx, runtime);
    }

    void sceDmaGetChan(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t chanArg = getRegU32(ctx, 4);
        const uint32_t channelBase = resolveDmaChannelBase(rdram, chanArg);
        setReturnU32(ctx, channelBase);
    }

    void sceDmaGetEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaGetEnv", rdram, ctx, runtime);
    }

    void sceDmaLastSyncTime(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaLastSyncTime", rdram, ctx, runtime);
    }

    void sceDmaPause(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaPause", rdram, ctx, runtime);
    }

    void sceDmaPutEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaPutEnv", rdram, ctx, runtime);
    }

    void sceDmaPutStallAddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaPutStallAddr", rdram, ctx, runtime);
    }

    void sceDmaRecv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaRecv", rdram, ctx, runtime);
    }

    void sceDmaRecvI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaRecvI", rdram, ctx, runtime);
    }

    void sceDmaRecvN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaRecvN", rdram, ctx, runtime);
    }

    void sceDmaReset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceDmaRestart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaRestart", rdram, ctx, runtime);
    }

    void sceDmaSend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, submitDmaSend(rdram, ctx, runtime, false));
    }

    void sceDmaSendI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, submitDmaSend(rdram, ctx, runtime, false));
    }

    void sceDmaSendM(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, submitDmaSend(rdram, ctx, runtime, false));
    }

    void sceDmaSendN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, submitDmaSend(rdram, ctx, runtime, true));
    }

    void sceDmaSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, submitDmaSync(rdram, ctx, runtime));
    }

    void sceDmaSyncN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, submitDmaSync(rdram, ctx, runtime));
    }

    void sceDmaWatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaWatch", rdram, ctx, runtime);
    }

    void sceFsInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceFsInit", rdram, ctx, runtime);
    }

    void sceFsReset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceGsExecLoadImage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t imgAddr = getRegU32(ctx, 4);
        uint32_t srcAddr = getRegU32(ctx, 5);

        GsImageMem img{};
        if (!runtime || !readGsImage(rdram, imgAddr, img))
        {
            setReturnS32(ctx, -1);
            return;
        }

        const uint32_t rowBytes = bytesForPixels(img.psm, static_cast<uint32_t>(img.width));
        if (rowBytes == 0)
        {
            setReturnS32(ctx, -1);
            return;
        }

        uint32_t fbw = img.vram_width ? img.vram_width : std::max<uint32_t>(1, (img.width + 63) / 64);
        uint32_t base = static_cast<uint32_t>(img.vram_addr) * 2048u;
        uint32_t stride = bytesForPixels(img.psm, fbw * 64u);
        if (stride == 0)
        {
            setReturnS32(ctx, -1);
            return;
        }

        uint8_t *gsvram = runtime->memory().getGSVRAM();
        uint8_t *src = getMemPtr(rdram, srcAddr);
        if (!gsvram || !src)
        {
            setReturnS32(ctx, -1);
            return;
        }

        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub sceGsExecLoadImage: x=" << img.x
                      << " y=" << img.y
                      << " w=" << img.width
                      << " h=" << img.height
                      << " vram=0x" << std::hex << img.vram_addr
                      << " fbw=" << std::dec << static_cast<int>(fbw)
                      << " psm=" << static_cast<int>(img.psm)
                      << " src=0x" << std::hex << srcAddr << std::dec << std::endl;
            ++logCount;
        }

        for (uint32_t row = 0; row < img.height; ++row)
        {
            uint32_t dstOff = base + (static_cast<uint32_t>(img.y) + row) * stride + bytesForPixels(img.psm, static_cast<uint32_t>(img.x));
            uint32_t srcOff = row * rowBytes;
            if (dstOff >= PS2_GS_VRAM_SIZE)
                break;
            uint32_t copyBytes = rowBytes;
            if (dstOff + copyBytes > PS2_GS_VRAM_SIZE)
                copyBytes = PS2_GS_VRAM_SIZE - dstOff;
            std::memcpy(gsvram + dstOff, src + srcOff, copyBytes);
        }

        if (img.width >= 320 && img.height >= 200)
        {
            auto &gs = runtime->memory().gs();
            gs.dispfb1 = makeDispFb(img.vram_addr, fbw, img.psm, 0, 0);
            gs.display1 = makeDisplay(0, 0, 0, 0, img.width - 1, img.height - 1);
        }

        setReturnS32(ctx, 0);
    }

    void sceGsExecStoreImage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t imgAddr = getRegU32(ctx, 4);
        uint32_t dstAddr = getRegU32(ctx, 5);

        GsImageMem img{};
        if (!runtime || !readGsImage(rdram, imgAddr, img))
        {
            setReturnS32(ctx, -1);
            return;
        }

        const uint32_t rowBytes = bytesForPixels(img.psm, static_cast<uint32_t>(img.width));
        if (rowBytes == 0)
        {
            setReturnS32(ctx, -1);
            return;
        }

        uint32_t fbw = img.vram_width ? img.vram_width : std::max<uint32_t>(1, (img.width + 63) / 64);
        uint32_t base = static_cast<uint32_t>(img.vram_addr) * 2048u;
        uint32_t stride = bytesForPixels(img.psm, fbw * 64u);
        if (stride == 0)
        {
            setReturnS32(ctx, -1);
            return;
        }

        uint8_t *gsvram = runtime->memory().getGSVRAM();
        uint8_t *dst = getMemPtr(rdram, dstAddr);
        if (!gsvram || !dst)
        {
            setReturnS32(ctx, -1);
            return;
        }

        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub sceGsExecStoreImage: x=" << img.x
                      << " y=" << img.y
                      << " w=" << img.width
                      << " h=" << img.height
                      << " vram=0x" << std::hex << img.vram_addr
                      << " fbw=" << std::dec << static_cast<int>(fbw)
                      << " psm=" << static_cast<int>(img.psm)
                      << " dst=0x" << std::hex << dstAddr << std::dec << std::endl;
            ++logCount;
        }

        for (uint32_t row = 0; row < img.height; ++row)
        {
            uint32_t srcOff = base + (static_cast<uint32_t>(img.y) + row) * stride + bytesForPixels(img.psm, static_cast<uint32_t>(img.x));
            uint32_t dstOff = row * rowBytes;
            if (srcOff >= PS2_GS_VRAM_SIZE)
                break;
            uint32_t copyBytes = rowBytes;
            if (srcOff + copyBytes > PS2_GS_VRAM_SIZE)
                copyBytes = PS2_GS_VRAM_SIZE - srcOff;
            std::memcpy(dst + dstOff, gsvram + srcOff, copyBytes);
        }

        setReturnS32(ctx, 0);
    }

    void sceGsGetGParam(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t addr = writeGsGParamToScratch(runtime);
        setReturnU32(ctx, addr);
    }

    void sceGsPutDispEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t envAddr = getRegU32(ctx, 4);
        GsDispEnvMem env{};
        if (readGsDispEnv(rdram, envAddr, env))
        {
            auto &gs = runtime->memory().gs();
            gs.display1 = env.display;
            gs.dispfb1 = env.dispfb;
        }
        setReturnS32(ctx, 0);
    }

    void sceGsPutDrawEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t envAddr = getRegU32(ctx, 4);
        uint32_t psm = getRegU32(ctx, 5);
        uint32_t w = getRegU32(ctx, 6);
        uint32_t h = getRegU32(ctx, 7);

        if (w == 0)
            w = 640;
        if (h == 0)
            h = 448;

        GsDrawEnvMem env{};
        env.offset_x = static_cast<uint16_t>(2048 - (w / 2));
        env.offset_y = static_cast<uint16_t>(2048 - (h / 2));
        env.clip_x = 0;
        env.clip_y = 0;
        env.clip_w = static_cast<uint16_t>(w);
        env.clip_h = static_cast<uint16_t>(h);
        env.vram_addr = 0;
        env.fbw = static_cast<uint8_t>((w + 63) / 64);
        env.psm = static_cast<uint8_t>(psm);
        env.vram_x = 0;
        env.vram_y = 0;
        env.draw_mask = 0;
        env.auto_clear = 1;
        env.bg_r = 1;
        env.bg_g = 1;
        env.bg_b = 1;
        env.bg_a = 0x80;
        env.bg_q = 0.0f;

        uint8_t *ptr = getMemPtr(rdram, envAddr);
        if (ptr)
        {
            std::memcpy(ptr, &env, sizeof(env));
        }
        setReturnS32(ctx, 0);
    }

    void sceGsResetGraph(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t mode = getRegU32(ctx, 4);
        uint32_t interlace = getRegU32(ctx, 5);
        uint32_t omode = getRegU32(ctx, 6);
        uint32_t ffmode = getRegU32(ctx, 7);

        if (mode == 0)
        {
            g_gparam.interlace = static_cast<uint8_t>(interlace & 0x1);
            g_gparam.omode = static_cast<uint8_t>(omode & 0xFF);
            g_gparam.ffmode = static_cast<uint8_t>(ffmode & 0x1);
            writeGsGParamToScratch(runtime);

            auto &gs = runtime->memory().gs();
            gs.pmode = makePmode(1, 0, 0, 0, 0, 0x80);
            gs.smode2 = (interlace & 0x1) | ((ffmode & 0x1) << 1);
            gs.dispfb1 = makeDispFb(0, 10, 0, 0, 0);
            gs.display1 = makeDisplay(0, 0, 0, 0, 639, 447);
        }

        setReturnS32(ctx, 0);
    }

    void sceGsResetPath(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceGsSetDefClear(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsSetDefClear", rdram, ctx, runtime);
    }

    void sceGsSetDefDBuffDc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceGsSetDefDispEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t envAddr = getRegU32(ctx, 4);
        uint32_t psm = getRegU32(ctx, 5);
        uint32_t w = getRegU32(ctx, 6);
        uint32_t h = getRegU32(ctx, 7);
        uint32_t dx = readStackU32(rdram, ctx, 16);
        uint32_t dy = readStackU32(rdram, ctx, 20);

        if (w == 0)
            w = 640;
        if (h == 0)
            h = 448;

        uint32_t fbw = (w + 63) / 64;
        uint64_t dispfb = makeDispFb(0, fbw, psm, 0, 0);
        uint64_t display = makeDisplay(dx, dy, 0, 0, w - 1, h - 1);

        writeGsDispEnv(rdram, envAddr, display, dispfb);
        setReturnS32(ctx, 0);
    }

    void sceGsSetDefDrawEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsSetDefDrawEnv", rdram, ctx, runtime);
    }

    void sceGsSetDefDrawEnv2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsSetDefDrawEnv2", rdram, ctx, runtime);
    }

    void sceGsSetDefLoadImage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t imgAddr = getRegU32(ctx, 4);
        const GsSetDefImageArgs args = decodeGsSetDefImageArgs(rdram, ctx);

        GsImageMem img{};
        img.x = static_cast<uint16_t>(args.x);
        img.y = static_cast<uint16_t>(args.y);
        img.width = static_cast<uint16_t>(args.width);
        img.height = static_cast<uint16_t>(args.height);
        img.vram_addr = static_cast<uint16_t>(args.vramAddr);
        img.vram_width = static_cast<uint8_t>(args.vramWidth);
        img.psm = static_cast<uint8_t>(args.psm);

        writeGsImage(rdram, imgAddr, img);
        setReturnS32(ctx, 0);
    }

    void sceGsSetDefStoreImage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        sceGsSetDefLoadImage(rdram, ctx, runtime);
    }

    void sceGsSwapDBuffDc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // can we get away with that ? kkkk
        static int cur = 0;
        cur ^= 1;
        setReturnS32(ctx, cur);
    }

    void sceGsSyncPath(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceGsSyncV(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceGsSyncVCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceGszbufaddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGszbufaddr", rdram, ctx, runtime);
    }

    void sceIoctl(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceIoctl", rdram, ctx, runtime);
    }

    void sceIpuInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceIpuInit", rdram, ctx, runtime);
    }

    void sceIpuRestartDMA(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceIpuRestartDMA", rdram, ctx, runtime);
    }

    void sceIpuStopDMA(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceIpuStopDMA", rdram, ctx, runtime);
    }

    void sceIpuSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceIpuSync", rdram, ctx, runtime);
    }

    void sceLseek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::fioLseek(rdram, ctx, runtime);
    }

    void sceMcChangeThreadPriority(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcChangeThreadPriority", rdram, ctx, runtime);
    }

    void sceMcChdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcChdir", rdram, ctx, runtime);
    }

    void sceMcClose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcClose", rdram, ctx, runtime);
    }

    void sceMcDelete(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcDelete", rdram, ctx, runtime);
    }

    void sceMcFlush(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcFlush", rdram, ctx, runtime);
    }

    void sceMcFormat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcFormat", rdram, ctx, runtime);
    }

    void sceMcGetDir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcGetDir", rdram, ctx, runtime);
    }

    void sceMcGetEntSpace(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcGetEntSpace", rdram, ctx, runtime);
    }

    void sceMcGetInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcGetInfo", rdram, ctx, runtime);
    }

    void sceMcGetSlotMax(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcGetSlotMax", rdram, ctx, runtime);
    }

    void sceMcInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static uint32_t logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub sceMcInit -> 0" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void sceMcMkdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcMkdir", rdram, ctx, runtime);
    }

    void sceMcOpen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcOpen", rdram, ctx, runtime);
    }

    void sceMcRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcRead", rdram, ctx, runtime);
    }

    void sceMcRename(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcRename", rdram, ctx, runtime);
    }

    void sceMcSeek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcSeek", rdram, ctx, runtime);
    }

    void sceMcSetFileInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcSetFileInfo", rdram, ctx, runtime);
    }

    void sceMcSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcSync", rdram, ctx, runtime);
    }

    void sceMcUnformat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcUnformat", rdram, ctx, runtime);
    }

    void sceMcWrite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcWrite", rdram, ctx, runtime);
    }

    void sceMpegAddBs(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegAddBs", rdram, ctx, runtime);
    }

    void sceMpegAddCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegAddCallback", rdram, ctx, runtime);
    }

    void sceMpegAddStrCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegAddStrCallback", rdram, ctx, runtime);
    }

    void sceMpegClearRefBuff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegClearRefBuff", rdram, ctx, runtime);
    }

    void sceMpegCreate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegCreate", rdram, ctx, runtime);
    }

    void sceMpegDelete(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegDelete", rdram, ctx, runtime);
    }

    void sceMpegDemuxPss(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegDemuxPss", rdram, ctx, runtime);
    }

    void sceMpegDemuxPssRing(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegDemuxPssRing", rdram, ctx, runtime);
    }

    void sceMpegDispCenterOffX(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegDispCenterOffX", rdram, ctx, runtime);
    }

    void sceMpegDispCenterOffY(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegDispCenterOffY", rdram, ctx, runtime);
    }

    void sceMpegDispHeight(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegDispHeight", rdram, ctx, runtime);
    }

    void sceMpegDispWidth(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegDispWidth", rdram, ctx, runtime);
    }

    void sceMpegGetDecodeMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegGetDecodeMode", rdram, ctx, runtime);
    }

    void sceMpegGetPicture(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegGetPicture", rdram, ctx, runtime);
    }

    void sceMpegGetPictureRAW8(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegGetPictureRAW8", rdram, ctx, runtime);
    }

    void sceMpegGetPictureRAW8xy(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegGetPictureRAW8xy", rdram, ctx, runtime);
    }

    void sceMpegInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegInit", rdram, ctx, runtime);
    }

    void sceMpegIsEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegIsEnd", rdram, ctx, runtime);
    }

    void sceMpegIsRefBuffEmpty(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegIsRefBuffEmpty", rdram, ctx, runtime);
    }

    void sceMpegReset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegReset", rdram, ctx, runtime);
    }

    void sceMpegResetDefaultPtsGap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegResetDefaultPtsGap", rdram, ctx, runtime);
    }

    void sceMpegSetDecodeMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegSetDecodeMode", rdram, ctx, runtime);
    }

    void sceMpegSetDefaultPtsGap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegSetDefaultPtsGap", rdram, ctx, runtime);
    }

    void sceMpegSetImageBuff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegSetImageBuff", rdram, ctx, runtime);
    }

    void sceOpen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::fioOpen(rdram, ctx, runtime);
    }

    void scePadEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadEnd", rdram, ctx, runtime);
    }

    void scePadEnterPressMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadEnterPressMode", rdram, ctx, runtime);
    }

    void scePadExitPressMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadExitPressMode", rdram, ctx, runtime);
    }

    void scePadGetButtonMask(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadGetButtonMask", rdram, ctx, runtime);
    }

    void scePadGetDmaStr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadGetDmaStr", rdram, ctx, runtime);
    }

    void scePadGetFrameCount(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadGetFrameCount", rdram, ctx, runtime);
    }

    void scePadGetModVersion(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        // Arbitrary non-zero module version.
        setReturnS32(ctx, 0x0200);
    }

    void scePadGetPortMax(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        setReturnS32(ctx, 2);
    }

    void scePadGetReqState(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        // 0 = completed/no pending request.
        setReturnS32(ctx, 0);
    }

    void scePadGetSlotMax(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        // Most games use one slot unless multitap is active.
        setReturnS32(ctx, 1);
    }

    void scePadGetState(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        // Pad state constants used by libpad: 6 means stable and ready.
        setReturnS32(ctx, 6);
    }

    void scePadInfoAct(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadInfoAct", rdram, ctx, runtime);
    }

    void scePadInfoComb(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadInfoComb", rdram, ctx, runtime);
    }

    void scePadInfoMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;

        const int32_t infoMode = static_cast<int32_t>(getRegU32(ctx, 6)); // a2
        const int32_t index = static_cast<int32_t>(getRegU32(ctx, 7));    // a3

        // Minimal DualShock-like capabilities to keep game-side pad setup paths alive.
        constexpr int32_t kPadTypeDualShock = 7;
        switch (infoMode)
        {
        case 1: // PAD_MODECURID
            setReturnS32(ctx, kPadTypeDualShock);
            return;
        case 2: // PAD_MODECUREXID
            setReturnS32(ctx, kPadTypeDualShock);
            return;
        case 3: // PAD_MODECUROFFS
            setReturnS32(ctx, 0);
            return;
        case 4: // PAD_MODETABLE
            if (index == -1)
            {
                setReturnS32(ctx, 1); // one available mode
            }
            else if (index == 0)
            {
                setReturnS32(ctx, kPadTypeDualShock);
            }
            else
            {
                setReturnS32(ctx, 0);
            }
            return;
        default:
            setReturnS32(ctx, 0);
            return;
        }
    }

    void scePadInfoPressMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        // Pressure mode is disabled in this minimal implementation.
        setReturnS32(ctx, 0);
    }

    void scePadInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        setReturnS32(ctx, 1);
    }

    void scePadInit2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        setReturnS32(ctx, 1);
    }

    void scePadPortClose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        setReturnS32(ctx, 1);
    }

    void scePadPortOpen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        setReturnS32(ctx, 1);
    }

    void scePadRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;

        const uint32_t dataAddr = getRegU32(ctx, 6); // a2
        uint8_t *data = getMemPtr(rdram, dataAddr);
        if (!data)
        {
            setReturnS32(ctx, 0);
            return;
        }

        // struct padButtonStatus (32 bytes): neutral state, no buttons pressed.
        std::memset(data, 0, 32);
        data[1] = 0x73; // analog/dualshock mode marker
        data[2] = 0xFF; // btns low (active-low)
        data[3] = 0xFF; // btns high
        data[4] = 0x80; // rjoy_h
        data[5] = 0x80; // rjoy_v
        data[6] = 0x80; // ljoy_h
        data[7] = 0x80; // ljoy_v

        setReturnS32(ctx, 1);
    }

    void scePadReqIntToStr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadReqIntToStr", rdram, ctx, runtime);
    }

    void scePadSetActAlign(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadSetActAlign", rdram, ctx, runtime);
    }

    void scePadSetActDirect(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadSetActDirect", rdram, ctx, runtime);
    }

    void scePadSetButtonInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadSetButtonInfo", rdram, ctx, runtime);
    }

    void scePadSetMainMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadSetMainMode", rdram, ctx, runtime);
    }

    void scePadSetReqState(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadSetReqState", rdram, ctx, runtime);
    }

    void scePadSetVrefParam(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadSetVrefParam", rdram, ctx, runtime);
    }

    void scePadSetWarningLevel(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadSetWarningLevel", rdram, ctx, runtime);
    }

    void scePadStateIntToStr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadStateIntToStr", rdram, ctx, runtime);
    }

    void scePrintf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePrintf", rdram, ctx, runtime);
    }

    void sceRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::fioRead(rdram, ctx, runtime);
    }

    void sceResetttyinit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceResetttyinit", rdram, ctx, runtime);
    }

    void sceSdCallBack(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSdCallBack", rdram, ctx, runtime);
    }

    void sceSdRemote(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSdRemote", rdram, ctx, runtime);
    }

    void sceSdRemoteInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSdRemoteInit", rdram, ctx, runtime);
    }

    void sceSdTransToIOP(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSdTransToIOP", rdram, ctx, runtime);
    }

    void sceSetBrokenLink(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSetBrokenLink", rdram, ctx, runtime);
    }

    void sceSetPtm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSetPtm", rdram, ctx, runtime);
    }

    void sceSifAddCmdHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifAddCmdHandler", rdram, ctx, runtime);
    }

    void sceSifAllocIopHeap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t reqSize = getRegU32(ctx, 4);
        const uint32_t alignedSize = (reqSize + (kIopHeapAlign - 1)) & ~(kIopHeapAlign - 1);
        if (alignedSize == 0 || g_iopHeapNext + alignedSize > kIopHeapLimit)
        {
            setReturnS32(ctx, 0);
            return;
        }

        const uint32_t allocAddr = g_iopHeapNext;
        g_iopHeapNext += alignedSize;
        setReturnS32(ctx, static_cast<int32_t>(allocAddr));
    }

    void sceSifBindRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::SifBindRpc(rdram, ctx, runtime);
    }

    void sceSifCheckStatRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::SifCheckStatRpc(rdram, ctx, runtime);
    }

    void sceSifDmaStat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifDmaStat", rdram, ctx, runtime);
    }

    void sceSifExecRequest(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceSifExitCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifExitCmd", rdram, ctx, runtime);
    }

    void sceSifExitRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceSifFreeIopHeap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceSifGetDataTable(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifGetDataTable", rdram, ctx, runtime);
    }

    void sceSifGetIopAddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifGetIopAddr", rdram, ctx, runtime);
    }

    void sceSifGetNextRequest(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceSifGetOtherData(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceSifGetReg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifGetReg", rdram, ctx, runtime);
    }

    void sceSifGetSreg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifGetSreg", rdram, ctx, runtime);
    }

    void sceSifInitCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifInitCmd", rdram, ctx, runtime);
    }

    void sceSifInitIopHeap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_iopHeapNext = kIopHeapBase;
        setReturnS32(ctx, 0);
    }

    void sceSifInitRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::SifInitRpc(rdram, ctx, runtime);
    }

    void sceSifIsAliveIop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifIsAliveIop", rdram, ctx, runtime);
    }

    void sceSifLoadElf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::sceSifLoadElf(rdram, ctx, runtime);
    }

    void sceSifLoadElfPart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::sceSifLoadElfPart(rdram, ctx, runtime);
    }

    void sceSifLoadFileReset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifLoadFileReset", rdram, ctx, runtime);
    }

    void sceSifLoadIopHeap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceSifLoadModuleBuffer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::sceSifLoadModuleBuffer(rdram, ctx, runtime);
    }

    void sceSifRebootIop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void sceSifRegisterRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::SifRegisterRpc(rdram, ctx, runtime);
    }

    void sceSifRemoveCmdHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifRemoveCmdHandler", rdram, ctx, runtime);
    }

    void sceSifRemoveRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::SifRemoveRpc(rdram, ctx, runtime);
    }

    void sceSifRemoveRpcQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::SifRemoveRpcQueue(rdram, ctx, runtime);
    }

    void sceSifResetIop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifResetIop", rdram, ctx, runtime);
    }

    void sceSifRpcLoop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceSifSetCmdBuffer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifSetCmdBuffer", rdram, ctx, runtime);
    }

    void sceSifSetDChain(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifSetDChain", rdram, ctx, runtime);
    }

    void sceSifSetDma(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifSetDma", rdram, ctx, runtime);
    }

    void sceSifSetIopAddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifSetIopAddr", rdram, ctx, runtime);
    }

    void sceSifSetReg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifSetReg", rdram, ctx, runtime);
    }

    void sceSifSetRpcQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::SifSetRpcQueue(rdram, ctx, runtime);
    }

    void sceSifSetSreg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifSetSreg", rdram, ctx, runtime);
    }

    void sceSifSetSysCmdBuffer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifSetSysCmdBuffer", rdram, ctx, runtime);
    }

    void sceSifStopDma(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifStopDma", rdram, ctx, runtime);
    }

    void sceSifSyncIop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void sceSifWriteBackDCache(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifWriteBackDCache", rdram, ctx, runtime);
    }

    void sceSSyn_BreakAtick(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_BreakAtick", rdram, ctx, runtime);
    }

    void sceSSyn_ClearBreakAtick(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_ClearBreakAtick", rdram, ctx, runtime);
    }

    void sceSSyn_SendExcMsg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SendExcMsg", rdram, ctx, runtime);
    }

    void sceSSyn_SendNrpnMsg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SendNrpnMsg", rdram, ctx, runtime);
    }

    void sceSSyn_SendRpnMsg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SendRpnMsg", rdram, ctx, runtime);
    }

    void sceSSyn_SendShortMsg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SendShortMsg", rdram, ctx, runtime);
    }

    void sceSSyn_SetChPriority(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SetChPriority", rdram, ctx, runtime);
    }

    void sceSSyn_SetMasterVolume(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SetMasterVolume", rdram, ctx, runtime);
    }

    void sceSSyn_SetOutPortVolume(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SetOutPortVolume", rdram, ctx, runtime);
    }

    void sceSSyn_SetOutputAssign(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SetOutputAssign", rdram, ctx, runtime);
    }

    void sceSSyn_SetOutputMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceSSyn_SetPortMaxPoly(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SetPortMaxPoly", rdram, ctx, runtime);
    }

    void sceSSyn_SetPortVolume(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SetPortVolume", rdram, ctx, runtime);
    }

    void sceSSyn_SetTvaEnvMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SetTvaEnvMode", rdram, ctx, runtime);
    }

    void sceSynthesizerAmpProcI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerAmpProcI", rdram, ctx, runtime);
    }

    void sceSynthesizerAmpProcNI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerAmpProcNI", rdram, ctx, runtime);
    }

    void sceSynthesizerAssignAllNoteOff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerAssignAllNoteOff", rdram, ctx, runtime);
    }

    void sceSynthesizerAssignAllSoundOff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerAssignAllSoundOff", rdram, ctx, runtime);
    }

    void sceSynthesizerAssignHoldChange(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerAssignHoldChange", rdram, ctx, runtime);
    }

    void sceSynthesizerAssignNoteOff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerAssignNoteOff", rdram, ctx, runtime);
    }

    void sceSynthesizerAssignNoteOn(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerAssignNoteOn", rdram, ctx, runtime);
    }

    void sceSynthesizerCalcEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerCalcEnv", rdram, ctx, runtime);
    }

    void sceSynthesizerCalcPortamentPitch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerCalcPortamentPitch", rdram, ctx, runtime);
    }

    void sceSynthesizerCalcTvfCoefAll(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerCalcTvfCoefAll", rdram, ctx, runtime);
    }

    void sceSynthesizerCalcTvfCoefF0(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerCalcTvfCoefF0", rdram, ctx, runtime);
    }

    void sceSynthesizerCent2PhaseInc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerCent2PhaseInc", rdram, ctx, runtime);
    }

    void sceSynthesizerChangeEffectSend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangeEffectSend", rdram, ctx, runtime);
    }

    void sceSynthesizerChangeHsPanpot(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangeHsPanpot", rdram, ctx, runtime);
    }

    void sceSynthesizerChangeNrpnCutOff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangeNrpnCutOff", rdram, ctx, runtime);
    }

    void sceSynthesizerChangeNrpnLfoDepth(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangeNrpnLfoDepth", rdram, ctx, runtime);
    }

    void sceSynthesizerChangeNrpnLfoRate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangeNrpnLfoRate", rdram, ctx, runtime);
    }

    void sceSynthesizerChangeOutAttrib(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangeOutAttrib", rdram, ctx, runtime);
    }

    void sceSynthesizerChangeOutVol(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangeOutVol", rdram, ctx, runtime);
    }

    void sceSynthesizerChangePanpot(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangePanpot", rdram, ctx, runtime);
    }

    void sceSynthesizerChangePartBendSens(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangePartBendSens", rdram, ctx, runtime);
    }

    void sceSynthesizerChangePartExpression(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangePartExpression", rdram, ctx, runtime);
    }

    void sceSynthesizerChangePartHsExpression(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangePartHsExpression", rdram, ctx, runtime);
    }

    void sceSynthesizerChangePartHsPitchBend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangePartHsPitchBend", rdram, ctx, runtime);
    }

    void sceSynthesizerChangePartModuration(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangePartModuration", rdram, ctx, runtime);
    }

    void sceSynthesizerChangePartPitchBend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangePartPitchBend", rdram, ctx, runtime);
    }

    void sceSynthesizerChangePartVolume(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangePartVolume", rdram, ctx, runtime);
    }

    void sceSynthesizerChangePortamento(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangePortamento", rdram, ctx, runtime);
    }

    void sceSynthesizerChangePortamentoTime(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangePortamentoTime", rdram, ctx, runtime);
    }

    void sceSynthesizerClearKeyMap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerClearKeyMap", rdram, ctx, runtime);
    }

    void sceSynthesizerClearSpr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerClearSpr", rdram, ctx, runtime);
    }

    void sceSynthesizerCopyOutput(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerCopyOutput", rdram, ctx, runtime);
    }

    void sceSynthesizerDmaFromSPR(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerDmaFromSPR", rdram, ctx, runtime);
    }

    void sceSynthesizerDmaSpr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerDmaSpr", rdram, ctx, runtime);
    }

    void sceSynthesizerDmaToSPR(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerDmaToSPR", rdram, ctx, runtime);
    }

    void sceSynthesizerGetPartial(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerGetPartial", rdram, ctx, runtime);
    }

    void sceSynthesizerGetPartOutLevel(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerGetPartOutLevel", rdram, ctx, runtime);
    }

    void sceSynthesizerGetSampleParam(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerGetSampleParam", rdram, ctx, runtime);
    }

    void sceSynthesizerHsMessage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerHsMessage", rdram, ctx, runtime);
    }

    void sceSynthesizerLfoNone(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerLfoNone", rdram, ctx, runtime);
    }

    void sceSynthesizerLfoProc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerLfoProc", rdram, ctx, runtime);
    }

    void sceSynthesizerLfoSawDown(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerLfoSawDown", rdram, ctx, runtime);
    }

    void sceSynthesizerLfoSawUp(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerLfoSawUp", rdram, ctx, runtime);
    }

    void sceSynthesizerLfoSquare(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerLfoSquare", rdram, ctx, runtime);
    }

    void sceSynthesizerReadNoise(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerReadNoise", rdram, ctx, runtime);
    }

    void sceSynthesizerReadNoiseAdd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerReadNoiseAdd", rdram, ctx, runtime);
    }

    void sceSynthesizerReadSample16(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerReadSample16", rdram, ctx, runtime);
    }

    void sceSynthesizerReadSample16Add(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerReadSample16Add", rdram, ctx, runtime);
    }

    void sceSynthesizerReadSample8(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerReadSample8", rdram, ctx, runtime);
    }

    void sceSynthesizerReadSample8Add(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerReadSample8Add", rdram, ctx, runtime);
    }

    void sceSynthesizerResetPart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerResetPart", rdram, ctx, runtime);
    }

    void sceSynthesizerRestorDma(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerRestorDma", rdram, ctx, runtime);
    }

    void sceSynthesizerSelectPatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSelectPatch", rdram, ctx, runtime);
    }

    void sceSynthesizerSendShortMessage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSendShortMessage", rdram, ctx, runtime);
    }

    void sceSynthesizerSetMasterVolume(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetMasterVolume", rdram, ctx, runtime);
    }

    void sceSynthesizerSetRVoice(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetRVoice", rdram, ctx, runtime);
    }

    void sceSynthesizerSetupDma(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetupDma", rdram, ctx, runtime);
    }

    void sceSynthesizerSetupLfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetupLfo", rdram, ctx, runtime);
    }

    void sceSynthesizerSetupMidiModuration(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetupMidiModuration", rdram, ctx, runtime);
    }

    void sceSynthesizerSetupMidiPanpot(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetupMidiPanpot", rdram, ctx, runtime);
    }

    void sceSynthesizerSetupNewNoise(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetupNewNoise", rdram, ctx, runtime);
    }

    void sceSynthesizerSetupReleaseEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetupReleaseEnv", rdram, ctx, runtime);
    }

    void sceSynthesizerSetuptEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetuptEnv", rdram, ctx, runtime);
    }

    void sceSynthesizerSetupTruncateTvaEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetupTruncateTvaEnv", rdram, ctx, runtime);
    }

    void sceSynthesizerSetupTruncateTvfPitchEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetupTruncateTvfPitchEnv", rdram, ctx, runtime);
    }

    void sceSynthesizerTonegenerator(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerTonegenerator", rdram, ctx, runtime);
    }

    void sceSynthesizerTransposeMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerTransposeMatrix", rdram, ctx, runtime);
    }

    void sceSynthesizerTvfProcI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerTvfProcI", rdram, ctx, runtime);
    }

    void sceSynthesizerTvfProcNI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerTvfProcNI", rdram, ctx, runtime);
    }

    void sceSynthesizerWaitDmaFromSPR(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerWaitDmaFromSPR", rdram, ctx, runtime);
    }

    void sceSynthesizerWaitDmaToSPR(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerWaitDmaToSPR", rdram, ctx, runtime);
    }

    void sceSynthsizerGetDrumPatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthsizerGetDrumPatch", rdram, ctx, runtime);
    }

    void sceSynthsizerGetMeloPatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthsizerGetMeloPatch", rdram, ctx, runtime);
    }

    void sceSynthsizerLfoNoise(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthsizerLfoNoise", rdram, ctx, runtime);
    }

    void sceSynthSizerLfoTriangle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthSizerLfoTriangle", rdram, ctx, runtime);
    }

    void sceTtyHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceTtyHandler", rdram, ctx, runtime);
    }

    void sceTtyInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceTtyInit", rdram, ctx, runtime);
    }

    void sceTtyRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceTtyRead", rdram, ctx, runtime);
    }

    void sceTtyWrite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceTtyWrite", rdram, ctx, runtime);
    }

    void sceVpu0Reset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceVu0AddVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0AddVector", rdram, ctx, runtime);
    }

    void sceVu0ApplyMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ApplyMatrix", rdram, ctx, runtime);
    }

    void sceVu0CameraMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0CameraMatrix", rdram, ctx, runtime);
    }

    void sceVu0ClampVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ClampVector", rdram, ctx, runtime);
    }

    void sceVu0ClipAll(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ClipAll", rdram, ctx, runtime);
    }

    void sceVu0ClipScreen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ClipScreen", rdram, ctx, runtime);
    }

    void sceVu0ClipScreen3(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ClipScreen3", rdram, ctx, runtime);
    }

    void sceVu0CopyMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0CopyMatrix", rdram, ctx, runtime);
    }

    void sceVu0CopyVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0CopyVector", rdram, ctx, runtime);
    }

    void sceVu0CopyVectorXYZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0CopyVectorXYZ", rdram, ctx, runtime);
    }

    void sceVu0DivVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0DivVector", rdram, ctx, runtime);
    }

    void sceVu0DivVectorXYZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0DivVectorXYZ", rdram, ctx, runtime);
    }

    void sceVu0DropShadowMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0DropShadowMatrix", rdram, ctx, runtime);
    }

    void sceVu0FTOI0Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0FTOI0Vector", rdram, ctx, runtime);
    }

    void sceVu0FTOI4Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0FTOI4Vector", rdram, ctx, runtime);
    }

    void sceVu0InnerProduct(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0InnerProduct", rdram, ctx, runtime);
    }

    void sceVu0InterVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0InterVector", rdram, ctx, runtime);
    }

    void sceVu0InterVectorXYZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0InterVectorXYZ", rdram, ctx, runtime);
    }

    void sceVu0InversMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0InversMatrix", rdram, ctx, runtime);
    }

    void sceVu0ITOF0Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ITOF0Vector", rdram, ctx, runtime);
    }

    void sceVu0ITOF12Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ITOF12Vector", rdram, ctx, runtime);
    }

    void sceVu0ITOF4Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ITOF4Vector", rdram, ctx, runtime);
    }

    void sceVu0LightColorMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0LightColorMatrix", rdram, ctx, runtime);
    }

    void sceVu0MulMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0MulMatrix", rdram, ctx, runtime);
    }

    void sceVu0MulVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0MulVector", rdram, ctx, runtime);
    }

    void sceVu0Normalize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0Normalize", rdram, ctx, runtime);
    }

    void sceVu0NormalLightMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0NormalLightMatrix", rdram, ctx, runtime);
    }

    void sceVu0OuterProduct(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0OuterProduct", rdram, ctx, runtime);
    }

    void sceVu0RotMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0RotMatrix", rdram, ctx, runtime);
    }

    void sceVu0RotMatrixX(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0RotMatrixX", rdram, ctx, runtime);
    }

    void sceVu0RotMatrixY(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0RotMatrixY", rdram, ctx, runtime);
    }

    void sceVu0RotMatrixZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0RotMatrixZ", rdram, ctx, runtime);
    }

    void sceVu0RotTransPers(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0RotTransPers", rdram, ctx, runtime);
    }

    void sceVu0RotTransPersN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0RotTransPersN", rdram, ctx, runtime);
    }

    void sceVu0ScaleVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ScaleVector", rdram, ctx, runtime);
    }

    void sceVu0ScaleVectorXYZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ScaleVectorXYZ", rdram, ctx, runtime);
    }

    void sceVu0SubVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0SubVector", rdram, ctx, runtime);
    }

    void sceVu0TransMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0TransMatrix", rdram, ctx, runtime);
    }

    void sceVu0TransposeMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0TransposeMatrix", rdram, ctx, runtime);
    }

    void sceVu0UnitMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dstAddr = getRegU32(ctx, 4); // sceVu0FMATRIX dst
        alignas(16) const float identity[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f};

        if (!writeGuestBytes(rdram, runtime, dstAddr, reinterpret_cast<const uint8_t *>(identity), sizeof(identity)))
        {
            static uint32_t warnCount = 0;
            if (warnCount < 8)
            {
                std::cerr << "sceVu0UnitMatrix: failed to write matrix at 0x"
                          << std::hex << dstAddr << std::dec << std::endl;
                ++warnCount;
            }
        }

        setReturnS32(ctx, 0);
    }

    void sceVu0ViewScreenMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ViewScreenMatrix", rdram, ctx, runtime);
    }

    void sceWrite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::fioWrite(rdram, ctx, runtime);
    }

    void srand(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("srand", rdram, ctx, runtime);
    }

    void stat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("stat", rdram, ctx, runtime);
    }

    void strcasecmp(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("strcasecmp", rdram, ctx, runtime);
    }

    void vfprintf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t file_handle = getRegU32(ctx, 4);  // $a0
        uint32_t format_addr = getRegU32(ctx, 5);  // $a1
        uint32_t va_list_addr = getRegU32(ctx, 6); // $a2
        FILE *fp = get_file_ptr(file_handle);
        const std::string formatOwned = readPs2CStringBounded(rdram, runtime, format_addr, 1024);
        int ret = -1;

        if (fp && format_addr != 0)
        {
            std::string rendered = formatPs2StringWithVaList(rdram, runtime, formatOwned.c_str(), va_list_addr);
            ret = std::fprintf(fp, "%s", rendered.c_str());
        }
        else
        {
            std::cerr << "vfprintf error: Invalid file handle or format address."
                      << " Handle: 0x" << std::hex << file_handle << " (file valid: " << (fp != nullptr) << ")"
                      << ", Format: 0x" << format_addr << std::dec
                      << std::endl;
        }

        setReturnS32(ctx, ret);
    }

    void vsprintf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t str_addr = getRegU32(ctx, 4);     // $a0
        uint32_t format_addr = getRegU32(ctx, 5);  // $a1
        uint32_t va_list_addr = getRegU32(ctx, 6); // $a2
        const std::string formatOwned = readPs2CStringBounded(rdram, runtime, format_addr, 1024);
        int ret = -1;

        if (format_addr != 0)
        {
            std::string rendered = formatPs2StringWithVaList(rdram, runtime, formatOwned.c_str(), va_list_addr);
            if (writeGuestBytes(rdram, runtime, str_addr, reinterpret_cast<const uint8_t *>(rendered.c_str()), rendered.size() + 1u))
            {
                ret = static_cast<int>(rendered.size());
            }
            else
            {
                std::cerr << "vsprintf error: Failed to write destination buffer at 0x"
                          << std::hex << str_addr << std::dec << std::endl;
            }
        }
        else
        {
            std::cerr << "vsprintf error: Invalid address provided."
                      << " Dest: 0x" << std::hex << str_addr
                      << ", Format: 0x" << format_addr << std::dec
                      << std::endl;
        }

        setReturnS32(ctx, ret);
    }

    void write(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::fioWrite(rdram, ctx, runtime);
    }

    void TODO(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("unknown", rdram, ctx, runtime);
    }

    void TODO_NAMED(const char *name, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const std::string stubName = name ? name : "unknown";
        uint32_t callCount = 0;
        {
            std::lock_guard<std::mutex> lock(g_stubWarningMutex);
            callCount = ++g_stubWarningCount[stubName];
        }

        if (callCount > kMaxStubWarningsPerName)
        {
            if (callCount == (kMaxStubWarningsPerName + 1))
            {
                std::cerr << "Warning: Further calls to PS2 stub '" << stubName
                          << "' are suppressed after " << kMaxStubWarningsPerName << " warnings" << std::endl;
            }
            setReturnS32(ctx, -1);
            return;
        }

        uint32_t stub_num = getRegU32(ctx, 2);   // $v0
        uint32_t caller_ra = getRegU32(ctx, 31); // $ra

        std::cerr << "Warning: Unimplemented PS2 stub called. name=" << stubName
                  << " PC=0x" << std::hex << ctx->pc
                  << ", RA=0x" << caller_ra
                  << ", Stub# guess (from $v0)=0x" << stub_num << std::dec << std::endl;

        // More context for debugging
        std::cerr << "  Args: $a0=0x" << std::hex << getRegU32(ctx, 4)
                  << ", $a1=0x" << getRegU32(ctx, 5)
                  << ", $a2=0x" << getRegU32(ctx, 6)
                  << ", $a3=0x" << getRegU32(ctx, 7) << std::dec << std::endl;

        setReturnS32(ctx, -1); // Return error
    }
}
