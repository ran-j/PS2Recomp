#include "Common.h"
#include "SIF.h"
#include "../Syscalls/RPC.h"

#include <map>

namespace ps2_stubs
{
    void sceSifCmdIntrHdlr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifCmdIntrHdlr", rdram, ctx, runtime);
    }

    void sceSifLoadModule(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::SifLoadModule(rdram, ctx, runtime);
    }

    void sceSifSendCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t srcAddr = getRegU32(ctx, 7); // $a3
        const uint32_t dstAddr = readStackU32(rdram, ctx, 16);
        const uint32_t size = readStackU32(rdram, ctx, 20);
        if (size != 0u && srcAddr != 0u && dstAddr != 0u)
        {
            for (uint32_t i = 0; i < size; ++i)
            {
                const uint8_t *src = getConstMemPtr(rdram, srcAddr + i);
                uint8_t *dst = getMemPtr(rdram, dstAddr + i);
                if (!src || !dst)
                {
                    break;
                }
                *dst = *src;
            }
        }

        setReturnS32(ctx, 1);
    }

    namespace
    {
        struct Ps2SifDmaTransfer
        {
            uint32_t src = 0;
            uint32_t dest = 0;
            int32_t size = 0;
            int32_t attr = 0;
        };
        static_assert(sizeof(Ps2SifDmaTransfer) == 16u, "Unexpected SIF DMA descriptor size");

        std::mutex g_sifDmaTransferMutex;
        uint32_t g_nextSifDmaTransferId = 1u;
        std::mutex g_sifCmdStateMutex;
        std::mutex g_sifHeapMutex;
        std::unordered_map<uint32_t, uint32_t> g_sifRegs;
        std::unordered_map<uint32_t, uint32_t> g_sifSregs;
        std::unordered_map<uint32_t, uint32_t> g_sifCmdHandlers;
        std::map<uint32_t, uint32_t> g_sifHeapAllocations;
        uint32_t g_sifCmdBuffer = 0u;
        uint32_t g_sifSysCmdBuffer = 0u;
        bool g_sifCmdInitialized = false;
        uint32_t g_sifGetRegLogCount = 0u;
        uint32_t g_sifSetRegLogCount = 0u;

        constexpr uint32_t kSifRegBootStatus = 0x4u;
        constexpr uint32_t kSifRegMainAddr = 0x80000000u;
        constexpr uint32_t kSifRegSubAddr = 0x80000001u;
        constexpr uint32_t kSifRegMsCom = 0x80000002u;
        constexpr uint32_t kSifBootReadyMask = 0x00020000u;

        void seedDefaultSifRegsLocked()
        {
            g_sifRegs.clear();
            g_sifSregs.clear();
            g_sifCmdHandlers.clear();
            g_sifCmdBuffer = 0u;
            g_sifSysCmdBuffer = 0u;
            g_sifCmdInitialized = false;
            g_sifGetRegLogCount = 0u;
            g_sifSetRegLogCount = 0u;

            g_sifRegs[kSifRegBootStatus] = kSifBootReadyMask;
            g_sifRegs[kSifRegMainAddr] = 0u;
            g_sifRegs[kSifRegSubAddr] = 0u;
            g_sifRegs[kSifRegMsCom] = 0u;
        }

        bool shouldTraceSifReg(uint32_t reg)
        {
            switch (reg)
            {
            case 0x2u:
            case 0x4u:
            case 0x80000000u:
            case 0x80000001u:
            case 0x80000002u:
                return true;
            default:
                return false;
            }
        }

        struct SifStateInitializer
        {
            SifStateInitializer()
            {
                std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
                seedDefaultSifRegsLocked();
            }
        } g_sifStateInitializer;

        uint32_t allocateSifDmaTransferId()
        {
            std::lock_guard<std::mutex> lock(g_sifDmaTransferMutex);
            uint32_t id = g_nextSifDmaTransferId++;
            if (id == 0u)
            {
                id = g_nextSifDmaTransferId++;
            }
            return id;
        }

        uint32_t alignIopHeapSize(uint32_t size)
        {
            return (size + (kIopHeapAlign - 1u)) & ~(kIopHeapAlign - 1u);
        }

        uint32_t allocateSifHeapBlock(uint32_t requestSize)
        {
            const uint32_t alignedSize = alignIopHeapSize(requestSize);
            if (alignedSize == 0u)
            {
                return 0u;
            }

            std::lock_guard<std::mutex> lock(g_sifHeapMutex);
            uint32_t candidate = kIopHeapBase;
            for (const auto &[addr, size] : g_sifHeapAllocations)
            {
                if (candidate + alignedSize <= addr)
                {
                    break;
                }

                const uint32_t blockEnd = alignIopHeapSize(addr + size);
                if (blockEnd > candidate)
                {
                    candidate = blockEnd;
                }
            }

            if (candidate < kIopHeapBase || candidate + alignedSize > kIopHeapLimit)
            {
                return 0u;
            }

            g_sifHeapAllocations[candidate] = alignedSize;
            g_iopHeapNext = candidate + alignedSize;
            return candidate;
        }

        bool freeSifHeapBlock(uint32_t addr)
        {
            std::lock_guard<std::mutex> lock(g_sifHeapMutex);
            const auto it = g_sifHeapAllocations.find(addr);
            if (it == g_sifHeapAllocations.end())
            {
                return false;
            }

            g_sifHeapAllocations.erase(it);
            if (g_sifHeapAllocations.empty())
            {
                g_iopHeapNext = kIopHeapBase;
            }
            return true;
        }

        void resetSifHeapState()
        {
            std::lock_guard<std::mutex> lock(g_sifHeapMutex);
            g_sifHeapAllocations.clear();
            g_iopHeapNext = kIopHeapBase;
        }

        bool isCopyableGuestAddress(uint32_t addr)
        {
            if (addr >= PS2_SCRATCHPAD_BASE && addr < (PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE))
            {
                return true;
            }

            if (addr < 0x20000000u)
            {
                return true;
            }

            if (addr >= 0x20000000u && addr < 0x40000000u)
            {
                return true;
            }

            if (addr >= 0x80000000u && addr < 0xC0000000u)
            {
                return true;
            }

            return false;
        }

        bool canCopyGuestByteRange(const uint8_t *rdram, uint32_t dstAddr, uint32_t srcAddr, uint32_t sizeBytes)
        {
            if (!rdram)
            {
                return false;
            }

            if (sizeBytes == 0u)
            {
                return true;
            }

            for (uint32_t i = 0u; i < sizeBytes; ++i)
            {
                const uint32_t srcByteAddr = srcAddr + i;
                const uint32_t dstByteAddr = dstAddr + i;

                if (!isCopyableGuestAddress(srcByteAddr) || !isCopyableGuestAddress(dstByteAddr))
                {
                    return false;
                }

                const uint8_t *src = getConstMemPtr(rdram, srcByteAddr);
                const uint8_t *dst = getConstMemPtr(rdram, dstByteAddr);
                if (!src || !dst)
                {
                    return false;
                }
            }

            return true;
        }

        bool copyGuestByteRange(uint8_t *rdram, uint32_t dstAddr, uint32_t srcAddr, uint32_t sizeBytes)
        {
            if (!canCopyGuestByteRange(rdram, dstAddr, srcAddr, sizeBytes))
            {
                return false;
            }

            if (sizeBytes == 0u)
            {
                return true;
            }

            const uint64_t srcBegin = srcAddr;
            const uint64_t srcEnd = srcBegin + static_cast<uint64_t>(sizeBytes);
            const uint64_t dstBegin = dstAddr;
            const bool copyBackward = (dstBegin > srcBegin) && (dstBegin < srcEnd);

            if (copyBackward)
            {
                for (uint32_t i = sizeBytes; i > 0u; --i)
                {
                    const uint32_t index = i - 1u;
                    const uint8_t *src = getConstMemPtr(rdram, srcAddr + index);
                    uint8_t *dst = getMemPtr(rdram, dstAddr + index);
                    if (!src || !dst)
                    {
                        return false;
                    }
                    *dst = *src;
                }
                return true;
            }

            for (uint32_t i = 0; i < sizeBytes; ++i)
            {
                const uint8_t *src = getConstMemPtr(rdram, srcAddr + i);
                uint8_t *dst = getMemPtr(rdram, dstAddr + i);
                if (!src || !dst)
                {
                    return false;
                }
                *dst = *src;
            }
            return true;
        }
    }

    void resetSifState()
    {
        std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
        seedDefaultSifRegsLocked();
        resetSifHeapState();
    }

    void sceSifAddCmdHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t cid = getRegU32(ctx, 4);
        const uint32_t handler = getRegU32(ctx, 5);
        std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
        g_sifCmdHandlers[cid] = handler;
        setReturnS32(ctx, 0);
    }

    void sceSifAllocIopHeap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;

        const uint32_t reqSize = getRegU32(ctx, 4);
        setReturnU32(ctx, allocateSifHeapBlock(reqSize));
    }

    void sceSifAllocSysMemory(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;

        const uint32_t size = getRegU32(ctx, 5);
        setReturnU32(ctx, allocateSifHeapBlock(size));
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
        (void)rdram;
        (void)runtime;
        (void)getRegU32(ctx, 4); // trid

        // Transfers are applied immediately by sceSifSetDma in this runtime.
        setReturnS32(ctx, -1);
    }

    void sceSifExecRequest(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceSifExitCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
        seedDefaultSifRegsLocked();
        setReturnS32(ctx, 0);
    }

    void sceSifExitRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceSifFreeIopHeap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;

        const uint32_t addr = getRegU32(ctx, 4);
        setReturnS32(ctx, freeSifHeapBlock(addr) ? 0 : -1);
    }

    void sceSifFreeSysMemory(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;

        const uint32_t addr = getRegU32(ctx, 4);
        setReturnS32(ctx, freeSifHeapBlock(addr) ? 0 : -1);
    }

    void sceSifGetDataTable(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
        setReturnU32(ctx, g_sifCmdBuffer);
    }

    void sceSifGetIopAddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnU32(ctx, getRegU32(ctx, 4));
    }

    void sceSifGetNextRequest(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceSifGetOtherData(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;

        const uint32_t rdAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        const uint32_t dstAddr = getRegU32(ctx, 6);
        const int32_t sizeSigned = static_cast<int32_t>(getRegU32(ctx, 7));

        if (sizeSigned <= 0)
        {
            setReturnS32(ctx, 0);
            return;
        }

        const uint32_t size = static_cast<uint32_t>(sizeSigned);
        if (size > PS2_RAM_SIZE)
        {
            static uint32_t warnCount = 0;
            if (warnCount < 32u)
            {
                std::cerr << "sceSifGetOtherData rejected oversized transfer size=0x"
                          << std::hex << size << std::dec << std::endl;
                ++warnCount;
            }
            setReturnS32(ctx, -1);
            return;
        }

        ps2_syscalls::prepareSoundDriverStatusTransfer(rdram, srcAddr, size);

        if (!copyGuestByteRange(rdram, dstAddr, srcAddr, size))
        {
            static uint32_t warnCount = 0;
            if (warnCount < 32u)
            {
                std::cerr << "sceSifGetOtherData copy failed src=0x" << std::hex << srcAddr
                          << " dst=0x" << dstAddr
                          << " size=0x" << size
                          << std::dec << std::endl;
                ++warnCount;
            }
            setReturnS32(ctx, -1);
            return;
        }

        // SifRpcReceiveData_t keeps src/dest/size at offsets 0x10/0x14/0x18.
        if (uint8_t *rd = getMemPtr(rdram, rdAddr))
        {
            std::memcpy(rd + 0x10u, &srcAddr, sizeof(srcAddr));
            std::memcpy(rd + 0x14u, &dstAddr, sizeof(dstAddr));
            std::memcpy(rd + 0x18u, &size, sizeof(size));
        }

        ps2_syscalls::finalizeSoundDriverStatusTransfer(rdram, srcAddr, dstAddr, size);

        setReturnS32(ctx, 0);
    }

    void sceSifGetReg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t reg = getRegU32(ctx, 4);
        uint32_t value = 0u;
        bool shouldLog = false;
        {
            std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
            auto it = g_sifRegs.find(reg);
            if (it != g_sifRegs.end())
            {
                value = it->second;
            }
            shouldLog = shouldTraceSifReg(reg) && g_sifGetRegLogCount < 128u;
            if (shouldLog)
            {
                ++g_sifGetRegLogCount;
            }
        }
        if (shouldLog)
        {
            auto flags = std::cerr.flags();
            std::cerr << "[sceSifGetReg] reg=0x" << std::hex << reg
                      << " value=0x" << value
                      << " pc=0x" << (ctx ? ctx->pc : 0u)
                      << " ra=0x" << (ctx ? getRegU32(ctx, 31) : 0u)
                      << std::dec << std::endl;
            std::cerr.flags(flags);
        }
        setReturnU32(ctx, value);
    }

    void sceSifGetSreg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t reg = getRegU32(ctx, 4);
        uint32_t value = 0u;
        {
            std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
            auto it = g_sifSregs.find(reg);
            if (it != g_sifSregs.end())
            {
                value = it->second;
            }
        }
        setReturnU32(ctx, value);
    }

    void sceSifInitCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
        g_sifCmdInitialized = true;
        setReturnS32(ctx, 0);
    }

    void sceSifInitIopHeap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        resetSifHeapState();
        setReturnS32(ctx, 0);
    }

    void sceSifInitRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::SifInitRpc(rdram, ctx, runtime);
    }

    void sceSifIsAliveIop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
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
        setReturnS32(ctx, 0);
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
        const uint32_t cid = getRegU32(ctx, 4);
        std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
        g_sifCmdHandlers.erase(cid);
        setReturnS32(ctx, 0);
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
        setReturnS32(ctx, 1);
    }

    void sceSifRpcLoop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceSifSetCmdBuffer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t newBuffer = getRegU32(ctx, 4);
        uint32_t prev = 0u;
        {
            std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
            prev = g_sifCmdBuffer;
            g_sifCmdBuffer = newBuffer;
        }
        setReturnU32(ctx, prev);
    }

    void isceSifSetDChain(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        sceSifSetDChain(rdram, ctx, runtime);
    }

    void isceSifSetDma(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        sceSifSetDma(rdram, ctx, runtime);
    }

    void sceSifSetDChain(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        setReturnS32(ctx, 0);
    }

    void sceSifSetDma(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;

        const uint32_t dmatAddr = getRegU32(ctx, 4);
        const uint32_t count = getRegU32(ctx, 5);
        if (!dmatAddr || count == 0u || count > 32u)
        {
            setReturnS32(ctx, 0);
            return;
        }

        std::array<Ps2SifDmaTransfer, 32u> pending{};
        uint32_t pendingCount = 0u;
        bool ok = true;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t entryAddr = dmatAddr + (i * static_cast<uint32_t>(sizeof(Ps2SifDmaTransfer)));
            const uint8_t *entry = getConstMemPtr(rdram, entryAddr);
            if (!entry)
            {
                ok = false;
                break;
            }

            Ps2SifDmaTransfer xfer{};
            std::memcpy(&xfer, entry, sizeof(xfer));
            if (xfer.size <= 0)
            {
                continue;
            }

            const uint32_t sizeBytes = static_cast<uint32_t>(xfer.size);
            if (sizeBytes > PS2_RAM_SIZE)
            {
                ok = false;
                break;
            }
            if (!canCopyGuestByteRange(rdram, xfer.dest, xfer.src, sizeBytes))
            {
                ok = false;
                break;
            }

            pending[pendingCount++] = xfer;
        }

        if (ok)
        {
            for (uint32_t i = 0; i < pendingCount; ++i)
            {
                const Ps2SifDmaTransfer &xfer = pending[i];
                if (!copyGuestByteRange(rdram, xfer.dest, xfer.src, static_cast<uint32_t>(xfer.size)))
                {
                    ok = false;
                    break;
                }

                ps2_syscalls::noteDtxSifDmaTransfer(
                    rdram,
                    xfer.src,
                    xfer.dest,
                    static_cast<uint32_t>(xfer.size));
            }
        }

        if (!ok)
        {
            static uint32_t warnCount = 0;
            if (warnCount < 32u)
            {
                std::cerr << "sceSifSetDma failed dmat=0x" << std::hex << dmatAddr
                          << " count=0x" << count
                          << std::dec << std::endl;
                ++warnCount;
            }
            setReturnS32(ctx, 0);
            return;
        }

        ps2_syscalls::dispatchDmacHandlersForCause(rdram, runtime, 5u);

        setReturnS32(ctx, static_cast<int32_t>(allocateSifDmaTransferId()));
    }

    void sceSifSetIopAddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnU32(ctx, getRegU32(ctx, 5));
    }

    void sceSifSetReg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t reg = getRegU32(ctx, 4);
        const uint32_t value = getRegU32(ctx, 5);
        uint32_t prev = 0u;
        bool shouldLog = false;
        {
            std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
            auto it = g_sifRegs.find(reg);
            if (it != g_sifRegs.end())
            {
                prev = it->second;
            }
            g_sifRegs[reg] = value;
            shouldLog = shouldTraceSifReg(reg) && g_sifSetRegLogCount < 128u;
            if (shouldLog)
            {
                ++g_sifSetRegLogCount;
            }
        }
        if (shouldLog)
        {
            auto flags = std::cerr.flags();
            std::cerr << "[sceSifSetReg] reg=0x" << std::hex << reg
                      << " prev=0x" << prev
                      << " value=0x" << value
                      << " pc=0x" << (ctx ? ctx->pc : 0u)
                      << " ra=0x" << (ctx ? getRegU32(ctx, 31) : 0u)
                      << std::dec << std::endl;
            std::cerr.flags(flags);
        }
        setReturnU32(ctx, prev);
    }

    void sceSifSetRpcQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::SifSetRpcQueue(rdram, ctx, runtime);
    }

    void sceSifSetSreg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t reg = getRegU32(ctx, 4);
        const uint32_t value = getRegU32(ctx, 5);
        uint32_t prev = 0u;
        {
            std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
            auto it = g_sifSregs.find(reg);
            if (it != g_sifSregs.end())
            {
                prev = it->second;
            }
            g_sifSregs[reg] = value;
        }
        setReturnU32(ctx, prev);
    }

    void sceSifSetSysCmdBuffer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t newBuffer = getRegU32(ctx, 4);
        uint32_t prev = 0u;
        {
            std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
            prev = g_sifSysCmdBuffer;
            g_sifSysCmdBuffer = newBuffer;
        }
        setReturnU32(ctx, prev);
    }

    void sceSifStopDma(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceSifSyncIop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void sceSifWriteBackDCache(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }
}
