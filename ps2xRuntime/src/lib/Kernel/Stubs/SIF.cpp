#include "Common.h"
#include "SIF.h"
#include "../Syscalls/RPC.h"
#include "../../ps2_iop_transport.h"
#include "runtime/ps2_address.h"

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
        struct SifCommandHandler
        {
            uint32_t function = 0u;
            uint32_t argument = 0u;
        };

        std::unordered_map<uint32_t, SifCommandHandler> g_sifCmdHandlers;
        std::unordered_map<uint32_t, uint32_t> g_rawSifRpcClients;
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
            g_rawSifRpcClients.clear();
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

        constexpr uint32_t kSifRpcEndCommand = 0x80000008u;
        constexpr uint32_t kSifRpcBindCommand = 0x80000009u;
        constexpr uint32_t kSifRpcCallCommand = 0x8000000Au;
        constexpr uint32_t kRawSifRpcServerToken = 0x00010000u;

        bool copyGuestByteRange(uint8_t *rdram,
                                uint32_t dstAddr,
                                uint32_t srcAddr,
                                uint32_t sizeBytes);

        bool readGuestU32(uint8_t *rdram, uint32_t address, uint32_t &value)
        {
            const uint8_t *source = getConstMemPtr(rdram, address);
            if (!source)
            {
                return false;
            }
            std::memcpy(&value, source, sizeof(value));
            return true;
        }

        bool writeGuestU32(uint8_t *rdram, uint32_t address, uint32_t value)
        {
            uint8_t *destination = getMemPtr(rdram, address);
            if (!destination)
            {
                return false;
            }
            std::memcpy(destination, &value, sizeof(value));
            return true;
        }

        uint32_t rawSifRpcReceiveBuffer(uint8_t *rdram)
        {
            uint32_t receivePointerAddress = 0u;
            {
                std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
                const auto it = g_sifRegs.find(kSifRegSubAddr);
                if (it == g_sifRegs.end())
                {
                    return 0u;
                }
                receivePointerAddress = it->second;
            }

            uint32_t receiveAddress = 0u;
            return readGuestU32(rdram, receivePointerAddress, receiveAddress)
                       ? receiveAddress
                       : 0u;
        }

        bool invokeSifCommandHandler(uint8_t *rdram,
                                     R5900Context *ctx,
                                     PS2Runtime *runtime,
                                     const SifCommandHandler &handler,
                                     uint32_t packetAddress)
        {
            if (!rdram || !ctx || !runtime || handler.function == 0u ||
                !runtime->hasFunction(handler.function))
            {
                return false;
            }

            constexpr uint32_t kCallbackStackSize = 0x4000u;
            constexpr uint32_t kReturnSentinel = 0x00FFF000u;
            constexpr uint32_t kMaxSteps = 0x8000u;

            R5900Context callback = *ctx;
            SET_GPR_U32(&callback, 4, packetAddress);
            SET_GPR_U32(&callback, 5, handler.argument);
            SET_GPR_U32(&callback, 6, 0u);
            SET_GPR_U32(&callback, 7, 0u);

            thread_local uint32_t callbackStackTop = 0u;
            if (callbackStackTop == 0u)
            {
                const uint32_t stackBase =
                    runtime->guestMalloc(kCallbackStackSize, 16u);
                if (stackBase != 0u)
                {
                    callbackStackTop =
                        (stackBase + kCallbackStackSize) & ~0xFu;
                }
            }
            if (callbackStackTop != 0u)
            {
                SET_GPR_U32(&callback, 29, callbackStackTop);
            }

            SET_GPR_U32(&callback, 31, kReturnSentinel);
            callback.pc = handler.function;
            uint32_t steps = 0u;
            while (callback.pc != 0u &&
                   callback.pc != kReturnSentinel &&
                   runtime->hasFunction(callback.pc) &&
                   steps < kMaxSteps)
            {
                const PS2Runtime::RecompiledFunction function =
                    runtime->lookupFunction(callback.pc);
                {
                    PS2Runtime::GuestExecutionScope guestExecution(runtime);
                    function(rdram, &callback, runtime);
                }
                ++steps;
            }
            return callback.pc == kReturnSentinel;
        }

        void completeRawSifRpcClient(uint8_t *rdram,
                                     R5900Context *ctx,
                                     PS2Runtime *runtime,
                                     uint32_t responseAddress,
                                     uint32_t clientAddress,
                                     uint32_t requestCommand,
                                     uint32_t server)
        {
            if (clientAddress == 0u || !getMemPtr(rdram, clientAddress))
            {
                return;
            }

            uint32_t packetAddress = 0u;
            uint32_t semaphore = 0xFFFFFFFFu;
            uint32_t endFunction = 0u;
            uint32_t endParameter = 0u;
            uint32_t serverBuffer = 0u;
            uint32_t serverCopyBuffer = 0u;
            if (!readGuestU32(rdram, clientAddress, packetAddress) ||
                !readGuestU32(rdram, clientAddress + 0x08u, semaphore) ||
                !readGuestU32(rdram, clientAddress + 0x1Cu, endFunction) ||
                !readGuestU32(rdram, clientAddress + 0x20u, endParameter) ||
                !readGuestU32(rdram, responseAddress + 0x28u, serverBuffer) ||
                !readGuestU32(rdram, responseAddress + 0x2Cu, serverCopyBuffer))
            {
                return;
            }

            if (requestCommand == kSifRpcBindCommand)
            {
                (void)writeGuestU32(rdram, clientAddress + 0x24u, server);
            }
            (void)writeGuestU32(rdram, clientAddress + 0x14u, serverBuffer);
            (void)writeGuestU32(rdram, clientAddress + 0x18u, serverCopyBuffer);

            if (requestCommand == kSifRpcCallCommand && endFunction != 0u)
            {
                const SifCommandHandler callback{endFunction, 0u};
                (void)invokeSifCommandHandler(rdram,
                                              ctx,
                                              runtime,
                                              callback,
                                              endParameter);
            }

            if (static_cast<int32_t>(semaphore) >= 0)
            {
                R5900Context signalContext = *ctx;
                SET_GPR_U32(&signalContext, 4, semaphore);
                ps2_syscalls::SignalSema(rdram, &signalContext, runtime);
            }

            if (packetAddress != 0u)
            {
                uint32_t packetFlags = 0u;
                if (readGuestU32(rdram, packetAddress + 0x10u, packetFlags))
                {
                    (void)writeGuestU32(rdram,
                                        packetAddress + 0x10u,
                                        packetFlags & ~1u);
                }
                (void)writeGuestU32(rdram, packetAddress + 0x18u, 0u);
            }
            (void)writeGuestU32(rdram, clientAddress, 0u);
        }

        bool writeRawSifRpcEnd(uint8_t *rdram,
                               R5900Context *ctx,
                               PS2Runtime *runtime,
                               uint32_t recordId,
                               uint32_t packetAddress,
                               uint32_t rpcId,
                               uint32_t client,
                               uint32_t requestCommand,
                               uint32_t server)
        {
            const uint32_t responseAddress = rawSifRpcReceiveBuffer(rdram);
            const uint32_t normalizedResponseAddress = responseAddress & PS2_RAM_MASK;
            uint8_t *response = getMemPtr(rdram, responseAddress);
            if (!response || responseAddress == 0u ||
                normalizedResponseAddress > PS2_RAM_SIZE - 0x30u)
            {
                return false;
            }

            std::memset(response, 0, 0x30u);
            const bool wroteResponse =
                writeGuestU32(rdram, responseAddress, 0x30u) &&
                writeGuestU32(rdram, responseAddress + 0x08u, kSifRpcEndCommand) &&
                writeGuestU32(rdram, responseAddress + 0x10u, recordId) &&
                writeGuestU32(rdram, responseAddress + 0x14u, packetAddress) &&
                writeGuestU32(rdram, responseAddress + 0x18u, rpcId) &&
                writeGuestU32(rdram, responseAddress + 0x1Cu, client) &&
                writeGuestU32(rdram, responseAddress + 0x20u, requestCommand) &&
                writeGuestU32(rdram, responseAddress + 0x24u, server);
            if (!wroteResponse)
            {
                return false;
            }

            SifCommandHandler handler{};
            {
                std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
                const auto it = g_sifCmdHandlers.find(kSifRpcEndCommand);
                if (it != g_sifCmdHandlers.end())
                {
                    handler = it->second;
                }
            }

            bool dispatched = false;
            if (handler.function != 0u)
            {
                dispatched = invokeSifCommandHandler(rdram,
                                                     ctx,
                                                     runtime,
                                                     handler,
                                                     responseAddress);
            }
            if (dispatched)
            {
                // The registered command handler consumed this inbound
                // command synchronously; do not queue it a second time.
                (void)writeGuestU32(rdram, responseAddress + 0x08u, 0u);
                runtime->yieldGuestExecutionAfterWake();
            }
            else
            {
                completeRawSifRpcClient(rdram,
                                        ctx,
                                        runtime,
                                        responseAddress,
                                        client,
                                        requestCommand,
                                        server);
            }
            return true;
        }

        bool dispatchRawSifRpc(uint8_t *rdram,
                               R5900Context *ctx,
                               PS2Runtime *runtime,
                               const Ps2SifDmaTransfer *transfers,
                               uint32_t transferCount)
        {
            if (!runtime)
            {
                return false;
            }

            for (uint32_t index = 0; index < transferCount; ++index)
            {
                const Ps2SifDmaTransfer &packetTransfer = transfers[index];
                if (packetTransfer.size < 0x40 || (packetTransfer.attr & 0x4) == 0)
                {
                    continue;
                }

                uint32_t packetSize = 0u;
                uint32_t command = 0u;
                if (!readGuestU32(rdram, packetTransfer.src, packetSize) ||
                    !readGuestU32(rdram, packetTransfer.src + 0x08u, command) ||
                    (packetSize & 0xFFu) != 0x40u)
                {
                    continue;
                }

                uint32_t recordId = 0u;
                uint32_t packetAddress = 0u;
                uint32_t rpcId = 0u;
                uint32_t client = 0u;
                if (!readGuestU32(rdram, packetTransfer.src + 0x10u, recordId) ||
                    !readGuestU32(rdram, packetTransfer.src + 0x14u, packetAddress) ||
                    !readGuestU32(rdram, packetTransfer.src + 0x18u, rpcId) ||
                    !readGuestU32(rdram, packetTransfer.src + 0x1Cu, client))
                {
                    continue;
                }

                if (command == kSifRpcBindCommand)
                {
                    uint32_t sid = 0u;
                    if (!readGuestU32(rdram, packetTransfer.src + 0x20u, sid) ||
                        !PS2IopTransport::handlesSid(runtime, sid))
                    {
                        continue;
                    }
                    {
                        std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
                        g_rawSifRpcClients[client] = sid;
                    }
                    return writeRawSifRpcEnd(rdram,
                                             ctx,
                                             runtime,
                                             recordId,
                                             packetAddress,
                                             rpcId,
                                             client,
                                             command,
                                             kRawSifRpcServerToken);
                }

                if (command != kSifRpcCallCommand)
                {
                    continue;
                }

                uint32_t sid = 0u;
                {
                    std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
                    const auto clientIt = g_rawSifRpcClients.find(client);
                    if (clientIt == g_rawSifRpcClients.end())
                    {
                        continue;
                    }
                    sid = clientIt->second;
                }

                uint32_t function = 0u;
                uint32_t sendSize = 0u;
                uint32_t receiveAddress = 0u;
                uint32_t receiveSize = 0u;
                uint32_t mode = 0u;
                if (!readGuestU32(rdram, packetTransfer.src + 0x20u, function) ||
                    !readGuestU32(rdram, packetTransfer.src + 0x24u, sendSize) ||
                    !readGuestU32(rdram, packetTransfer.src + 0x28u, receiveAddress) ||
                    !readGuestU32(rdram, packetTransfer.src + 0x2Cu, receiveSize) ||
                    !readGuestU32(rdram, packetTransfer.src + 0x30u, mode))
                {
                    continue;
                }

                uint32_t sendAddress = 0u;
                if (sendSize != 0u)
                {
                    for (uint32_t payloadIndex = 0; payloadIndex < transferCount; ++payloadIndex)
                    {
                        const Ps2SifDmaTransfer &payload = transfers[payloadIndex];
                        if (payloadIndex != index &&
                            payload.size == static_cast<int32_t>(sendSize) &&
                            (payload.attr & 0x4) == 0)
                        {
                            sendAddress = payload.src;
                            break;
                        }
                    }
                    if (sendAddress == 0u)
                    {
                        continue;
                    }
                }

                ps2x::iop::RpcRequest request{};
                request.clientAddress = client;
                request.serverAddress = kRawSifRpcServerToken;
                request.sid = sid;
                request.function = function;
                request.mode = mode;
                request.send = {sendAddress, sendSize};
                request.receive = {receiveAddress, receiveSize};

                const ps2x::iop::RpcResult result =
                    PS2IopTransport::handleRpc(runtime, rdram, ctx, request);
                if (!result.handled)
                {
                    continue;
                }
                if (receiveAddress != 0u && receiveSize != 0u &&
                    result.resultAddress != 0u &&
                    result.resultAddress != receiveAddress)
                {
                    if (!copyGuestByteRange(rdram,
                                            receiveAddress,
                                            result.resultAddress,
                                            receiveSize))
                    {
                        continue;
                    }
                }
                return writeRawSifRpcEnd(rdram,
                                         ctx,
                                         runtime,
                                         recordId,
                                         packetAddress,
                                         rpcId,
                                         client,
                                         command,
                                         0u);
            }
            return false;
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
        const uint32_t argument = getRegU32(ctx, 6);
        std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
        g_sifCmdHandlers[cid] = {handler, argument};
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

        if (ok)
        {
            (void)dispatchRawSifRpc(rdram, ctx, runtime, pending.data(), pendingCount);
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
            g_sifRegs[reg] = value;
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
