#include "Common.h"
#include "System.h"

namespace ps2_syscalls
{
    void GsSetCrt(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int interlaced = getRegU32(ctx, 4); // $a0 - 0=non-interlaced, 1=interlaced
        int videoMode = getRegU32(ctx, 5);  // $a1 - 0=NTSC, 1=PAL, 2=VESA, 3=HiVision
        int frameMode = getRegU32(ctx, 6);  // $a2 - 0=field, 1=frame

        if (runtime)
        {
            auto &gs = runtime->memory().gs();
            const uint64_t smode2 =
                (static_cast<uint64_t>(interlaced) & 0x1ull) |
                ((static_cast<uint64_t>(frameMode) & 0x1ull) << 1);

            gs.smode2 = smode2;

            // Keep CRT1 enabled after the BIOS syscall selects a display mode.
            if ((gs.pmode & 0x3ull) == 0ull)
            {
                gs.pmode |= 0x1ull;
            }
        }

        RUNTIME_LOG("PS2 GsSetCrt: interlaced=" << interlaced
                                                << ", videoMode=" << videoMode
                                                << ", frameMode=" << frameMode << std::endl);

        setReturnS32(ctx, 0);
    }

    void SetGsCrt(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        GsSetCrt(rdram, ctx, runtime);
    }

    void GsGetIMR(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint64_t imr = 0;
        if (runtime)
        {
            imr = runtime->memory().gs().imr;
        }

        RUNTIME_LOG("PS2 GsGetIMR: Returning IMR=0x" << std::hex << imr << std::dec);

        setReturnU64(ctx, imr); // Return in $v0/$v1
    }

    void iGsGetIMR(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        GsGetIMR(rdram, ctx, runtime);
    }

    void GsPutIMR(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint64_t newImr = getRegU32(ctx, 4) | ((uint64_t)getRegU32(ctx, 5) << 32); // $a0 = lower 32 bits, $a1 = upper 32 bits
        uint64_t oldImr = 0;
        if (runtime)
        {
            oldImr = runtime->memory().gs().imr;
            runtime->memory().gs().imr = newImr;
        }
        RUNTIME_LOG("PS2 GsPutIMR: Setting IMR=0x" << std::hex << newImr << std::dec);
        setReturnU64(ctx, oldImr);
    }

    void iGsPutIMR(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        GsPutIMR(rdram, ctx, runtime);
    }

    void GsSetVideoMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int mode = getRegU32(ctx, 4); // $a0 - video mode (various flags)

        RUNTIME_LOG("PS2 GsSetVideoMode: mode=0x" << std::hex << mode << std::dec);

