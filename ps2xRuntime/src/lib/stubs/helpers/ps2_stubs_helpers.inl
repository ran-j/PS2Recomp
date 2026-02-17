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
