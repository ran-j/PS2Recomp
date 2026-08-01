#include "Common.h"
#include "RPC.h"
#include "../../ps2_iop_transport.h"

namespace ps2_syscalls
{
    namespace
    {
        SifRpcDebugEvent makeRpcDebugEvent(const char *op, R5900Context *ctx)
        {
            SifRpcDebugEvent event{};
            event.op = op;
            event.pc = ctx ? ctx->pc : 0u;
            event.ra = ctx ? getRegU32(ctx, 31) : 0u;
            event.threadId = static_cast<uint32_t>(g_currentThreadId);
            return event;
        }

        void pushSifRpcDebugEventLocked(SifRpcDebugEvent event)
        {
            event.seq = ++g_sif_rpc_debug_next_seq;
            g_sif_rpc_debug_history[event.seq % kSifRpcDebugHistoryCount] = event;
        }

        void pushSifRpcDebugEvent(SifRpcDebugEvent event)
        {
            std::lock_guard<std::mutex> lock(g_rpc_mutex);
            pushSifRpcDebugEventLocked(event);
        }

        void fillRpcDebugPreview(const uint8_t *rdram, uint32_t addr, uint32_t size, uint8_t *preview, uint32_t &previewSize)
        {
            previewSize = 0u;
            if (!rdram || !addr || !preview || size == 0u)
            {
                return;
            }

            const uint32_t count = std::min<uint32_t>(size, static_cast<uint32_t>(kSifRpcDebugPreviewBytes));
            for (uint32_t i = 0; i < count; ++i)
            {
                const uint8_t *ptr = getConstMemPtr(rdram, addr + i);
                if (!ptr)
                {
                    break;
                }
                preview[i] = *ptr;
                ++previewSize;
            }
        }

        std::string formatRpcTraceBytes(const uint8_t *bytes, uint32_t count)
        {
            std::string out;
            if (!bytes || count == 0u)
            {
                return out;
            }

            char item[4] = {};
            for (uint32_t i = 0; i < count; ++i)
            {
                std::snprintf(item, sizeof(item), "%02X", bytes[i]);
                if (!out.empty())
                {
                    out.push_back(' ');
                }
                out += item;
            }
            return out;
        }

#ifndef PS2X_ENABLE_IOP_RPC_TRACE
#define PS2X_ENABLE_IOP_RPC_TRACE 1
#endif

#if PS2X_ENABLE_IOP_RPC_TRACE
        std::string loadedIopModuleTraceSummary()
        {
            std::lock_guard<std::mutex> lock(g_sif_module_mutex);
            std::string out;
            uint32_t count = 0u;
            for (const auto &entry : g_sif_modules_by_id)
            {
                const SifModuleRecord &module = entry.second;
                if (!module.loaded)
                {
                    continue;
                }

                if (!out.empty())
                {
                    out += "; ";
                }
                out += module.pathKey.empty() ? module.path : module.pathKey;
                ++count;
                if (count >= 6u)
                {
                    break;
                }
            }
            return out;
        }

        void logUnhandledRpcTrace(const SifRpcDebugEvent &event)
        {
            static std::unordered_set<uint64_t> loggedSignatures;
            if (std::strcmp(event.op ? event.op : "", "CallRpc") != 0)
            {
                return;
            }

            uint64_t signature = 1469598103934665603ull;
            auto mixSignature = [&](uint32_t value)
            {
                signature ^= static_cast<uint64_t>(value);
                signature *= 1099511628211ull;
            };
            mixSignature(event.sid);
            mixSignature(event.rpcNum);
            mixSignature(event.sendSize);
            mixSignature(event.recvSize);
            if (!loggedSignatures.insert(signature).second || loggedSignatures.size() > 128u)
            {
                return;
            }

            const std::string modules = loadedIopModuleTraceSummary();
            std::cerr << "[IOP/RPC trace:unhandled]"
                      << " sid=0x" << std::hex << event.sid
                      << " rpc=0x" << event.rpcNum
                      << " pc=0x" << event.pc
                      << " ra=0x" << event.ra
                      << " send=0x" << event.sendBuf << "/" << std::dec << event.sendSize
                      << " recv=0x" << std::hex << event.recvBuf << "/" << std::dec << event.recvSize
                      << " sendBytes=[" << formatRpcTraceBytes(event.sendPreview, event.sendPreviewSize) << "]"
                      << " loadedModules=[" << modules << "]"
                      << std::dec << std::endl;
        }
#endif