        // Do nothing for now.
    }

    void GetOsdConfigParam(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t paramAddr = getRegU32(ctx, 4); // $a0 - pointer to parameter structure

        if (!getMemPtr(rdram, paramAddr))
        {
            std::cerr << "PS2 GetOsdConfigParam error: Invalid parameter address: 0x"
                      << std::hex << paramAddr << std::dec << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        uint32_t *param = reinterpret_cast<uint32_t *>(getMemPtr(rdram, paramAddr));

        ensureOsdConfigInitialized();
        uint32_t raw;
        {
            std::lock_guard<std::mutex> lock(g_osd_mutex);
            raw = g_osd_config_raw;
        }

        *param = raw;

        setReturnS32(ctx, 0);
    }

    void SetOsdConfigParam(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t paramAddr = getRegU32(ctx, 4); // $a0 - pointer to parameter structure

        if (!getConstMemPtr(rdram, paramAddr))
        {
            std::cerr << "PS2 SetOsdConfigParam error: Invalid parameter address: 0x"
                      << std::hex << paramAddr << std::dec << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        const uint32_t *param = reinterpret_cast<const uint32_t *>(getConstMemPtr(rdram, paramAddr));
        uint32_t raw = param ? *param : 0;
        raw = sanitizeOsdConfigRaw(raw);
        {
            std::lock_guard<std::mutex> lock(g_osd_mutex);
            g_osd_config_raw = raw;
            g_osd_config_initialized = true;
        }

        setReturnS32(ctx, 0);
    }

    void GetRomName(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t bufAddr = getRegU32(ctx, 4); // $a0
        size_t bufSize = getRegU32(ctx, 5);   // $a1
        char *hostBuf = reinterpret_cast<char *>(getMemPtr(rdram, bufAddr));
        const char *romName = "ROMVER 0100";

        if (!hostBuf)
        {
            std::cerr << "GetRomName error: Invalid buffer address" << std::endl;
            setReturnS32(ctx, -1); // Error
            return;
        }
        if (bufSize == 0)
        {
            setReturnS32(ctx, 0);
            return;
        }

        strncpy(hostBuf, romName, bufSize - 1);
        hostBuf[bufSize - 1] = '\0';

        // returns the length of the string (excluding null?) or error
        setReturnS32(ctx, (int32_t)strlen(hostBuf));
    }

    void SifLoadElfPart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t pathAddr = getRegU32(ctx, 4);     // $a0 - path
        const uint32_t secNameAddr = getRegU32(ctx, 5);  // $a1 - section name ("all" typically)
        const uint32_t execDataAddr = getRegU32(ctx, 6); // $a2 - t_ExecData*

        std::string secName = readGuestCStringBounded(rdram, secNameAddr, kLoadfileArgMaxBytes);
        if (secName.empty())
        {
            secName = "all";
        }

        const int32_t ret = runSifLoadElfPart(rdram, ctx, runtime, pathAddr, secName, execDataAddr);
        setReturnS32(ctx, ret);
    }

    void sceSifLoadElf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t pathAddr = getRegU32(ctx, 4);     // $a0 - path
        const uint32_t execDataAddr = getRegU32(ctx, 5); // $a1 - t_ExecData*
        const int32_t ret = runSifLoadElfPart(rdram, ctx, runtime, pathAddr, "all", execDataAddr);
        setReturnS32(ctx, ret);
    }

    void sceSifLoadElfPart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        SifLoadElfPart(rdram, ctx, runtime);
    }

    void sceSifLoadModule(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // Use the same tracker as SifLoadModule so both APIs return the same module IDs.
        SifLoadModule(rdram, ctx, runtime);
    }

    void sceSifLoadModuleBuffer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t bufferAddr = getRegU32(ctx, 4); // $a0
        if (!rdram || bufferAddr == 0u)
        {
            setReturnS32(ctx, -1);
            return;
        }

        // Match buffer-based module loads to stable synthetic tags so module ID lookup remains deterministic.
        const std::string moduleTag = makeSifModuleBufferTag(rdram, bufferAddr);
        const int32_t moduleId = trackSifModuleLoad(moduleTag);
        if (moduleId <= 0)
        {
            setReturnS32(ctx, -1);
            return;
        }

        uint32_t refs = 0;
        {
            std::lock_guard<std::mutex> lock(g_sif_module_mutex);
            auto it = g_sif_modules_by_id.find(moduleId);
            if (it != g_sif_modules_by_id.end())
            {
                refs = it->second.refCount;
            }
        }
        logSifModuleAction("load-buffer", moduleId, moduleTag, refs);
        setReturnS32(ctx, moduleId);
    }

    void TODO(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t encodedSyscallId)
    {
        // a bit more detail mayber reomve old logic, lets get it more raw
        std::cerr << "[Syscall TODO]"
                  << " encoded=0x" << std::hex << encodedSyscallId
                  << " v1=0x" << getRegU32(ctx, 3)
                  << " v0=0x" << getRegU32(ctx, 2)
                  << " a0=0x" << getRegU32(ctx, 4)
                  << " a1=0x" << getRegU32(ctx, 5)
                  << " a2=0x" << getRegU32(ctx, 6)
                  << " a3=0x" << getRegU32(ctx, 7)
                  << " pc=0x" << ctx->pc
                  << std::dec << std::endl;

        const uint32_t v0 = getRegU32(ctx, 2);
        const uint32_t v1 = getRegU32(ctx, 3);
        const uint32_t caller_ra = getRegU32(ctx, 31);
        uint32_t syscallId = encodedSyscallId;
        if (syscallId == 0u)
        {
            syscallId = (v0 != 0u) ? v0 : v1;
        }

        std::cerr << "Warning: Unimplemented PS2 syscall called. PC=0x" << std::hex << ctx->pc
                  << ", RA=0x" << caller_ra
                  << ", Encoded=0x" << encodedSyscallId
                  << ", v0=0x" << v0
                  << ", v1=0x" << v1
                  << ", Chosen=0x" << syscallId
                  << std::dec << std::endl;

        std::cerr << "  Args: $a0=0x" << std::hex << getRegU32(ctx, 4)
                  << ", $a1=0x" << getRegU32(ctx, 5)
                  << ", $a2=0x" << getRegU32(ctx, 6)
                  << ", $a3=0x" << getRegU32(ctx, 7) << std::dec << std::endl;

        // Common syscalls:
        // 0x04: Exit
        // 0x06: LoadExecPS2
        // 0x07: ExecPS2
        if (syscallId == 0x04u)
        {
            std::cerr << "  -> Syscall is Exit(), calling ExitThread stub." << std::endl;
            ExitThread(rdram, ctx, runtime);
            return;
        }

        static std::mutex s_unknownMutex;
        static std::unordered_map<uint32_t, uint64_t> s_unknownCounts;
        {
            std::lock_guard<std::mutex> lock(s_unknownMutex);
            const uint64_t count = ++s_unknownCounts[syscallId];
            if (count == 1 || (count % 5000u) == 0u)
            {
                std::cerr << "  -> Unknown syscallId=0x" << std::hex << syscallId
                          << " hits=" << std::dec << count << std::endl;
            }
        }

        // Bootstrap default: avoid hard-failing loops that probe syscall availability.
        setReturnS32(ctx, 0);
    }

    static uint32_t computeBuiltinFindAddressResult(uint8_t *rdram,
                                                    uint32_t originalStart,
                                                    uint32_t originalEnd,
                                                    uint32_t target);

    bool dispatchSyscallOverride(uint32_t syscallNumber, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t handler = 0u;
        {
            std::lock_guard<std::mutex> lock(g_syscall_override_mutex);
            auto it = g_syscall_overrides.find(syscallNumber);
            if (it == g_syscall_overrides.end())
            {
                return false;
            }
            handler = it->second;
        }

        if (!runtime || !ctx || handler == 0u)
        {
            return false;
        }

        const uint32_t overrideA0 = getRegU32(ctx, 4);
        const uint32_t overrideA1 = getRegU32(ctx, 5);
        const uint32_t overrideA2 = getRegU32(ctx, 6);
        const uint32_t overrideA3 = getRegU32(ctx, 7);
        const uint32_t overridePc = ctx->pc;
        const uint32_t overrideRa = getRegU32(ctx, 31);

        thread_local std::vector<uint32_t> s_activeSyscallOverrides;
        if (std::find(s_activeSyscallOverrides.begin(), s_activeSyscallOverrides.end(), syscallNumber) != s_activeSyscallOverrides.end())
        {
            static std::atomic<uint32_t> s_reentrantLogs{0u};
            constexpr uint32_t kMaxReentrantLogs = 32u;
            const uint32_t logIndex = s_reentrantLogs.fetch_add(1u, std::memory_order_relaxed);
            if (logIndex < kMaxReentrantLogs)
            {
                std::cerr << "[SyscallOverride:reentrant]"
                          << " syscall=0x" << std::hex << syscallNumber
                          << " handler=0x" << handler
                          << " pc=0x" << ctx->pc
                          << " ra=0x" << getRegU32(ctx, 31)
                          << std::dec << std::endl;
            }
            return false;
        }

        s_activeSyscallOverrides.push_back(syscallNumber);
        struct ScopedActiveOverride
        {
            std::vector<uint32_t> &active;
            ~ScopedActiveOverride()
            {
                if (!active.empty())
                {
                    active.pop_back();
                }
            }
        } scopedActiveOverride{s_activeSyscallOverrides};

        uint32_t retV0 = 0u;
        const bool invoked = rpcInvokeFunction(rdram,
                                               ctx,
                                               runtime,
                                               handler,
                                               getRegU32(ctx, 4),
                                               getRegU32(ctx, 5),
                                               getRegU32(ctx, 6),
                                               getRegU32(ctx, 7),
                                               &retV0);

        if (syscallNumber == 0x83u)
        {
            const uint32_t builtinRet = computeBuiltinFindAddressResult(rdram, overrideA0, overrideA1, overrideA2);
            const bool mismatch = (retV0 != builtinRet);

            static std::atomic<uint32_t> s_findAddressOverrideLogs{0u};
            static std::atomic<uint32_t> s_findAddressOverrideMismatchLogs{0u};
            constexpr uint32_t kMaxFindAddressOverrideLogs = 64u;
            constexpr uint32_t kMaxFindAddressOverrideMismatchLogs = 128u;

            const uint32_t logIndex = s_findAddressOverrideLogs.fetch_add(1u, std::memory_order_relaxed);
            const uint32_t mismatchIndex = mismatch
                                               ? s_findAddressOverrideMismatchLogs.fetch_add(1u, std::memory_order_relaxed)
                                               : 0u;
            if (logIndex < kMaxFindAddressOverrideLogs ||
                (mismatch && mismatchIndex < kMaxFindAddressOverrideMismatchLogs))
            {
                const uint32_t guestMinus20c = (retV0 != 0u) ? (retV0 - 0x20Cu) : 0u;
                const uint32_t guestMinus168 = (retV0 != 0u) ? (retV0 - 0x168u) : 0u;
                const uint32_t builtinMinus20c = (builtinRet != 0u) ? (builtinRet - 0x20Cu) : 0u;
                const uint32_t builtinMinus168 = (builtinRet != 0u) ? (builtinRet - 0x168u) : 0u;

                std::cerr << "[Syscall83:override]"
                          << " handler=0x" << std::hex << handler
                          << " invoked=" << (invoked ? "true" : "false")
                          << " pc=0x" << overridePc
                          << " ra=0x" << overrideRa
                          << " a0=0x" << overrideA0
                          << " a1=0x" << overrideA1
                          << " a2=0x" << overrideA2
                          << " a3=0x" << overrideA3
                          << " guestRet=0x" << retV0
                          << " builtinRet=0x" << builtinRet
                          << " guest-20c=0x" << guestMinus20c
                          << " builtin-20c=0x" << builtinMinus20c
                          << " guest-168=0x" << guestMinus168
                          << " builtin-168=0x" << builtinMinus168
                          << " match=" << (mismatch ? "false" : "true")
                          << std::dec << std::endl;
            }
        }

        if (!invoked)
        {
            static std::atomic<uint32_t> s_fallbackLogs{0u};
            constexpr uint32_t kMaxFallbackLogs = 64u;
            const uint32_t logIndex = s_fallbackLogs.fetch_add(1u, std::memory_order_relaxed);
            if (logIndex < kMaxFallbackLogs)
            {
                std::cerr << "[SyscallOverride:fallback]"
                          << " syscall=0x" << std::hex << syscallNumber
                          << " handler=0x" << handler
                          << " pc=0x" << ctx->pc
                          << " ra=0x" << getRegU32(ctx, 31)
                          << std::dec << std::endl;
            }
            return false;
        }

        setReturnU32(ctx, retV0);
        return true;
    }

    static bool tryResolveGuestSyscallMirrorAddr(uint32_t syscallIndex, uint32_t &guestAddr)
    {
        const int64_t offsetBytes =
            static_cast<int64_t>(static_cast<int32_t>(syscallIndex)) * static_cast<int64_t>(sizeof(uint32_t));
        const int64_t guestAddr64 = static_cast<int64_t>(kGuestSyscallTablePhysBase) + offsetBytes;
        if (guestAddr64 < 0 || (guestAddr64 + static_cast<int64_t>(sizeof(uint32_t))) > static_cast<int64_t>(kGuestSyscallMirrorLimit))
        {
            return false;
        }

        guestAddr = static_cast<uint32_t>(guestAddr64);
        return true;
    }

    static void writeGuestKernelWord(uint8_t *rdram, uint32_t guestAddr, uint32_t value)
    {
        if (!rdram)
        {
            return;
        }

        if (uint8_t *ptr = getMemPtr(rdram, guestAddr))
        {
            std::memcpy(ptr, &value, sizeof(value));
        }
    }

    static void seedGuestSyscallTableProbeLocked(uint8_t *rdram)
    {
        writeGuestKernelWord(rdram, kGuestSyscallTableProbeBase + 0u, kGuestSyscallTableGuestBase >> 16);
        writeGuestKernelWord(rdram, kGuestSyscallTableProbeBase + 8u, kGuestSyscallTableGuestBase & 0xFFFFu);
        g_syscall_mirror_addrs.insert(kGuestSyscallTableProbeBase + 0u);
        g_syscall_mirror_addrs.insert(kGuestSyscallTableProbeBase + 8u);
    }

    static void mirrorGuestSyscallEntryLocked(uint8_t *rdram, uint32_t syscallIndex, uint32_t handler)
    {
        uint32_t guestAddr = 0u;
        if (!tryResolveGuestSyscallMirrorAddr(syscallIndex, guestAddr))
        {
            return;
        }

        writeGuestKernelWord(rdram, guestAddr, handler);
        if (handler == 0u)
        {
            g_syscall_mirror_addrs.erase(guestAddr);
            return;
        }

        g_syscall_mirror_addrs.insert(guestAddr);
    }

    void initializeGuestKernelState(uint8_t *rdram)
    {
        if (!rdram)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(g_syscall_override_mutex);
        for (uint32_t guestAddr : g_syscall_mirror_addrs)
        {
            writeGuestKernelWord(rdram, guestAddr, 0u);
        }
        g_syscall_mirror_addrs.clear();

        seedGuestSyscallTableProbeLocked(rdram);

        for (const auto &entry : g_syscall_overrides)
        {
            mirrorGuestSyscallEntryLocked(rdram, entry.first, entry.second);
        }
    }

    void SetSyscall(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;
        const uint32_t syscallIndex = getRegU32(ctx, 4);
        const uint32_t handler = getRegU32(ctx, 5);

        {
            std::lock_guard<std::mutex> lock(g_syscall_override_mutex);
            if (handler == 0u)
            {
                g_syscall_overrides.erase(syscallIndex);
            }
            else
            {
                g_syscall_overrides[syscallIndex] = handler;
            }

            mirrorGuestSyscallEntryLocked(rdram, syscallIndex, handler);
        }

        setReturnS32(ctx, 0);
    }

    // 0x3C SetupThread
    // args: $a0 = gp, $a1 = stack, $a2 = stack_size, $a3 = args, $t0 = root_func
    void SetupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t gp = getRegU32(ctx, 4);
        const uint32_t stack = getRegU32(ctx, 5);
        const int32_t stackSizeSigned = static_cast<int32_t>(getRegU32(ctx, 6));
        const uint32_t currentSp = getRegU32(ctx, 29);

        if (gp != 0u)
        {
            setRegU32(ctx, 28, gp);
        }

        uint32_t sp = currentSp;
        if (stack == 0xFFFFFFFFu)
        {
            if (stackSizeSigned > 0)
            {
                const uint32_t requestedSize = static_cast<uint32_t>(stackSizeSigned);
                if (requestedSize < PS2_RAM_SIZE)
                {
                    sp = PS2_RAM_SIZE - requestedSize;
                }
                else
                {
                    sp = PS2_RAM_SIZE;
                }
            }
            else
            {
                sp = PS2_RAM_SIZE;
            }
        }
        else if (stack != 0u)
        {
            if (stackSizeSigned > 0)
            {
                sp = stack + static_cast<uint32_t>(stackSizeSigned);
            }
            else
            {
                sp = stack;
            }
        }

        sp &= ~0xFu;
        setReturnU32(ctx, sp);
    }

    // 0x3D SetupHeap: returns heap base/start pointer
    void SetupHeap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t heapBase = getRegU32(ctx, 4); // $a0
        const uint32_t heapSize = getRegU32(ctx, 5); // $a1 (optional size)

        if (runtime)
        {
            uint32_t heapLimit = PS2_RAM_SIZE;
            if (heapSize != 0u && heapBase < PS2_RAM_SIZE)
            {
                const uint64_t candidateLimit = static_cast<uint64_t>(heapBase) + static_cast<uint64_t>(heapSize);
                heapLimit = static_cast<uint32_t>(std::min<uint64_t>(candidateLimit, PS2_RAM_SIZE));
            }
            runtime->configureGuestHeap(heapBase, heapLimit);
            setReturnU32(ctx, runtime->guestHeapBase());
            return;
        }

        setReturnU32(ctx, heapBase);
    }

    // 0x3E EndOfHeap: commonly returns current heap end; keep it stable for now.
    void EndOfHeap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (runtime)
        {
            setReturnU32(ctx, runtime->guestHeapEnd());
            return;
        }

        setReturnU32(ctx, getRegU32(ctx, 4));
    }

    void GetMemorySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        setReturnU32(ctx, PS2_RAM_SIZE);
    }

    static inline uint32_t normalizeKernelAlias(uint32_t addr)
    {
        if (addr >= 0x80000000u && addr < 0xC0000000u)
        {
            return addr & 0x1FFFFFFFu;
        }
        return addr;
    }

    static uint32_t computeBuiltinFindAddressResult(uint8_t *rdram,
                                                    uint32_t originalStart,
                                                    uint32_t originalEnd,
                                                    uint32_t target)
    {
        uint32_t start = (originalStart + 3u) & ~0x3u;
        uint32_t end = originalEnd & ~0x3u;
        if (start >= end)
        {
            return 0u;
        }

        const uint32_t targetNorm = normalizeKernelAlias(target);
        for (uint32_t addr = start; addr < end; addr += sizeof(uint32_t))
        {
            const uint8_t *entryPtr = getConstMemPtr(rdram, addr);
            if (!entryPtr)
            {
                break;
            }

            uint32_t entry = 0u;
            std::memcpy(&entry, entryPtr, sizeof(entry));
            if (entry == target || normalizeKernelAlias(entry) == targetNorm)
            {
                return addr;
            }
        }

        return 0u;
    }

    struct FindAddressWordSample
    {
        uint32_t addr = 0u;
        uint32_t value = 0u;
    };

    struct FindAddressMatchSample
    {
        uint32_t addr = 0u;
        uint32_t value = 0u;
        bool aliasOnly = false;
    };

    static void logFindAddressDiagnostics(uint32_t callerPc,
                                          uint32_t originalStart,
                                          uint32_t originalEnd,
                                          uint32_t alignedStart,
                                          uint32_t alignedEnd,
                                          uint32_t target,
                                          uint32_t targetNorm,
                                          bool found,
                                          uint32_t resultAddr,
                                          uint32_t scannedWords,
                                          bool allZero,
                                          bool aborted,
                                          uint32_t abortedAddr,
                                          const FindAddressWordSample *firstWords,
                                          uint32_t firstWordCount,
                                          const FindAddressWordSample *nonZeroWords,
                                          uint32_t nonZeroWordCount,
                                          const FindAddressMatchSample *matches,
                                          uint32_t matchCount)
    {
        static std::atomic<uint32_t> s_findAddressHitLogs{0u};
        static std::atomic<uint32_t> s_findAddressMissLogs{0u};
        constexpr uint32_t kMaxFindAddressHitLogs = 16u;
        constexpr uint32_t kMaxFindAddressMissLogs = 128u;

        std::atomic<uint32_t> &counter = found ? s_findAddressHitLogs : s_findAddressMissLogs;
        const uint32_t logIndex = counter.fetch_add(1u, std::memory_order_relaxed);
        const uint32_t logLimit = found ? kMaxFindAddressHitLogs : kMaxFindAddressMissLogs;
        if (logIndex >= logLimit)
        {
            return;
        }

        std::cerr << "[FindAddress:" << (found ? "hit" : "miss") << "]"
                  << " pc=0x" << std::hex << callerPc
                  << " start=0x" << originalStart
                  << " end=0x" << originalEnd
                  << " alignedStart=0x" << alignedStart
                  << " alignedEnd=0x" << alignedEnd
                  << " target=0x" << target
                  << " targetNorm=0x" << targetNorm
                  << " result=0x" << resultAddr
                  << std::dec
                  << " scannedWords=" << scannedWords
                  << " allZero=" << (allZero ? "true" : "false")
                  << " aborted=" << (aborted ? "true" : "false");
        if (aborted)
        {
            std::cerr << " abortedAddr=0x" << std::hex << abortedAddr << std::dec;
        }
        std::cerr << std::endl;

        std::cerr << "  firstWords:";
        if (firstWordCount == 0u)
        {
            std::cerr << " none";
        }
        else
        {
            for (uint32_t i = 0; i < firstWordCount; ++i)
            {
                std::cerr << " [0x" << std::hex << firstWords[i].addr
                          << "]=0x" << firstWords[i].value;
            }
            std::cerr << std::dec;
        }
        std::cerr << std::endl;

        std::cerr << "  nonZeroSample:";
        if (nonZeroWordCount == 0u)
        {
            std::cerr << " none";
        }
        else
        {
            for (uint32_t i = 0; i < nonZeroWordCount; ++i)
            {
                std::cerr << " [0x" << std::hex << nonZeroWords[i].addr
                          << "]=0x" << nonZeroWords[i].value;
            }
            std::cerr << std::dec;
        }
        std::cerr << std::endl;

        std::cerr << "  matches:";
        if (matchCount == 0u)
        {
            std::cerr << " none";
        }
        else
        {
            for (uint32_t i = 0; i < matchCount; ++i)
            {
                std::cerr << " [0x" << std::hex << matches[i].addr
                          << "]=0x" << matches[i].value
                          << (matches[i].aliasOnly ? "(alias)" : "(exact)");
            }
            std::cerr << std::dec;
        }
        std::cerr << std::endl;
    }

    // 0x83 FindAddress:
    // - a0: table start (inclusive)
    // - a1: table end (exclusive)
    // - a2: target address to locate inside the table (word entries)
    // Returns the guest address of the matching word entry, or 0 if not found.
    void FindAddress(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;

        constexpr uint32_t kFindAddressWordSamples = 8u;
        constexpr uint32_t kFindAddressMatchSamples = 4u;

        const uint32_t originalStart = getRegU32(ctx, 4);
        const uint32_t originalEnd = getRegU32(ctx, 5);
        const uint32_t target = getRegU32(ctx, 6);
        const uint32_t targetNorm = normalizeKernelAlias(target);
        const uint32_t callerPc = ctx->pc;

        uint32_t start = originalStart;
        uint32_t end = originalEnd;

        // Word-scan semantics: align the search window to uint32 boundaries.
        start = (start + 3u) & ~0x3u;
        end &= ~0x3u;

        if (start >= end)
        {
            logFindAddressDiagnostics(callerPc,
                                      originalStart,
                                      originalEnd,
                                      start,
                                      end,
                                      target,
                                      targetNorm,
                                      false,
                                      0u,
                                      0u,
                                      true,
                                      false,
                                      0u,
                                      nullptr,
                                      0u,
                                      nullptr,
                                      0u,
                                      nullptr,
                                      0u);
            setReturnU32(ctx, 0u);
            return;
        }

        FindAddressWordSample firstWords[kFindAddressWordSamples]{};
        FindAddressWordSample nonZeroWords[kFindAddressWordSamples]{};
        FindAddressMatchSample matches[kFindAddressMatchSamples]{};
        uint32_t firstWordCount = 0u;
        uint32_t nonZeroWordCount = 0u;
        uint32_t matchCount = 0u;
        uint32_t scannedWords = 0u;
        uint32_t resultAddr = 0u;
        uint32_t abortedAddr = 0u;
        bool aborted = false;
        bool allZero = true;
        bool foundMatch = false;

        for (uint32_t addr = start; addr < end; addr += sizeof(uint32_t))
        {
            const uint8_t *entryPtr = getConstMemPtr(rdram, addr);
            if (!entryPtr)
            {
                aborted = true;
                abortedAddr = addr;
                break;
            }

            uint32_t entry = 0;
            std::memcpy(&entry, entryPtr, sizeof(entry));
            ++scannedWords;

            if (firstWordCount < kFindAddressWordSamples)
            {
                firstWords[firstWordCount++] = {addr, entry};
            }

            if (entry != 0u)
            {
                allZero = false;
                if (nonZeroWordCount < kFindAddressWordSamples)
                {
                    nonZeroWords[nonZeroWordCount++] = {addr, entry};
                }
            }

            const bool exactMatch = (entry == target);
            const bool aliasMatch = !exactMatch && (normalizeKernelAlias(entry) == targetNorm);
            if (exactMatch || aliasMatch)
            {
                if (!foundMatch)
                {
                    resultAddr = addr;
                    foundMatch = true;
                }
                if (matchCount < kFindAddressMatchSamples)
                {
                    matches[matchCount++] = {addr, entry, aliasMatch};
                }
            }
        }

        logFindAddressDiagnostics(callerPc,
                                  originalStart,
                                  originalEnd,
                                  start,
                                  end,
                                  target,
                                  targetNorm,
                                  foundMatch,
                                  resultAddr,
                                  scannedWords,
                                  allZero,
                                  aborted,
                                  abortedAddr,
                                  firstWords,
                                  firstWordCount,
                                  nonZeroWords,
                                  nonZeroWordCount,
                                  matches,
                                  matchCount);

        setReturnU32(ctx, resultAddr);
    }

    void Deci2Call(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        setReturnS32(ctx, KE_OK);
    }

    // 0x5A QueryBootMode (stub): return 0 for now
    void QueryBootMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t mode = getRegU32(ctx, 4);
        ensureBootModeTable(rdram);
        uint32_t addr = 0;
        {
            std::lock_guard<std::mutex> lock(g_bootmode_mutex);
            auto it = g_bootmode_addresses.find(static_cast<uint8_t>(mode));
            if (it != g_bootmode_addresses.end())
                addr = it->second;
        }
        setReturnU32(ctx, addr);
    }

    // 0x5B GetThreadTLS (stub): return 0
    void GetThreadTLS(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        auto info = ensureCurrentThreadInfo(ctx);
        if (!info)
        {
            setReturnU32(ctx, 0);
            return;
        }

        if (info->tlsBase == 0)
        {
            info->tlsBase = allocTlsAddr(rdram);
        }

        setReturnU32(ctx, info->tlsBase);
    }

    // 0x74 RegisterExitHandler (stub): return 0
    void RegisterExitHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t func = getRegU32(ctx, 4);
        uint32_t arg = getRegU32(ctx, 5);
        if (func == 0)
        {
            setReturnS32(ctx, -1);
            return;
        }

        int tid = g_currentThreadId;
        {
            std::lock_guard<std::mutex> lock(g_exit_handler_mutex);
            g_exit_handlers[tid].push_back({func, arg});
        }

        setReturnS32(ctx, 0);
    }
}
