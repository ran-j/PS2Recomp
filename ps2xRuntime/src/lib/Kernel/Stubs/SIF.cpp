#include "Common.h"
#include "SIF.h"
#include "../Syscalls/RPC.h"
#include "../../ps2_iop_transport.h"
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_address.h"

#include <algorithm>
#include <limits>
#include <map>
#include <vector>

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
        std::array<uint8_t, kIopHeapLimit - kIopHeapBase> g_sifHeapStorage{};
        uint32_t g_sifCmdBuffer = 0u;
        uint32_t g_sifSysCmdBuffer = 0u;
        bool g_sifCmdInitialized = false;
        uint32_t g_sifGetRegLogCount = 0u;
        uint32_t g_sifSetRegLogCount = 0u;

        constexpr uint32_t kSifRegSubAddr = 0x2u;
        constexpr uint32_t kSifRegMsFlag = 0x3u;
        constexpr uint32_t kSifRegSmFlag = 0x4u;
        constexpr uint32_t kSifSysRegSubAddr = 0x80000000u;
        constexpr uint32_t kSifSysRegMainAddr = 0x80000001u;
        constexpr uint32_t kSifSysRegRpcInit = 0x80000002u;
        constexpr uint32_t kSifStatSifInit = 0x00010000u;
        constexpr uint32_t kSifStatCmdInit = 0x00020000u;
        constexpr uint32_t kSifStatBootEnd = 0x00040000u;

        bool isSifSystemRegister(uint32_t reg)
        {
            return reg == kSifSysRegSubAddr || reg == kSifSysRegMainAddr || reg == kSifSysRegRpcInit;
        }

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

            const uint32_t ready = kSifStatSifInit | kSifStatCmdInit | kSifStatBootEnd;
            g_sifRegs[kSifRegMsFlag] = ready;
            g_sifRegs[kSifRegSmFlag] = ready;
            g_sifRegs[kSifRegSubAddr] = kSifIopBuffer;
            g_sifRegs[kSifSysRegSubAddr] = 0u;
            g_sifRegs[kSifSysRegMainAddr] = 0u;
            g_sifRegs[kSifSysRegRpcInit] = 1u;
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
            std::fill_n(g_sifHeapStorage.data() + (candidate - kIopHeapBase),
                        alignedSize,
                        uint8_t{0});
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
            g_sifHeapStorage.fill(0u);
            g_iopHeapNext = kIopHeapBase;
        }

        bool isAllocatedSifHeapRangeLocked(uint32_t address, size_t size)
        {
            if (address < kIopHeapBase || address >= kIopHeapLimit || size > static_cast<size_t>(kIopHeapLimit - address))
            {
                return false;
            }

            auto it = g_sifHeapAllocations.upper_bound(address);
            if (it == g_sifHeapAllocations.begin())
            {
                return false;
            }
            --it;

            const uint64_t allocationEnd = static_cast<uint64_t>(it->first) + it->second;
            const uint64_t rangeEnd = static_cast<uint64_t>(address) + size;
            return address >= it->first && rangeEnd <= allocationEnd;
        }

        bool isCopyableGuestAddress(uint32_t addr)
        {
            if (Ps2AddressInRange(addr, PS2_SCRATCHPAD_BASE, PS2_SCRATCHPAD_SIZE))
            {
                return true;
            }

            if (addr < PS2_EE_UNCACHED_RAM_MIRROR_BASE)
            {
                return true;
            }

            if (Ps2IsUncachedRamMirrorAddress(addr))
            {
                return true;
            }

            if (Ps2IsKseg01Address(addr))
            {
                return true;
            }

            return false;
        }

        bool canCopyAddressRange(const uint8_t *rdram, uint32_t address, uint32_t sizeBytes)
        {
            if (isSifIopHeapRange(address, sizeBytes))
            {
                return true;
            }
            if (isSifIopHeapAddress(address) || !rdram)
            {
                return false;
            }
            if (sizeBytes == 0u)
            {
                return true;
            }
            if (sizeBytes - 1u > std::numeric_limits<uint32_t>::max() - address)
            {
                return false;
            }
            for (uint32_t i = 0u; i < sizeBytes; ++i)
            {
                const uint32_t byteAddress = address + i;
                if (!isCopyableGuestAddress(byteAddress) ||getConstMemPtr(rdram, byteAddress) == nullptr)
                {
                    return false;
                }
            }
            return true;
        }

        bool canCopyGuestByteRange(const uint8_t *rdram, uint32_t dstAddr, uint32_t srcAddr, uint32_t sizeBytes)
        {
            return canCopyAddressRange(rdram, srcAddr, sizeBytes) && canCopyAddressRange(rdram, dstAddr, sizeBytes);
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

            const bool sourceIsIop = isSifIopHeapRange(srcAddr, sizeBytes);
            const bool destinationIsIop = isSifIopHeapRange(dstAddr, sizeBytes);
            if (sourceIsIop || destinationIsIop)
            {
                std::vector<uint8_t> payload(sizeBytes);
                if (sourceIsIop)
                {
                    if (!readSifIopHeap(srcAddr, payload.data(), payload.size()))
                    {
                        return false;
                    }
                }
                else
                {
                    for (uint32_t i = 0u; i < sizeBytes; ++i)
                    {
                        const uint8_t *src = getConstMemPtr(rdram, srcAddr + i);
                        if (!src)
                        {
                            return false;
                        }
                        payload[i] = *src;
                    }
                }

                if (destinationIsIop)
                {
                    return writeSifIopHeap(dstAddr, payload.data(), payload.size());
                }

                ps2TraceGuestRangeWrite(rdram, dstAddr, sizeBytes, "sifCopyGuestByteRange", nullptr);
                for (uint32_t i = 0u; i < sizeBytes; ++i)
                {
                    uint8_t *dst = getMemPtr(rdram, dstAddr + i);
                    if (!dst)
                    {
                        return false;
                    }
                    *dst = payload[i];
                }
                return true;
            }

            ps2TraceGuestRangeWrite(rdram, dstAddr, sizeBytes, "sifCopyGuestByteRange", nullptr);

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

        constexpr uint32_t kCmdHeaderFunction = 8u;

        constexpr uint32_t kPacketRecordId = 16u;
        constexpr uint32_t kPacketRpcId = 24u;
        constexpr uint32_t kPacketClientData = 28u;
        constexpr uint32_t kPacketAllocatedBit = 0x01u;

        constexpr uint32_t kBindServerId = 32u;

        constexpr uint32_t kCallFunction = 32u;
        constexpr uint32_t kCallReceiveBuffer = 40u;
        constexpr uint32_t kCallReceiveSize = 44u;

        constexpr uint32_t kClientPacketAddress = 0u;
        constexpr uint32_t kClientSemaphore = 8u;
        constexpr uint32_t kClientBuffer = 20u;
        constexpr uint32_t kClientEndFunction = 28u;
        constexpr uint32_t kClientEndParameter = 32u;
        constexpr uint32_t kClientServer = 36u;

        constexpr uint32_t kSifCmdRpcBind = 0x80000009u;
        constexpr uint32_t kSifCmdRpcCall = 0x8000000Au;

        constexpr uint32_t kFileIoServerId = 0x80000001u;
        constexpr uint32_t kFileIoWrite = 3u;

        constexpr uint32_t kWriteArgDescriptor = 0u;
        constexpr uint32_t kWriteArgBuffer = 4u;
        constexpr uint32_t kWriteArgSize = 8u;

        constexpr uint32_t kMaxWriteBytes = 1u << 20;

        struct SifRpcResult
        {
            bool handled = false;
            int32_t semaphore = -1;
            uint32_t endFunction = 0u;
            uint32_t endParameter = 0u;
        };

        bool readGuestU32(uint8_t *rdram, uint32_t address, uint32_t &out)
        {
            const uint8_t *pointer = getConstMemPtr(rdram, address);
            if (!pointer)
            {
                return false;
            }
            std::memcpy(&out, pointer, sizeof(out));
            return true;
        }

        bool writeGuestU32(uint8_t *rdram, uint32_t address, uint32_t value)
        {
            uint8_t *pointer = getMemPtr(rdram, address);
            if (!pointer)
            {
                return false;
            }
            std::memcpy(pointer, &value, sizeof(value));
            return true;
        }

        bool readSifDmaTransfer(uint8_t *rdram, uint32_t listAddress, uint32_t index, Ps2SifDmaTransfer &out)
        {
            const uint8_t *entry = getConstMemPtr(rdram, listAddress + (index * sizeof(Ps2SifDmaTransfer)));
            if (!entry)
            {
                return false;
            }
            std::memcpy(&out, entry, sizeof(out));
            return true;
        }

        int32_t writeFileIoStream(uint8_t *rdram, uint32_t argument)
        {
            uint32_t descriptor = 0u;
            uint32_t buffer = 0u;
            uint32_t size = 0u;
            if (!readGuestU32(rdram, argument + kWriteArgDescriptor, descriptor) ||
                !readGuestU32(rdram, argument + kWriteArgBuffer, buffer) ||
                !readGuestU32(rdram, argument + kWriteArgSize, size))
            {
                return -1;
            }

            std::FILE *stream = nullptr;
            if (descriptor == 1u)
            {
                stream = stdout;
            }
            else if (descriptor == 2u)
            {
                stream = stderr;
            }
            else
            {
                return -1;
            }

            if (size == 0u)
            {
                return 0;
            }

            const uint32_t clamped = std::min(size, kMaxWriteBytes);
            std::string bytes;
            bytes.reserve(clamped);
            for (uint32_t offset = 0u; offset < clamped; ++offset)
            {
                const uint8_t *pointer = getConstMemPtr(rdram, buffer + offset);
                if (!pointer)
                {
                    break;
                }
                bytes.push_back(static_cast<char>(*pointer));
            }

            std::fwrite(bytes.data(), 1u, bytes.size(), stream);
            return static_cast<int32_t>(bytes.size());
        }

        void releaseSifRpcPacket(uint8_t *rdram, uint32_t packet, uint32_t clientData)
        {
            uint32_t recordId = 0u;
            if (readGuestU32(rdram, packet + kPacketRecordId, recordId))
            {
                writeGuestU32(rdram, packet + kPacketRecordId, recordId & ~kPacketAllocatedBit);
            }
            writeGuestU32(rdram, packet + kPacketRpcId, 0u);
            writeGuestU32(rdram, clientData + kClientPacketAddress, 0u);
        }

        bool bindFileIoServer(uint8_t *rdram, uint32_t packet, uint32_t clientData)
        {
            uint32_t serverId = 0u;
            if (!readGuestU32(rdram, packet + kBindServerId, serverId) || serverId != kFileIoServerId)
            {
                return false;
            }
            writeGuestU32(rdram, clientData + kClientServer, kSifIopBuffer);
            writeGuestU32(rdram, clientData + kClientBuffer, kSifIopBuffer);
            return true;
        }

        bool callFileIoServer(uint8_t *rdram,
                              uint32_t packet,
                              uint32_t clientData,
                              uint32_t transferList,
                              uint32_t transferCount)
        {
            uint32_t server = 0u;
            if (!readGuestU32(rdram, clientData + kClientServer, server) || server != kSifIopBuffer)
            {
                return false;
            }

            uint32_t rpcNumber = 0u;
            uint32_t receiveBuffer = 0u;
            uint32_t receiveSize = 0u;
            if (!readGuestU32(rdram, packet + kCallFunction, rpcNumber) ||
                !readGuestU32(rdram, packet + kCallReceiveBuffer, receiveBuffer) ||
                !readGuestU32(rdram, packet + kCallReceiveSize, receiveSize))
            {
                return false;
            }

            switch (rpcNumber)
            {
            case kFileIoWrite:
            {
                Ps2SifDmaTransfer payload{};
                if (transferCount < 2u || !readSifDmaTransfer(rdram, transferList, 0u, payload) || payload.src == 0u)
                {
                    return false;
                }

                const int32_t written = writeFileIoStream(rdram, payload.src);
                if (receiveSize >= sizeof(uint32_t) && receiveBuffer != 0u)
                {
                    writeGuestU32(rdram, receiveBuffer, static_cast<uint32_t>(written));
                }
                return true;
            }
            default:
                return false;
            }
        }

        SifRpcResult processSifRpcTransfer(uint8_t *rdram, uint32_t transferList, uint32_t transferCount)
        {
            SifRpcResult result;
            if (!rdram || transferList == 0u || transferCount == 0u || transferCount > 32u)
            {
                return result;
            }

            Ps2SifDmaTransfer packetTransfer{};
            if (!readSifDmaTransfer(rdram, transferList, transferCount - 1u, packetTransfer) ||
                packetTransfer.src == 0u)
            {
                return result;
            }

            const uint32_t packet = packetTransfer.src;
            uint32_t function = 0u;
            if (!readGuestU32(rdram, packet + kCmdHeaderFunction, function))
            {
                return result;
            }

            if (function != kSifCmdRpcBind && function != kSifCmdRpcCall)
            {
                result.handled = transferCount == 1u && packetTransfer.dest == kSifIopBuffer;
                return result;
            }

            uint32_t clientData = 0u;
            if (!readGuestU32(rdram, packet + kPacketClientData, clientData) || clientData == 0u)
            {
                return result;
            }

            const bool serviced = (function == kSifCmdRpcBind)
                                      ? bindFileIoServer(rdram, packet, clientData)
                                      : callFileIoServer(rdram, packet, clientData, transferList, transferCount);
            if (!serviced)
            {
                return result;
            }

            readGuestU32(rdram, clientData + kClientEndFunction, result.endFunction);
            readGuestU32(rdram, clientData + kClientEndParameter, result.endParameter);

            uint32_t semaphore = 0u;
            if (readGuestU32(rdram, clientData + kClientSemaphore, semaphore))
            {
                result.semaphore = static_cast<int32_t>(semaphore);
            }

            releaseSifRpcPacket(rdram, packet, clientData);
            result.handled = true;
            return result;
        }

    }

    bool isSifIopHeapAddress(uint32_t address)
    {
        return address >= kIopHeapBase && address < kIopHeapLimit;
    }

    bool isSifIopHeapRange(uint32_t address, size_t size)
    {
        std::lock_guard<std::mutex> lock(g_sifHeapMutex);
        return isAllocatedSifHeapRangeLocked(address, size);
    }

    bool readSifIopHeap(uint32_t address, void *destination, size_t size)
    {
        if (!destination && size != 0u)
        {
            return false;
        }
        std::lock_guard<std::mutex> lock(g_sifHeapMutex);
        if (!isAllocatedSifHeapRangeLocked(address, size))
        {
            return false;
        }
        if (size != 0u)
        {
            std::memcpy(destination,
                        g_sifHeapStorage.data() + (address - kIopHeapBase),
                        size);
        }
        return true;
    }

    bool writeSifIopHeap(uint32_t address, const void *source, size_t size)
    {
        if (!source && size != 0u)
        {
            return false;
        }
        std::lock_guard<std::mutex> lock(g_sifHeapMutex);
        if (!isAllocatedSifHeapRangeLocked(address, size))
        {
            return false;
        }
        if (size != 0u)
        {
            std::memcpy(g_sifHeapStorage.data() + (address - kIopHeapBase),
                        source,
                        size);
        }
        return true;
    }

    bool zeroSifIopHeap(uint32_t address, size_t size)
    {
        std::lock_guard<std::mutex> lock(g_sifHeapMutex);
        if (!isAllocatedSifHeapRangeLocked(address, size))
        {
            return false;
        }
        if (size != 0u)
        {
            std::memset(g_sifHeapStorage.data() + (address - kIopHeapBase), 0, size);
        }
        return true;
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

        if (runtime)
        {
            PS2IopTransport::notifyTransfer(runtime, rdram, {
                ps2x::iop::SifTransferKind::GetOtherData,
                ps2x::iop::SifTransferPhase::BeforeCopy,
                srcAddr,
                dstAddr,
                size,
            });
        }

        if (!copyGuestByteRange(rdram, dstAddr, srcAddr, size))
        {
            static uint32_t warnCount = 0;
            if (warnCount < 32u)
            {
                PS2_IF_AGRESSIVE_LOGS({
                    std::cerr << "sceSifGetOtherData copy failed src=0x" << std::hex << srcAddr
                              << " dst=0x" << dstAddr
                              << " size=0x" << size
                              << std::dec << std::endl;
                });
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

        if (runtime)
        {
            PS2IopTransport::notifyTransfer(runtime, rdram, {
                ps2x::iop::SifTransferKind::GetOtherData,
                ps2x::iop::SifTransferPhase::AfterCopy,
                srcAddr,
                dstAddr,
                size,
            });
        }

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
            PS2_IF_AGRESSIVE_LOGS({
                auto flags = std::cerr.flags();
                std::cerr << "[sceSifGetReg] reg=0x" << std::hex << reg
                          << " value=0x" << value
                          << " pc=0x" << (ctx ? ctx->pc : 0u)
                          << " ra=0x" << (ctx ? getRegU32(ctx, 31) : 0u)
                          << std::dec << std::endl;
                std::cerr.flags(flags);
            });
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
        const uint32_t dmatAddr = getRegU32(ctx, 4);
        const uint32_t count = getRegU32(ctx, 5);

        const uint32_t listAddr = getRegU32(ctx, 4);
        PS2_IF_AGRESSIVE_LOGS({
            std::cerr << "[sceSifSetDma:CALL] pc=0x" << std::hex << ctx->pc
                      << " ra=0x" << getRegU32(ctx, 31)
                      << " list=0x" << listAddr
                      << " count=" << std::dec << count
                      << std::endl;

            for (uint32_t i = 0; i < count; ++i)
            {
                const uint32_t desc = listAddr + i * 16;
                const uint32_t src = READ32(desc + 0);
                const uint32_t dst = READ32(desc + 4);
                const uint32_t size = READ32(desc + 8);
                const uint32_t attr = READ32(desc + 12);

                std::cerr << "[sceSifSetDma:DESC] i=" << i
                          << " src=0x" << std::hex << src
                          << " dst=0x" << dst
                          << " size=0x" << size
                          << " attr=0x" << attr
                          << " pc=0x" << ctx->pc
                          << " ra=0x" << getRegU32(ctx, 31)
                          << std::dec << std::endl;
            }
        });

        if (!dmatAddr || count == 0u || count > 32u)
        {
            setReturnS32(ctx, 0);
            return;
        }

        const SifRpcResult rpc = processSifRpcTransfer(rdram, dmatAddr, count);
        if (rpc.handled)
        {
            setReturnS32(ctx, static_cast<int32_t>(allocateSifDmaTransferId()));
            if (rpc.semaphore >= 0 && runtime)
            {
                runtime->eeScheduler().signalSemaphore(rpc.semaphore, true);
            }
            if (rpc.endFunction != 0u && runtime && runtime->hasFunction(rpc.endFunction))
            {
                GuestInvocation callback{};
                callback.kind = GuestInvocationKind::RpcCallback;
                callback.context = *ctx;
                callback.context.pc = rpc.endFunction;
                SET_GPR_U32(&callback.context, 4, rpc.endParameter);
                SET_GPR_U32(&callback.context, 29, runtime->eeScheduler().invocationStackTop());
                SET_GPR_U32(&callback.context, 31, 0u);
                runtime->eeScheduler().invokeCurrent(std::move(callback));
            }
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
                if (runtime)
                {
                    PS2IopTransport::notifyTransfer(runtime, rdram, {
                        ps2x::iop::SifTransferKind::SetDma,
                        ps2x::iop::SifTransferPhase::BeforeCopy,
                        xfer.src,
                        xfer.dest,
                        static_cast<uint32_t>(xfer.size),
                    });
                }
                if (!copyGuestByteRange(rdram, xfer.dest, xfer.src, static_cast<uint32_t>(xfer.size)))
                {
                    ok = false;
                    break;
                }
                if (runtime)
                {
                    PS2IopTransport::notifyTransfer(runtime, rdram, {
                        ps2x::iop::SifTransferKind::SetDma,
                        ps2x::iop::SifTransferPhase::AfterCopy,
                        xfer.src,
                        xfer.dest,
                        static_cast<uint32_t>(xfer.size),
                    });
                }
            }
        }

        if (!ok)
        {
            static uint32_t warnCount = 0;
            if (warnCount < 32u)
            {
                PS2_IF_AGRESSIVE_LOGS({
                    std::cerr << "sceSifSetDma failed dmat=0x" << std::hex << dmatAddr
                              << " count=0x" << count
                              << std::dec << std::endl;
                });
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
            if (!isSifSystemRegister(reg))
            {
                g_sifRegs[reg] = value;
            }
            shouldLog = shouldTraceSifReg(reg) && g_sifSetRegLogCount < 128u;
            if (shouldLog)
            {
                ++g_sifSetRegLogCount;
            }
        }
        if (shouldLog)
        {
            PS2_IF_AGRESSIVE_LOGS({
                auto flags = std::cerr.flags();
                std::cerr << "[sceSifSetReg] reg=0x" << std::hex << reg
                          << " prev=0x" << prev
                          << " value=0x" << value
                          << " pc=0x" << (ctx ? ctx->pc : 0u)
                          << " ra=0x" << (ctx ? getRegU32(ctx, 31) : 0u)
                          << std::dec << std::endl;
                std::cerr.flags(flags);
            });
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
