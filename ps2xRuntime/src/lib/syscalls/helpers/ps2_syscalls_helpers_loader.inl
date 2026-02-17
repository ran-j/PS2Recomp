namespace
{
    std::string readGuestCStringBounded(const uint8_t *rdram, uint32_t guestAddr, size_t maxBytes)
    {
        std::string out;
        if (!rdram || guestAddr == 0 || maxBytes == 0)
        {
            return out;
        }

        out.reserve(maxBytes);
        for (size_t i = 0; i < maxBytes; ++i)
        {
            const char ch = static_cast<char>(rdram[(guestAddr + static_cast<uint32_t>(i)) & PS2_RAM_MASK]);
            if (ch == '\0')
            {
                break;
            }
            out.push_back(ch);
        }
        return out;
    }

    std::string normalizeSifModulePathKey(const std::string &path)
    {
        return toLowerAscii(normalizePs2PathSuffix(path));
    }

    uint64_t hashGuestBytesFnv1a64(const uint8_t *rdram, uint32_t guestAddr, size_t byteCount)
    {
        constexpr uint64_t kOffset = 1469598103934665603ull;
        constexpr uint64_t kPrime = 1099511628211ull;

        if (!rdram || guestAddr == 0 || byteCount == 0)
        {
            return 0ull;
        }

        uint64_t hash = kOffset;
        for (size_t i = 0; i < byteCount; ++i)
        {
            const uint8_t b = rdram[(guestAddr + static_cast<uint32_t>(i)) & PS2_RAM_MASK];
            hash ^= static_cast<uint64_t>(b);
            hash *= kPrime;
        }
        return hash;
    }

    std::string makeSifModuleBufferTag(const uint8_t *rdram, uint32_t bufferAddr)
    {
        char key[96] = {};
        const uint64_t hash = hashGuestBytesFnv1a64(rdram, bufferAddr, kSifModuleBufferProbeBytes);
        std::snprintf(key, sizeof(key), "iopbuf:fnv64:%016llx", static_cast<unsigned long long>(hash));
        return std::string(key);
    }

    void logSifModuleAction(const char *op, int32_t moduleId, const std::string &path, uint32_t refCount)
    {
        if (!op)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(g_sif_module_mutex);
        if (g_sif_module_log_count >= kMaxSifModuleLogs)
        {
            return;
        }

        std::cout << "[SIF module] " << op
                  << " id=" << moduleId
                  << " ref=" << refCount
                  << " path=\"" << path << "\""
                  << std::endl;
        ++g_sif_module_log_count;
    }

    int32_t trackSifModuleLoad(const std::string &path)
    {
        if (path.empty())
        {
            return -1;
        }

        const std::string pathKey = normalizeSifModulePathKey(path);
        if (pathKey.empty())
        {
            return -1;
        }

        std::lock_guard<std::mutex> lock(g_sif_module_mutex);

        auto byPathIt = g_sif_module_id_by_path.find(pathKey);
        if (byPathIt != g_sif_module_id_by_path.end())
        {
            auto byIdIt = g_sif_modules_by_id.find(byPathIt->second);
            if (byIdIt != g_sif_modules_by_id.end())
            {
                SifModuleRecord &record = byIdIt->second;
                record.loaded = true;
                ++record.refCount;
                return record.id;
            }
        }

        if (g_next_sif_module_id <= 0)
        {
            g_next_sif_module_id = 1;
        }

        const int32_t moduleId = g_next_sif_module_id++;
        SifModuleRecord record;
        record.id = moduleId;
        record.path = path;
        record.pathKey = pathKey;
        record.refCount = 1;
        record.loaded = true;

        g_sif_module_id_by_path[pathKey] = moduleId;
        g_sif_modules_by_id[moduleId] = record;
        return moduleId;
    }

    bool trackSifModuleStop(int32_t moduleId, uint32_t *remainingRefs = nullptr)
    {
        if (moduleId <= 0)
        {
            if (remainingRefs)
            {
                *remainingRefs = 0;
            }
            return false;
        }

        std::lock_guard<std::mutex> lock(g_sif_module_mutex);
        auto it = g_sif_modules_by_id.find(moduleId);
        if (it == g_sif_modules_by_id.end())
        {
            if (remainingRefs)
            {
                *remainingRefs = 0;
            }
            return false;
        }

        SifModuleRecord &record = it->second;
        if (record.refCount > 0)
        {
            --record.refCount;
        }
        record.loaded = (record.refCount != 0);

        if (remainingRefs)
        {
            *remainingRefs = record.refCount;
        }
        return true;
    }

    bool readFileBlockAt(std::ifstream &file, uint64_t offset, void *dst, size_t byteCount)
    {
        if (!dst || byteCount == 0)
        {
            return false;
        }

        file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!file)
        {
            return false;
        }

        file.read(reinterpret_cast<char *>(dst), static_cast<std::streamsize>(byteCount));
        return file.gcount() == static_cast<std::streamsize>(byteCount);
    }

    bool tryExtractElfGpValue(std::ifstream &file, const Elf32Header &header, uint32_t &gpOut)
    {
        uint8_t regInfo[24] = {};

        for (uint32_t i = 0; i < header.phnum; ++i)
        {
            Elf32ProgramHeader ph{};
            const uint64_t phOffset = static_cast<uint64_t>(header.phoff) + static_cast<uint64_t>(i) * header.phentsize;
            if (!readFileBlockAt(file, phOffset, &ph, sizeof(ph)))
            {
                return false;
            }

            if (ph.type == kElfPtMipsRegInfo && ph.filesz >= sizeof(regInfo))
            {
                if (!readFileBlockAt(file, ph.offset, regInfo, sizeof(regInfo)))
                {
                    return false;
                }
                std::memcpy(&gpOut, regInfo + 20u, sizeof(gpOut));
                return true;
            }
        }

        for (uint32_t i = 0; i < header.shnum; ++i)
        {
            Elf32SectionHeader sh{};
            const uint64_t shOffset = static_cast<uint64_t>(header.shoff) + static_cast<uint64_t>(i) * header.shentsize;
            if (!readFileBlockAt(file, shOffset, &sh, sizeof(sh)))
            {
                return false;
            }

            if (sh.type == kElfShtMipsRegInfo && sh.size >= sizeof(regInfo))
            {
                if (!readFileBlockAt(file, sh.offset, regInfo, sizeof(regInfo)))
                {
                    return false;
                }
                std::memcpy(&gpOut, regInfo + 20u, sizeof(gpOut));
                return true;
            }
        }

        return false;
    }

    bool loadElfIntoGuestMemory(const std::string &hostPath,
                                uint8_t *rdram,
                                PS2Runtime *runtime,
                                const std::string &sectionName,
                                GuestExecData &execDataOut,
                                std::string &errorOut)
    {
        if (!rdram || hostPath.empty())
        {
            errorOut = "invalid path or RDRAM pointer";
            return false;
        }

        std::ifstream file(hostPath, std::ios::binary);
        if (!file)
        {
            errorOut = "failed to open ELF";
            return false;
        }

        Elf32Header header{};
        if (!readFileBlockAt(file, 0, &header, sizeof(header)))
        {
            errorOut = "failed to read ELF header";
            return false;
        }

        if (header.magic != kElfMagic || header.machine != kElfMachineMips || header.type != kElfTypeExec)
        {
            errorOut = "not a MIPS executable ELF";
            return false;
        }

        bool loadedAny = false;
        const bool loadAll = sectionName.empty() || toLowerAscii(sectionName) == "all";
        static uint32_t secFilterLogCount = 0;
        if (!loadAll && secFilterLogCount < 8u)
        {
            std::cout << "[SifLoadElfPart] section filter \"" << sectionName
                      << "\" requested; loading PT_LOAD segments only." << std::endl;
            ++secFilterLogCount;
        }

        for (uint32_t i = 0; i < header.phnum; ++i)
        {
            Elf32ProgramHeader ph{};
            const uint64_t phOffset = static_cast<uint64_t>(header.phoff) + static_cast<uint64_t>(i) * header.phentsize;
            if (!readFileBlockAt(file, phOffset, &ph, sizeof(ph)))
            {
                errorOut = "failed to read ELF program headers";
                return false;
            }

            if (ph.type != kElfPtLoad || ph.memsz == 0u)
            {
                continue;
            }
            if (ph.filesz > ph.memsz)
            {
                errorOut = "ELF segment filesz > memsz";
                return false;
            }

            const uint64_t memSize64 = static_cast<uint64_t>(ph.memsz);
            if (runtime && ph.vaddr >= PS2_SCRATCHPAD_BASE && ph.vaddr < (PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE))
            {
                const uint32_t scratchOffset = runtime->memory().translateAddress(ph.vaddr);
                if (static_cast<uint64_t>(scratchOffset) + memSize64 > PS2_SCRATCHPAD_SIZE)
                {
                    errorOut = "ELF scratchpad segment out of range";
                    return false;
                }

                uint8_t *dest = runtime->memory().getScratchpad() + scratchOffset;
                if (ph.filesz > 0u)
                {
                    if (!readFileBlockAt(file, ph.offset, dest, ph.filesz))
                    {
                        errorOut = "failed to read ELF segment payload";
                        return false;
                    }
                }
                if (ph.memsz > ph.filesz)
                {
                    std::memset(dest + ph.filesz, 0, ph.memsz - ph.filesz);
                }
            }
            else
            {
                const uint32_t physAddr = runtime ? runtime->memory().translateAddress(ph.vaddr) : (ph.vaddr & PS2_RAM_MASK);
                if (static_cast<uint64_t>(physAddr) + memSize64 > PS2_RAM_SIZE)
                {
                    errorOut = "ELF RDRAM segment out of range";
                    return false;
                }

                uint8_t *dest = rdram + physAddr;
                if (ph.filesz > 0u)
                {
                    if (!readFileBlockAt(file, ph.offset, dest, ph.filesz))
                    {
                        errorOut = "failed to read ELF segment payload";
                        return false;
                    }
                }
                if (ph.memsz > ph.filesz)
                {
                    std::memset(dest + ph.filesz, 0, ph.memsz - ph.filesz);
                }
            }

            loadedAny = true;
        }

        if (!loadedAny)
        {
            errorOut = "ELF has no loadable segments";
            return false;
        }

        execDataOut.epc = header.entry;
        execDataOut.gp = 0u;
        execDataOut.sp = 0u;
        execDataOut.dummy = 0u;

        uint32_t gpValue = 0u;
        if (tryExtractElfGpValue(file, header, gpValue))
        {
            execDataOut.gp = gpValue;
        }

        return true;
    }

    int32_t runSifLoadElfPart(uint8_t *rdram,
                              R5900Context *ctx,
                              PS2Runtime *runtime,
                              uint32_t pathAddr,
                              const std::string &sectionName,
                              uint32_t execDataAddr)
    {
        if (!rdram || !ctx)
        {
            return -1;
        }

        const std::string ps2Path = readGuestCStringBounded(rdram, pathAddr, kLoadfilePathMaxBytes);
        if (ps2Path.empty())
        {
            return -1;
        }

        const std::string hostPath = translatePs2Path(ps2Path.c_str());
        if (hostPath.empty())
        {
            return -1;
        }

        GuestExecData execData{};
        std::string loadError;
        if (!loadElfIntoGuestMemory(hostPath, rdram, runtime, sectionName, execData, loadError))
        {
            static uint32_t logCount = 0;
            if (logCount < 16u)
            {
                std::cerr << "[SifLoadElfPart] failed path=\"" << ps2Path << "\" host=\"" << hostPath
                          << "\" reason=" << loadError << std::endl;
                ++logCount;
            }
            return -1;
        }

        if (execData.gp == 0u)
        {
            execData.gp = getRegU32(ctx, 28);
        }
        execData.sp = getRegU32(ctx, 29);

        if (execDataAddr != 0u)
        {
            GuestExecData *guestExec = reinterpret_cast<GuestExecData *>(getMemPtr(rdram, execDataAddr));
            if (!guestExec)
            {
                return -1;
            }
            std::memcpy(guestExec, &execData, sizeof(execData));
        }

        static uint32_t successLogs = 0;
        if (successLogs < 16u)
        {
            std::cout << "[SifLoadElfPart] loaded \"" << ps2Path << "\" epc=0x"
                      << std::hex << execData.epc << " gp=0x" << execData.gp << std::dec << std::endl;
            ++successLogs;
        }

        return 0;
    }
}