        bool signalRpcCompletionSema(uint32_t semaId)
        {
            if (semaId == 0u || semaId > 0xFFFFu)
            {
                return false;
            }

            auto sema = lookupSemaInfo(static_cast<int>(semaId));
            if (!sema)
            {
                return false;
            }

            bool signaled = false;
            int wokenTid = 0;
            ps2sched::FiberToken wokenToken{};
            {
                std::lock_guard<std::mutex> lock(sema->m);
                if (!sema->deleted && sema->count < sema->maxCount)
                {
                    sema->count++;
                    signaled = true;
                    if (!sema->waitList.empty())
                    {
                        wokenTid   = sema->waitList.front().first;
                        wokenToken = sema->waitList.front().second;
                        sema->waitList.erase(sema->waitList.begin());
                    }
                }
            }

            if (wokenTid != 0)
            {
                // Called from the RPC worker (non-guest host thread). Use the
                // validated variant to avoid stale wakeups if the tid was recycled.
                ps2sched::enqueue_external_wakeup_validated(wokenTid, wokenToken);
            }
            return signaled;
        }

    } // namespace

    void SifStopModule(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int32_t moduleId = static_cast<int32_t>(getRegU32(ctx, 4)); // $a0
        const uint32_t resultAddr = getRegU32(ctx, 7);                    // $a3 (int* result, optional)

        uint32_t refsLeft = 0;
        const bool knownModule = trackSifModuleStop(moduleId, &refsLeft);
        const int32_t ret = knownModule ? 0 : -1;

        if (resultAddr != 0)
        {
            int32_t *hostResult = reinterpret_cast<int32_t *>(getMemPtr(rdram, resultAddr));
            if (hostResult)
            {
                *hostResult = knownModule ? 0 : -1;
            }
        }

        if (knownModule)
        {
            std::string modulePath;
            {
                std::lock_guard<std::mutex> lock(g_sif_module_mutex);
                auto it = g_sif_modules_by_id.find(moduleId);
                if (it != g_sif_modules_by_id.end())
                {
                    modulePath = it->second.path;
                }
            }
            logSifModuleAction("stop", moduleId, modulePath, refsLeft);
        }

        setReturnS32(ctx, ret);
    }

    void SifLoadModule(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t pathAddr = getRegU32(ctx, 4); // $a0
        const std::string modulePath = readGuestCStringBounded(rdram, pathAddr, kMaxSifModulePathBytes);
        if (modulePath.empty())
        {
            setReturnS32(ctx, -1);
            return;
        }

        const int32_t moduleId = trackSifModuleLoad(modulePath);
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
        logSifModuleAction("load", moduleId, modulePath, refs);

        setReturnS32(ctx, moduleId);
    }

    void SifInitRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::lock_guard<std::mutex> lock(g_rpc_mutex);
        if (runtime)
        {
            PS2IopTransport::reset(runtime);
        }
        if (!g_rpc_initialized)
        {
            g_rpc_servers.clear();
            g_rpc_clients.clear();
            g_rpc_next_id = 1;
            g_rpc_packet_index = 0;
            g_rpc_server_index = 0;
            g_rpc_active_queue = 0;
            g_sif_rpc_debug_next_seq = 0;
            for (size_t i = 0; i < kSifRpcDebugHistoryCount; ++i)
            {
                g_sif_rpc_debug_history[i] = SifRpcDebugEvent{};
            }
            g_rpc_initialized = true;
            RUNTIME_LOG("[SifInitRpc] Initialized");
        }

        SifRpcDebugEvent event = makeRpcDebugEvent("InitRpc", ctx);
        event.result = 0;
        pushSifRpcDebugEventLocked(event);
        setReturnS32(ctx, 0);
    }

    void SifBindRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t clientPtr = getRegU32(ctx, 4);
        uint32_t rpcId = getRegU32(ctx, 5);
        uint32_t mode = getRegU32(ctx, 6);

        t_SifRpcClientData *client = reinterpret_cast<t_SifRpcClientData *>(getMemPtr(rdram, clientPtr));

        if (!client)
        {
            SifRpcDebugEvent event = makeRpcDebugEvent("BindRpc", ctx);
            event.clientPtr = clientPtr;
            event.sid = rpcId;
            event.mode = mode;
            event.flags = kSifRpcDebugFlagMissingClient;
            event.result = -1;
            pushSifRpcDebugEvent(event);
            setReturnS32(ctx, -1);
            return;
        }

        client->command = 0;
        client->buf = 0;
        client->cbuf = 0;
        client->end_function = 0;
        client->end_param = 0;
        client->server = 0;
        client->hdr.pkt_addr = 0;
        client->hdr.sema_id = -1;
        client->hdr.mode = mode;

        uint32_t serverPtr = 0;
        {
            std::lock_guard<std::mutex> lock(g_rpc_mutex);
            client->hdr.rpc_id = g_rpc_next_id++;
            auto it = g_rpc_servers.find(rpcId);
            if (it != g_rpc_servers.end())
            {
                serverPtr = it->second.sd_ptr;
            }
            g_rpc_clients[clientPtr] = {};
            g_rpc_clients[clientPtr].sid = rpcId;
        }

        if (!serverPtr)
        {
            // Allocate a dummy server so bind loops can proceed.
            serverPtr = rpcAllocServerAddr(rdram);
            if (serverPtr)
            {
                t_SifRpcServerData *dummy = reinterpret_cast<t_SifRpcServerData *>(getMemPtr(rdram, serverPtr));
                if (dummy)
                {
                    std::memset(dummy, 0, sizeof(*dummy));
                    dummy->sid = static_cast<int>(rpcId);
                }
                std::lock_guard<std::mutex> lock(g_rpc_mutex);
                g_rpc_servers[rpcId] = {rpcId, serverPtr};
            }
        }

        if (serverPtr)
        {
            t_SifRpcServerData *sd = reinterpret_cast<t_SifRpcServerData *>(getMemPtr(rdram, serverPtr));
            client->server = serverPtr;
            client->buf = sd ? sd->buf : 0;
            client->cbuf = sd ? sd->cbuf : 0;
        }
        else
        {
            client->server = 0;
            client->buf = 0;
            client->cbuf = 0;
        }

        SifRpcDebugEvent event = makeRpcDebugEvent("BindRpc", ctx);
        event.clientPtr = clientPtr;
        event.serverPtr = serverPtr;
        event.sid = rpcId;
        event.mode = mode;
        event.result = 0;
        pushSifRpcDebugEvent(event);
        setReturnS32(ctx, 0);
    }

    void SifCallRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::lock_guard<std::recursive_mutex> rpcCallLock(g_sif_call_rpc_mutex);

        const uint32_t clientPtr = getRegU32(ctx, 4);
        const uint32_t rpcNum = getRegU32(ctx, 5);
        const uint32_t mode = getRegU32(ctx, 6);
        const uint32_t sendBuf = getRegU32(ctx, 7);
        const uint32_t stackPointer = getRegU32(ctx, 29);

        const uint32_t sendSizeRegisters = getRegU32(ctx, 8);
        const uint32_t receiveBufferRegisters = getRegU32(ctx, 9);
        const uint32_t receiveSizeRegisters = getRegU32(ctx, 10);
        const uint32_t endFunctionRegisters = getRegU32(ctx, 11);
        uint32_t endParameterRegisters = 0u;
        (void)readStackU32(rdram, stackPointer, 0x0u, endParameterRegisters);

        uint32_t sendSizeStack = 0u;
        uint32_t receiveBufferStack = 0u;
        uint32_t receiveSizeStack = 0u;
        uint32_t endFunctionStack = 0u;
        uint32_t endParameterStack = 0u;
        (void)readStackU32(rdram, stackPointer, 0x10u, sendSizeStack);
        (void)readStackU32(rdram, stackPointer, 0x14u, receiveBufferStack);
        (void)readStackU32(rdram, stackPointer, 0x18u, receiveSizeStack);
        (void)readStackU32(rdram, stackPointer, 0x1Cu, endFunctionStack);
        (void)readStackU32(rdram, stackPointer, 0x20u, endParameterStack);

        const auto looksLikeGuestPointer = [](uint32_t value)
        {
            if (value == 0u)
            {
                return true;
            }
            const uint32_t normalized = value & 0x1FFFFFFFu;
            return normalized >= 0x10000u && normalized < PS2_RAM_SIZE;
        };
        const auto looksLikeSize = [](uint32_t value)
        {
            return value <= 0x02000000u;
        };
        const auto plausiblePack = [&](uint32_t sendSize,
                                       uint32_t receiveBuffer,
                                       uint32_t receiveSize,
                                       uint32_t endFunction)
        {
            return looksLikeSize(sendSize) &&
                   looksLikeGuestPointer(receiveBuffer) &&
                   looksLikeSize(receiveSize) &&
                   (endFunction == 0u || looksLikeGuestPointer(endFunction));
        };

        const bool registerPackPlausible =
            plausiblePack(sendSizeRegisters,
                          receiveBufferRegisters,
                          receiveSizeRegisters,
                          endFunctionRegisters);
        const bool stackPackPlausible =
            plausiblePack(sendSizeStack,
                          receiveBufferStack,
                          receiveSizeStack,
                          endFunctionStack);

        uint32_t sidHint = 0u;
        {
            std::lock_guard<std::mutex> lock(g_rpc_mutex);
            const auto clientIt = g_rpc_clients.find(clientPtr);
            if (clientIt != g_rpc_clients.end())
            {
                sidHint = clientIt->second.sid;
            }
        }

        ps2x::iop::RpcAbi selectedAbi = ps2x::iop::RpcAbi::RuntimeDefault;
        {
            ps2x::iop::RpcAbiRequest request{};
            request.boundSid = sidHint;
            request.function = rpcNum;
            request.registers = {
                sendSizeRegisters,
                receiveBufferRegisters,
                receiveSizeRegisters,
                endFunctionRegisters,
                endParameterRegisters,
                registerPackPlausible,
            };
            request.stack = {
                sendSizeStack,
                receiveBufferStack,
                receiveSizeStack,
                endFunctionStack,
                endParameterStack,
                stackPackPlausible,
            };
            selectedAbi = PS2IopTransport::selectRpcAbi(runtime, request);
        }

        bool useRegisterConvention = selectedAbi != ps2x::iop::RpcAbi::Stack;
        if (selectedAbi == ps2x::iop::RpcAbi::RuntimeDefault && !registerPackPlausible && stackPackPlausible)
        {
            const bool registersHaveCallback = endFunctionRegisters != 0u && looksLikeGuestPointer(endFunctionRegisters);
            const bool stackHasCallback = endFunctionStack != 0u && looksLikeGuestPointer(endFunctionStack);
            if (!(registersHaveCallback && !stackHasCallback))
            {
                useRegisterConvention = false;
            }
        }
        else if (selectedAbi == ps2x::iop::RpcAbi::Registers)
        {
            useRegisterConvention = true;
        }

        const uint32_t sendSize = useRegisterConvention ? sendSizeRegisters : sendSizeStack;
        const uint32_t receiveBuffer = useRegisterConvention ? receiveBufferRegisters : receiveBufferStack;
        const uint32_t receiveSize = useRegisterConvention ? receiveSizeRegisters : receiveSizeStack;
        const uint32_t endFunction = useRegisterConvention ? endFunctionRegisters : endFunctionStack;
        const uint32_t endParameter = useRegisterConvention ? endParameterRegisters : endParameterStack;

        auto *client = reinterpret_cast<t_SifRpcClientData *>(getMemPtr(rdram, clientPtr));
        if (!client)
        {
            SifRpcDebugEvent event = makeRpcDebugEvent("CallRpc", ctx);
            event.clientPtr = clientPtr;
            event.sid = sidHint;
            event.rpcNum = rpcNum;
            event.mode = mode;
            event.sendBuf = sendBuf;
            event.sendSize = sendSize;
            event.recvBuf = receiveBuffer;
            event.recvSize = receiveSize;
            event.endFunc = endFunction;
            event.endParam = endParameter;
            event.flags = kSifRpcDebugFlagMissingClient | ((mode & kSifRpcModeNowait) ? kSifRpcDebugFlagNowait : 0u);
            event.result = -1;
            pushSifRpcDebugEvent(event);
            setReturnS32(ctx, -1);
            return;
        }

        client->command = rpcNum;
        client->end_function = endFunction;
        client->end_param = endParameter;
        client->hdr.mode = mode;

        uint32_t sid = 0u;
        {
            std::lock_guard<std::mutex> lock(g_rpc_mutex);
            auto &state = g_rpc_clients[clientPtr];
            state.busy = true;
            state.last_rpc = rpcNum;
            sid = state.sid;
            if (sid != 0u)
            {
                const auto serverIt = g_rpc_servers.find(sid);
                if (serverIt != g_rpc_servers.end() &&
                    serverIt->second.sd_ptr != 0u)
                {
                    client->server = serverIt->second.sd_ptr;
                }
            }
        }

        const uint32_t serverPtr = client->server;
        auto *server = serverPtr
                           ? reinterpret_cast<t_SifRpcServerData *>(getMemPtr(rdram, serverPtr))
                           : nullptr;
        if (server)
        {
            server->client = clientPtr;
            server->pkt_addr = client->hdr.pkt_addr;
            server->rpc_number = rpcNum;
            server->size = static_cast<int>(sendSize);
            server->recvbuf = receiveBuffer;
            server->rsize = static_cast<int>(receiveSize);
            server->rmode = ((mode & kSifRpcModeNowait) && endFunction == 0u) ? 0 : 1;
            server->rid = 0;

            if (server->buf != 0u && sendBuf != 0u && sendSize != 0u)
            {
                rpcCopyToRdram(rdram, server->buf, sendBuf, sendSize);
            }
        }

        uint32_t resultPointer = 0u;
        bool handled = false;
        bool handledByIop = false;
        bool serverDispatched = false;
        bool callbackCompleted = false;
        bool copiedFallback = false;
        bool zeroedFallback = false;
        ps2x::iop::RpcResult iopResult{};

        const auto completionSemaphore = [&]()
        {
            uint32_t semaphore = static_cast<uint32_t>(client->hdr.sema_id);
            if (semaphore == 0xFFFFFFFFu || semaphore == 0u)
            {
                semaphore = endParameter;
            }
            return semaphore;
        };

        {
            ps2x::iop::RpcRequest request{};
            request.clientAddress = clientPtr;
            request.serverAddress = serverPtr;
            request.serverFunction = server ? server->func : 0u;
            request.serverBuffer = server ? server->buf : 0u;
            request.sid = sid;
            request.function = rpcNum;
            request.mode = mode;
            request.send = {sendBuf, sendSize};
            request.receive = {receiveBuffer, receiveSize};
            request.endFunction = endFunction;
            request.endParameter = endParameter;

            iopResult = PS2IopTransport::handleRpc(runtime, rdram, ctx, request);
            handled = iopResult.handled;
            handledByIop = iopResult.handled;
            resultPointer = iopResult.resultAddress;

            if (iopResult.signalNowaitCompletion &&
                (mode & kSifRpcModeNowait) != 0u)
            {
                (void)signalRpcCompletionSema(completionSemaphore());
            }
            if (iopResult.signalCompletion)
            {
                (void)signalRpcCompletionSema(completionSemaphore());
            }
        }

        if (server && server->func != 0u && iopResult.serverDispatchPolicy != ps2x::iop::ServerDispatchPolicy::Suppress)
        {
            uint32_t serverResult = 0u;
            serverDispatched = rpcInvokeFunction(rdram,
                                                 ctx,
                                                 runtime,
                                                 server->func,
                                                 rpcNum,
                                                 server->buf,
                                                 sendSize,
                                                 0u,
                                                 &serverResult);
            if (serverDispatched)
            {
                handled = true;
                resultPointer = serverResult;
                if (resultPointer == 0u && server->buf != 0u)
                {
                    resultPointer = server->buf;
                }
                if (resultPointer == 0u)
                {
                    resultPointer = receiveBuffer;
                }
            }
        }

        if (receiveBuffer != 0u && receiveSize != 0u)
        {
            if (handled && resultPointer != 0u && resultPointer != receiveBuffer)
            {
                rpcCopyToRdram(rdram,
                               receiveBuffer,
                               resultPointer,
                               receiveSize);
            }
            else if (!handled && sendBuf != 0u && sendSize != 0u && sendBuf != receiveBuffer)
            {
                const uint32_t copySize = std::min(sendSize, receiveSize);
                rpcCopyToRdram(rdram, receiveBuffer, sendBuf, copySize);
                copiedFallback = true;
            }
            else if (!handled)
            {
                rpcZeroRdram(rdram, receiveBuffer, receiveSize);
                zeroedFallback = true;
            }
        }

        if (endFunction != 0u)
        {
            if (iopResult.callbackPolicy == ps2x::iop::CallbackPolicy::Suppress)
            {
                callbackCompleted = true;
            }
            else
            {
                callbackCompleted = rpcInvokeFunction(rdram,
                                                      ctx,
                                                      runtime,
                                                      endFunction,
                                                      endParameter,
                                                      0u,
                                                      0u,
                                                      0u,
                                                      nullptr);
                if (!callbackCompleted && endFunction >= 0x10000u)
                {
                    const uint32_t normalizedEndFunction = endFunction - 0x10000u;
                    if (runtime->hasFunction(normalizedEndFunction))
                    {
                        callbackCompleted = rpcInvokeFunction(
                            rdram,
                            ctx,
                            runtime,
                            normalizedEndFunction,
                            endParameter,
                            0u,
                            0u,
                            0u,
                            nullptr);
                    }
                }
            }

            if (!callbackCompleted)
            {
                const bool signaled = signalRpcCompletionSema(completionSemaphore());
                static uint32_t unresolvedCallbackWarnings = 0u;
                if (unresolvedCallbackWarnings < 32u)
                {
                    std::cerr
                        << "[SifCallRpc] unresolved end callback endFunc=0x"
                        << std::hex << endFunction
                        << " semaId=0x" << completionSemaphore()
                        << " fallbackSignal=" << std::dec
                        << (signaled ? 1 : 0) << std::endl;
                    ++unresolvedCallbackWarnings;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_rpc_mutex);
            g_rpc_clients[clientPtr].busy = false;
        }

        SifRpcDebugEvent event = makeRpcDebugEvent("CallRpc", ctx);
        event.clientPtr = clientPtr;
        event.serverPtr = serverPtr;
        event.sid = sid;
        event.rpcNum = rpcNum;
        event.mode = mode;
        event.sendBuf = sendBuf;
        event.sendSize = sendSize;
        event.recvBuf = receiveBuffer;
        event.recvSize = receiveSize;
        event.resultPtr = resultPointer;
        event.endFunc = endFunction;
        event.endParam = endParameter;
        event.semaId = static_cast<uint32_t>(client->hdr.sema_id);
        event.flags =
            ((mode & kSifRpcModeNowait) ? kSifRpcDebugFlagNowait : 0u) |
            (handledByIop ? kSifRpcDebugFlagHandledByHle : 0u) |
            (callbackCompleted ? kSifRpcDebugFlagCallback : 0u) |
            (serverDispatched ? kSifRpcDebugFlagServerDispatch : 0u) |
            (!handled ? kSifRpcDebugFlagUnhandled : 0u) |
            (copiedFallback ? kSifRpcDebugFlagFallbackCopy : 0u) |
            (zeroedFallback ? kSifRpcDebugFlagFallbackZero : 0u);
        fillRpcDebugPreview(rdram,
                            sendBuf,
                            sendSize,
                            event.sendPreview,
                            event.sendPreviewSize);
        fillRpcDebugPreview(rdram,
                            receiveBuffer,
                            receiveSize,
                            event.recvPreview,
                            event.recvPreviewSize);
        event.result = 0;

#if PS2X_ENABLE_IOP_RPC_TRACE
        if ((event.flags & kSifRpcDebugFlagUnhandled) != 0u)
        {
            logUnhandledRpcTrace(event);
        }
#endif
        pushSifRpcDebugEvent(event);

        setReturnS32(ctx, 0);
    }

    void SifRegisterRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t sdPtr = getRegU32(ctx, 4);
        uint32_t sid = getRegU32(ctx, 5);
        uint32_t func = getRegU32(ctx, 6);
        uint32_t buf = getRegU32(ctx, 7);
        // stack args: cfunc, cbuf, qd...
        uint32_t sp = getRegU32(ctx, 29);
        uint32_t cfunc = 0;
        uint32_t cbuf = 0;
        uint32_t qd = 0;
        readStackU32(rdram, sp, 0x10, cfunc);
        readStackU32(rdram, sp, 0x14, cbuf);
        readStackU32(rdram, sp, 0x18, qd);

        t_SifRpcServerData *sd = reinterpret_cast<t_SifRpcServerData *>(getMemPtr(rdram, sdPtr));
        if (!sd)
        {
            SifRpcDebugEvent event = makeRpcDebugEvent("RegisterRpc", ctx);
            event.serverPtr = sdPtr;
            event.sid = sid;
            event.sendBuf = buf;
            event.recvBuf = cbuf;
            event.resultPtr = qd;
            event.endFunc = cfunc;
            event.flags = kSifRpcDebugFlagMissingClient;
            event.result = -1;
            pushSifRpcDebugEvent(event);
            setReturnS32(ctx, -1);
            return;
        }

        sd->sid = static_cast<int>(sid);
        sd->func = func;
        sd->buf = buf;
        sd->size = 0;
        sd->cfunc = cfunc;
        sd->cbuf = cbuf;
        sd->size2 = 0;
        sd->client = 0;
        sd->pkt_addr = 0;
        sd->rpc_number = 0;
        sd->recvbuf = 0;
        sd->rsize = 0;
        sd->rmode = 0;
        sd->rid = 0;
        sd->base = qd;
        sd->link = 0;
        sd->next = 0;

        {
            std::lock_guard<std::mutex> lock(g_rpc_mutex);

            if (qd)
            {
                t_SifRpcDataQueue *queue = reinterpret_cast<t_SifRpcDataQueue *>(getMemPtr(rdram, qd));
                if (queue)
                {
                    if (!queue->link)
                    {
                        queue->link = sdPtr;
                    }
                    else
                    {
                        uint32_t curPtr = queue->link;
                        for (int guard = 0; guard < 1024 && curPtr; ++guard)
                        {
                            t_SifRpcServerData *cur = reinterpret_cast<t_SifRpcServerData *>(getMemPtr(rdram, curPtr));
                            if (!cur)
                                break;
                            if (!cur->link)
                            {
                                cur->link = sdPtr;
                                break;
                            }
                            if (cur->link == sdPtr)
                                break;
                            curPtr = cur->link;
                        }
                    }
                }
            }

            g_rpc_servers[sid] = {sid, sdPtr};
            for (auto &entry : g_rpc_clients)
            {
                if (entry.second.sid == sid)
                {
                    t_SifRpcClientData *cd = reinterpret_cast<t_SifRpcClientData *>(getMemPtr(rdram, entry.first));
                    if (cd)
                    {
                        cd->server = sdPtr;
                        cd->buf = sd->buf;
                        cd->cbuf = sd->cbuf;
                    }
                }
            }
        }

        RUNTIME_LOG("[SifRegisterRpc] sid=0x" << std::hex << sid << " sd=0x" << sdPtr << std::dec);
        SifRpcDebugEvent event = makeRpcDebugEvent("RegisterRpc", ctx);
        event.serverPtr = sdPtr;
        event.sid = sid;
        event.sendBuf = buf;
        event.recvBuf = cbuf;
        event.resultPtr = qd;
        event.endFunc = cfunc;
        event.result = 0;
        pushSifRpcDebugEvent(event);
        setReturnS32(ctx, 0);
    }

    void SifCheckStatRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t clientPtr = getRegU32(ctx, 4);
        std::lock_guard<std::mutex> lock(g_rpc_mutex);
        auto it = g_rpc_clients.find(clientPtr);
        if (it == g_rpc_clients.end())
        {
            setReturnS32(ctx, 0);
            return;
        }
        setReturnS32(ctx, it->second.busy ? 1 : 0);
    }

    void SifSetRpcQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t qdPtr = getRegU32(ctx, 4);
        int threadId = static_cast<int>(getRegU32(ctx, 5));

        t_SifRpcDataQueue *qd = reinterpret_cast<t_SifRpcDataQueue *>(getMemPtr(rdram, qdPtr));
        if (!qd)
        {
            setReturnS32(ctx, -1);
            return;
        }

        qd->thread_id = threadId;
        qd->active = 0;
        qd->link = 0;
        qd->start = 0;
        qd->end = 0;
        qd->next = 0;

        {
            std::lock_guard<std::mutex> lock(g_rpc_mutex);
            if (!g_rpc_active_queue)
            {
                g_rpc_active_queue = qdPtr;
            }
            else
            {
                uint32_t curPtr = g_rpc_active_queue;
                for (int guard = 0; guard < 1024 && curPtr; ++guard)
                {
                    if (curPtr == qdPtr)
                        break;
                    t_SifRpcDataQueue *cur = reinterpret_cast<t_SifRpcDataQueue *>(getMemPtr(rdram, curPtr));
                    if (!cur)
                        break;
                    if (!cur->next)
                    {
                        cur->next = qdPtr;
                        break;
                    }
                    curPtr = cur->next;
                }
            }
        }

        setReturnS32(ctx, 0);
    }

    void SifRemoveRpcQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t qdPtr = getRegU32(ctx, 4);
        if (!qdPtr)
        {
            setReturnU32(ctx, 0);
            return;
        }

        std::lock_guard<std::mutex> lock(g_rpc_mutex);
        if (!g_rpc_active_queue)
        {
            setReturnU32(ctx, 0);
            return;
        }

        if (g_rpc_active_queue == qdPtr)
        {
            t_SifRpcDataQueue *qd = reinterpret_cast<t_SifRpcDataQueue *>(getMemPtr(rdram, qdPtr));
            g_rpc_active_queue = qd ? qd->next : 0;
            setReturnU32(ctx, qdPtr);
            return;
        }

        uint32_t curPtr = g_rpc_active_queue;
        for (int guard = 0; guard < 1024 && curPtr; ++guard)
        {
            t_SifRpcDataQueue *cur = reinterpret_cast<t_SifRpcDataQueue *>(getMemPtr(rdram, curPtr));
            if (!cur)
                break;
            if (cur->next == qdPtr)
            {
                t_SifRpcDataQueue *rem = reinterpret_cast<t_SifRpcDataQueue *>(getMemPtr(rdram, qdPtr));
                cur->next = rem ? rem->next : 0;
                setReturnU32(ctx, qdPtr);
                return;
            }
            curPtr = cur->next;
        }

        setReturnU32(ctx, 0);
    }

    void SifRemoveRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t sdPtr = getRegU32(ctx, 4);
        uint32_t qdPtr = getRegU32(ctx, 5);

        t_SifRpcDataQueue *qd = reinterpret_cast<t_SifRpcDataQueue *>(getMemPtr(rdram, qdPtr));
        if (!qd || !sdPtr)
        {
            setReturnU32(ctx, 0);
            return;
        }

        std::lock_guard<std::mutex> lock(g_rpc_mutex);

        if (qd->link == sdPtr)
        {
            t_SifRpcServerData *sd = reinterpret_cast<t_SifRpcServerData *>(getMemPtr(rdram, sdPtr));
            qd->link = sd ? sd->link : 0;
            if (sd)
                sd->link = 0;
            setReturnU32(ctx, sdPtr);
            return;
        }

        uint32_t curPtr = qd->link;
        for (int guard = 0; guard < 1024 && curPtr; ++guard)
        {
            t_SifRpcServerData *cur = reinterpret_cast<t_SifRpcServerData *>(getMemPtr(rdram, curPtr));
            if (!cur)
                break;
            if (cur->link == sdPtr)
            {
                t_SifRpcServerData *sd = reinterpret_cast<t_SifRpcServerData *>(getMemPtr(rdram, sdPtr));
                cur->link = sd ? sd->link : 0;
                if (sd)
                    sd->link = 0;
                setReturnU32(ctx, sdPtr);
                return;
            }
            curPtr = cur->link;
        }

        setReturnU32(ctx, 0);
    }

    void sceSifCallRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        SifCallRpc(rdram, ctx, runtime);
    }

    void sceSifSendCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t cid = getRegU32(ctx, 4);
        uint32_t packetAddr = getRegU32(ctx, 5);
        uint32_t packetSize = getRegU32(ctx, 6);
        uint32_t srcExtra = getRegU32(ctx, 7);

        uint32_t sp = getRegU32(ctx, 29);
        uint32_t destExtra = 0;
        uint32_t sizeExtra = 0;
        readStackU32(rdram, sp, 0x10, destExtra);
        readStackU32(rdram, sp, 0x14, sizeExtra);

        if (sizeExtra > 0 && srcExtra && destExtra)
        {
            rpcCopyToRdram(rdram, destExtra, srcExtra, sizeExtra);
        }

        static int logCount = 0;
        if (logCount < 5)
        {
            RUNTIME_LOG("[sceSifSendCmd] cid=0x" << std::hex << cid
                                                 << " packet=0x" << packetAddr
                                                 << " psize=0x" << packetSize
                                                 << " extra=0x" << destExtra << std::dec << std::endl);
            ++logCount;
        }

        // Return non-zero on success.
        setReturnS32(ctx, 1);
    }

    void sceRpcGetPacket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t queuePtr = getRegU32(ctx, 4);
        setReturnS32(ctx, static_cast<int32_t>(queuePtr));
    }
}
