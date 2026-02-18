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
    if (!g_rpc_initialized)
    {
        g_rpc_servers.clear();
        g_rpc_clients.clear();
        g_rpc_next_id = 1;
        g_rpc_packet_index = 0;
        g_rpc_server_index = 0;
        g_rpc_active_queue = 0;
        {
            std::lock_guard<std::mutex> dtxLock(g_dtx_rpc_mutex);
            g_dtx_remote_by_id.clear();
            g_dtx_next_urpc_obj = kDtxUrpcObjBase;
        }
        g_rpc_initialized = true;
        std::cout << "[SifInitRpc] Initialized" << std::endl;
    }
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

    setReturnS32(ctx, 0);
}

void SifCallRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t clientPtr = getRegU32(ctx, 4);
    uint32_t rpcNum = getRegU32(ctx, 5);
    uint32_t mode = getRegU32(ctx, 6);
    uint32_t sendBuf = getRegU32(ctx, 7);
    uint32_t sendSize = 0;
    uint32_t recvBuf = 0;
    uint32_t recvSize = 0;
    uint32_t endFunc = 0;
    uint32_t endParam = 0;

    // EE-side calls use extended arg registers:
    // a0-a3 => r4-r7, arg5-arg8 => r8-r11, arg9 => stack + 0x0.
    // Keep O32 stack-layout fallback for compatibility with other call sites.
    uint32_t sp = getRegU32(ctx, 29);
    sendSize = getRegU32(ctx, 8);
    recvBuf = getRegU32(ctx, 9);
    recvSize = getRegU32(ctx, 10);
    endFunc = getRegU32(ctx, 11);
    (void)readStackU32(rdram, sp, 0x0, endParam);

    if (sendSize == 0 && recvBuf == 0 && recvSize == 0 && endFunc == 0)
    {
        readStackU32(rdram, sp, 0x10, sendSize);
        readStackU32(rdram, sp, 0x14, recvBuf);
        readStackU32(rdram, sp, 0x18, recvSize);
        readStackU32(rdram, sp, 0x1C, endFunc);
        readStackU32(rdram, sp, 0x20, endParam);
    }

    t_SifRpcClientData *client = reinterpret_cast<t_SifRpcClientData *>(getMemPtr(rdram, clientPtr));

    if (!client)
    {
        setReturnS32(ctx, -1);
        return;
    }

    client->command = rpcNum;
    client->end_function = endFunc;
    client->end_param = endParam;
    client->hdr.mode = mode;

    {
        std::lock_guard<std::mutex> lock(g_rpc_mutex);
        g_rpc_clients[clientPtr].busy = true;
        g_rpc_clients[clientPtr].last_rpc = rpcNum;
        uint32_t sid = g_rpc_clients[clientPtr].sid;
        if (sid)
        {
            auto it = g_rpc_servers.find(sid);
            if (it != g_rpc_servers.end())
            {
                uint32_t mappedServer = it->second.sd_ptr;
                if (mappedServer && client->server != mappedServer)
                {
                    client->server = mappedServer;
                }
            }
        }
    }

    uint32_t sid = 0;
    {
        std::lock_guard<std::mutex> lock(g_rpc_mutex);
        auto it = g_rpc_clients.find(clientPtr);
        if (it != g_rpc_clients.end())
        {
            sid = it->second.sid;
        }
    }

    uint32_t serverPtr = client->server;
    t_SifRpcServerData *sd = serverPtr ? reinterpret_cast<t_SifRpcServerData *>(getMemPtr(rdram, serverPtr)) : nullptr;

    if (sd)
    {
        sd->client = clientPtr;
        sd->pkt_addr = client->hdr.pkt_addr;
        sd->rpc_number = rpcNum;
        sd->size = static_cast<int>(sendSize);
        sd->recvbuf = recvBuf;
        sd->rsize = static_cast<int>(recvSize);
        sd->rmode = ((mode & kSifRpcModeNowait) && endFunc == 0) ? 0 : 1;
        sd->rid = 0;
    }

    if (sd && sd->buf && sendBuf && sendSize > 0)
    {
        rpcCopyToRdram(rdram, sd->buf, sendBuf, sendSize);
    }

    uint32_t resultPtr = 0;
    bool handled = false;

    auto readRpcU32 = [&](uint32_t addr, uint32_t &out) -> bool
    {
        if (!addr)
        {
            return false;
        }
        const uint8_t *ptr = getConstMemPtr(rdram, addr);
        if (!ptr)
        {
            return false;
        }
        std::memcpy(&out, ptr, sizeof(out));
        return true;
    };

    auto writeRpcU32 = [&](uint32_t addr, uint32_t value) -> bool
    {
        if (!addr)
        {
            return false;
        }
        uint8_t *ptr = getMemPtr(rdram, addr);
        if (!ptr)
        {
            return false;
        }
        std::memcpy(ptr, &value, sizeof(value));
        return true;
    };

    const bool isDtxUrpc = (sid == kDtxRpcSid) && (rpcNum >= 0x400u) && (rpcNum < 0x500u);
    uint32_t dtxUrpcCommand = isDtxUrpc ? (rpcNum & 0xFFu) : 0u;
    uint32_t dtxUrpcFn = 0;
    uint32_t dtxUrpcObj = 0;
    uint32_t dtxUrpcSend0 = 0;
    bool dtxUrpcDispatchAttempted = false;
    bool dtxUrpcFallbackEmulated = false;
    bool dtxUrpcFallbackCreate34 = false;
    bool hasUrpcHandler = false;
    if (isDtxUrpc)
    {
        if (sendBuf && sendSize >= sizeof(uint32_t))
        {
            (void)readRpcU32(sendBuf, dtxUrpcSend0);
        }
        if (dtxUrpcCommand < 64u)
        {
            (void)readRpcU32(kDtxUrpcFnTableBase + (dtxUrpcCommand * 4u), dtxUrpcFn);
            (void)readRpcU32(kDtxUrpcObjTableBase + (dtxUrpcCommand * 4u), dtxUrpcObj);
        }
        hasUrpcHandler = (dtxUrpcCommand < 64u) && (dtxUrpcFn != 0u);
    }
    const bool allowServerDispatch = !isDtxUrpc || hasUrpcHandler;

    if (sd && sd->func && (sid != kDtxRpcSid || isDtxUrpc) && allowServerDispatch)
    {
        dtxUrpcDispatchAttempted = dtxUrpcDispatchAttempted || isDtxUrpc;
        handled = rpcInvokeFunction(rdram, ctx, runtime, sd->func, rpcNum, sd->buf, sendSize, 0, &resultPtr);
        if (handled && resultPtr == 0 && sd->buf)
        {
            resultPtr = sd->buf;
        }
        if (handled && resultPtr == 0 && recvBuf)
        {
            resultPtr = recvBuf;
        }
    }

    if (!handled && isDtxUrpc && sendBuf && sendSize > 0)
    {
        // Only dispatch through dtx_rpc_func when a URPC handler is registered in the table.
        // If the slot is empty, defer to the fallback emulation below.
        if (hasUrpcHandler)
        {
            dtxUrpcDispatchAttempted = true;
            handled = rpcInvokeFunction(rdram, ctx, runtime, 0x2fabc0u, rpcNum, sendBuf, sendSize, 0, &resultPtr);
            if (handled && resultPtr == 0)
            {
                resultPtr = sendBuf;
            }
        }
    }

    if (!handled && sid == kDtxRpcSid)
    {
        if (rpcNum == 2 && recvBuf && recvSize >= sizeof(uint32_t))
        {
            uint32_t dtxId = 0;
            if (sendBuf && sendSize >= sizeof(uint32_t))
            {
                (void)readRpcU32(sendBuf, dtxId);
            }

            uint32_t remoteHandle = 0;
            {
                std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                auto it = g_dtx_remote_by_id.find(dtxId);
                if (it != g_dtx_remote_by_id.end())
                {
                    remoteHandle = it->second;
                }
                if (!remoteHandle)
                {
                    remoteHandle = rpcAllocServerAddr(rdram);
                    if (!remoteHandle)
                    {
                        remoteHandle = rpcAllocPacketAddr(rdram);
                    }
                    if (!remoteHandle)
                    {
                        remoteHandle = kRpcServerPoolBase + ((dtxId & 0xFFu) * kRpcServerStride);
                    }
                    g_dtx_remote_by_id[dtxId] = remoteHandle;
                }
            }

            (void)writeRpcU32(recvBuf, remoteHandle);
            if (recvSize > sizeof(uint32_t))
            {
                rpcZeroRdram(rdram, recvBuf + sizeof(uint32_t), recvSize - sizeof(uint32_t));
            }
            handled = true;
            resultPtr = recvBuf;
        }
        else if (rpcNum == 3)
        {
            uint32_t remoteHandle = 0;
            if (sendBuf && sendSize >= sizeof(uint32_t) && readRpcU32(sendBuf, remoteHandle) && remoteHandle)
            {
                std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                for (auto it = g_dtx_remote_by_id.begin(); it != g_dtx_remote_by_id.end(); ++it)
                {
                    if (it->second == remoteHandle)
                    {
                        g_dtx_remote_by_id.erase(it);
                        break;
                    }
                }
            }
            if (recvBuf && recvSize > 0)
            {
                rpcZeroRdram(rdram, recvBuf, recvSize);
            }
            handled = true;
            resultPtr = recvBuf;
        }
        else if (rpcNum >= 0x400 && rpcNum < 0x500)
        {
            dtxUrpcFallbackEmulated = true;
            const uint32_t urpcCommand = rpcNum & 0xFFu;
            uint32_t outWords[4] = {1u, 0u, 0u, 0u};
            uint32_t outWordCount = 1u;

            auto readSendWord = [&](uint32_t index, uint32_t &out) -> bool
            {
                const uint64_t byteOffset = static_cast<uint64_t>(index) * sizeof(uint32_t);
                if (!sendBuf || sendSize < (byteOffset + sizeof(uint32_t)))
                {
                    return false;
                }
                return readRpcU32(sendBuf + static_cast<uint32_t>(byteOffset), out);
            };

            switch (urpcCommand)
            {
            case 32u: // SJRMT_RBF_CREATE
            case 33u: // SJRMT_MEM_CREATE
            case 34u: // SJRMT_UNI_CREATE
            {
                uint32_t arg0 = 0;
                uint32_t arg1 = 0;
                uint32_t arg2 = 0;
                (void)readSendWord(0u, arg0);
                (void)readSendWord(1u, arg1);
                (void)readSendWord(2u, arg2);

                uint32_t mode = 0;
                uint32_t wkAddr = 0;
                uint32_t wkSize = 0;
                if (urpcCommand == 34u)
                {
                    mode = arg0;
                    wkAddr = arg1;
                    wkSize = arg2;
                    dtxUrpcFallbackCreate34 = true;
                }
                else if (urpcCommand == 33u)
                {
                    wkAddr = arg0;
                    wkSize = arg1;
                }
                else
                {
                    wkAddr = arg0;
                    wkSize = (arg1 != 0u) ? arg1 : arg2;
                }

                wkSize = dtxNormalizeSjrmtCapacity(wkSize);

                std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                const uint32_t handle = dtxAllocUrpcHandleLocked();
                DtxSjrmtState state{};
                state.handle = handle;
                state.mode = mode;
                state.wkAddr = wkAddr;
                state.wkSize = wkSize;
                state.readPos = 0u;
                state.writePos = 0u;
                state.roomBytes = wkSize;
                state.dataBytes = 0u;
                state.uuid0 = 0x53524D54u; // "SRMT"
                state.uuid1 = handle;
                state.uuid2 = wkAddr;
                state.uuid3 = wkSize;
                g_dtx_sjrmt_by_handle[handle] = state;

                outWords[0] = handle ? handle : 1u;
                outWordCount = 1u;
                break;
            }
            case 35u: // SJRMT_DESTROY
            {
                uint32_t handle = 0;
                (void)readSendWord(0u, handle);
                std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                g_dtx_sjrmt_by_handle.erase(handle);
                outWords[0] = 1u;
                outWordCount = 1u;
                break;
            }
            case 36u: // SJRMT_GET_UUID
            {
                uint32_t handle = 0;
                (void)readSendWord(0u, handle);
                std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                auto it = g_dtx_sjrmt_by_handle.find(handle);
                if (it != g_dtx_sjrmt_by_handle.end())
                {
                    outWords[0] = it->second.uuid0;
                    outWords[1] = it->second.uuid1;
                    outWords[2] = it->second.uuid2;
                    outWords[3] = it->second.uuid3;
                }
                else
                {
                    outWords[0] = 0u;
                    outWords[1] = 0u;
                    outWords[2] = 0u;
                    outWords[3] = 0u;
                }
                outWordCount = 4u;
                break;
            }
            case 37u: // SJRMT_RESET
            {
                uint32_t handle = 0;
                (void)readSendWord(0u, handle);
                std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                auto it = g_dtx_sjrmt_by_handle.find(handle);
                if (it != g_dtx_sjrmt_by_handle.end())
                {
                    const uint32_t cap = (it->second.wkSize == 0u) ? 0x4000u : it->second.wkSize;
                    it->second.readPos = 0u;
                    it->second.writePos = 0u;
                    it->second.roomBytes = cap;
                    it->second.dataBytes = 0u;
                }
                outWords[0] = 1u;
                outWordCount = 1u;
                break;
            }
            case 38u: // SJRMT_GET_CHUNK
            {
                uint32_t handle = 0;
                uint32_t streamId = 0;
                uint32_t nbyte = 0;
                (void)readSendWord(0u, handle);
                (void)readSendWord(1u, streamId);
                (void)readSendWord(2u, nbyte);

                uint32_t ptr = 0u;
                uint32_t len = 0u;

                std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                auto it = g_dtx_sjrmt_by_handle.find(handle);
                if (it != g_dtx_sjrmt_by_handle.end())
                {
                    DtxSjrmtState &state = it->second;
                    const uint32_t cap = (state.wkSize == 0u) ? 0x4000u : state.wkSize;

                    if (streamId == 0u)
                    {
                        len = std::min(nbyte, state.roomBytes);
                        ptr = state.wkAddr + (cap ? (state.writePos % cap) : 0u);
                        if (cap != 0u)
                        {
                            state.writePos = (state.writePos + len) % cap;
                        }
                        state.roomBytes -= len;
                    }
                    else if (streamId == 1u)
                    {
                        len = std::min(nbyte, state.dataBytes);
                        ptr = state.wkAddr + (cap ? (state.readPos % cap) : 0u);
                        if (cap != 0u)
                        {
                            state.readPos = (state.readPos + len) % cap;
                        }
                        state.dataBytes -= len;
                    }
                }

                outWords[0] = ptr;
                outWords[1] = len;
                outWordCount = 2u;
                break;
            }
            case 39u: // SJRMT_UNGET_CHUNK
            {
                uint32_t handle = 0;
                uint32_t streamId = 0;
                uint32_t len = 0;
                (void)readSendWord(0u, handle);
                (void)readSendWord(1u, streamId);
                (void)readSendWord(3u, len);

                std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                auto it = g_dtx_sjrmt_by_handle.find(handle);
                if (it != g_dtx_sjrmt_by_handle.end())
                {
                    DtxSjrmtState &state = it->second;
                    const uint32_t cap = (state.wkSize == 0u) ? 0x4000u : state.wkSize;
                    if (streamId == 0u)
                    {
                        const uint32_t delta = (cap == 0u) ? 0u : (len % cap);
                        if (cap != 0u)
                        {
                            state.writePos = (state.writePos + cap - delta) % cap;
                        }
                        state.roomBytes = std::min(cap, state.roomBytes + len);
                    }
                    else if (streamId == 1u)
                    {
                        const uint32_t delta = (cap == 0u) ? 0u : (len % cap);
                        if (cap != 0u)
                        {
                            state.readPos = (state.readPos + cap - delta) % cap;
                        }
                        state.dataBytes = std::min(cap, state.dataBytes + len);
                    }
                }

                outWords[0] = 1u;
                outWordCount = 1u;
                break;
            }
            case 40u: // SJRMT_PUT_CHUNK
            {
                uint32_t handle = 0;
                uint32_t streamId = 0;
                uint32_t len = 0;
                (void)readSendWord(0u, handle);
                (void)readSendWord(1u, streamId);
                (void)readSendWord(3u, len);

                std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                auto it = g_dtx_sjrmt_by_handle.find(handle);
                if (it != g_dtx_sjrmt_by_handle.end())
                {
                    DtxSjrmtState &state = it->second;
                    const uint32_t cap = (state.wkSize == 0u) ? 0x4000u : state.wkSize;
                    if (streamId == 0u)
                    {
                        state.roomBytes = std::min(cap, state.roomBytes + len);
                    }
                    else if (streamId == 1u)
                    {
                        state.dataBytes = std::min(cap, state.dataBytes + len);
                    }
                }

                outWords[0] = 1u;
                outWordCount = 1u;
                break;
            }
            case 41u: // SJRMT_GET_NUM_DATA
            {
                uint32_t handle = 0;
                uint32_t streamId = 0;
                (void)readSendWord(0u, handle);
                (void)readSendWord(1u, streamId);

                std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                auto it = g_dtx_sjrmt_by_handle.find(handle);
                if (it != g_dtx_sjrmt_by_handle.end())
                {
                    outWords[0] = (streamId == 0u) ? it->second.roomBytes : it->second.dataBytes;
                }
                else
                {
                    outWords[0] = 0u;
                }
                outWordCount = 1u;
                break;
            }
            case 42u: // SJRMT_IS_GET_CHUNK
            {
                uint32_t handle = 0;
                uint32_t streamId = 0;
                uint32_t nbyte = 0;
                (void)readSendWord(0u, handle);
                (void)readSendWord(1u, streamId);
                (void)readSendWord(2u, nbyte);

                uint32_t available = 0u;
                std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                auto it = g_dtx_sjrmt_by_handle.find(handle);
                if (it != g_dtx_sjrmt_by_handle.end())
                {
                    available = (streamId == 0u) ? it->second.roomBytes : it->second.dataBytes;
                }
                outWords[0] = (available >= nbyte) ? 1u : 0u;
                outWords[1] = available;
                outWordCount = 2u;
                break;
            }
            case 43u: // SJRMT_INIT
            case 44u: // SJRMT_FINISH
            {
                outWords[0] = 1u;
                outWordCount = 1u;
                break;
            }
            default:
            {
                uint32_t urpcRet = 1u;
                if (sendBuf && sendSize >= sizeof(uint32_t))
                {
                    (void)readRpcU32(sendBuf, urpcRet);
                }
                if (urpcCommand == 0u)
                {
                    std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                    urpcRet = dtxAllocUrpcHandleLocked();
                }
                if (urpcRet == 0u)
                {
                    urpcRet = 1u;
                }
                outWords[0] = urpcRet;
                outWordCount = 1u;
                break;
            }
            }

            if (recvBuf && recvSize > 0u)
            {
                const uint32_t recvWordCapacity = static_cast<uint32_t>(recvSize / sizeof(uint32_t));
                const uint32_t wordsToWrite = std::min(outWordCount, recvWordCapacity);
                for (uint32_t i = 0; i < wordsToWrite; ++i)
                {
                    (void)writeRpcU32(recvBuf + (i * sizeof(uint32_t)), outWords[i]);
                }

                // SJRMT_IsGetChunk callers read rbuf[1] even when nout==1.
                if (urpcCommand == 42u && outWordCount > 1u)
                {
                    (void)writeRpcU32(recvBuf + sizeof(uint32_t), outWords[1]);
                }

                if (recvSize > (wordsToWrite * sizeof(uint32_t)))
                {
                    rpcZeroRdram(rdram, recvBuf + (wordsToWrite * sizeof(uint32_t)),
                                 recvSize - (wordsToWrite * sizeof(uint32_t)));
                }
            }

            handled = true;
            resultPtr = recvBuf;
        }
    }

    auto signalRpcCompletionSema = [&](uint32_t semaId) -> bool
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
        {
            std::lock_guard<std::mutex> lock(sema->m);
            if (!sema->deleted && sema->count < sema->maxCount)
            {
                sema->count++;
                signaled = true;
            }
        }

        if (signaled)
        {
            sema->cv.notify_one();
        }
        return signaled;
    };

    if (sid == 1u && (rpcNum == 0x12u || rpcNum == 0x13u))
    {
        uint32_t responseWord = 1u;
        if (rpcNum == 0x13u)
        {
            static uint32_t sdrStateBlobAddr = 0u;
            if (sdrStateBlobAddr == 0u)
            {
                sdrStateBlobAddr = rpcAllocPacketAddr(rdram);
                if (sdrStateBlobAddr == 0u)
                {
                    sdrStateBlobAddr = kRpcPacketPoolBase;
                }
            }

            rpcZeroRdram(rdram, sdrStateBlobAddr, 64u);
            (void)writeRpcU32(sdrStateBlobAddr + 0u, 1u);
            responseWord = sdrStateBlobAddr;
        }

        if (recvBuf && recvSize >= sizeof(uint32_t))
        {
            (void)writeRpcU32(recvBuf, responseWord);
            if (recvSize > sizeof(uint32_t))
            {
                rpcZeroRdram(rdram, recvBuf + sizeof(uint32_t), recvSize - sizeof(uint32_t));
            }
            resultPtr = recvBuf;
        }

        handled = true;

        if ((mode & kSifRpcModeNowait) != 0u)
        {
            (void)signalRpcCompletionSema(endParam);
        }
    }

    if (recvBuf && recvSize > 0)
    {
        if (handled && resultPtr)
        {
            rpcCopyToRdram(rdram, recvBuf, resultPtr, recvSize);
        }
        else if (!handled && sendBuf && sendSize > 0)
        {
            size_t copySize = (sendSize < recvSize) ? sendSize : recvSize;
            rpcCopyToRdram(rdram, recvBuf, sendBuf, copySize);
        }
        else if (!handled)
        {
            rpcZeroRdram(rdram, recvBuf, recvSize);
        }
    }

    if (isDtxUrpc)
    {
        static int dtxUrpcLogCount = 0;
        if (dtxUrpcLogCount < 64)
        {
            uint32_t dtxUrpcRecv0 = 0;
            if (recvBuf && recvSize >= sizeof(uint32_t))
            {
                (void)readRpcU32(recvBuf, dtxUrpcRecv0);
            }
            std::cout << "[SifCallRpc:DTX] rpcNum=0x" << std::hex << rpcNum
                      << " cmd=0x" << dtxUrpcCommand
                      << " fn=0x" << dtxUrpcFn
                      << " obj=0x" << dtxUrpcObj
                      << " send0=0x" << dtxUrpcSend0
                      << " recv0=0x" << dtxUrpcRecv0
                      << " resultPtr=0x" << resultPtr
                      << " handled=" << std::dec << (handled ? 1 : 0)
                      << " dispatch=" << (dtxUrpcDispatchAttempted ? 1 : 0)
                      << " emu=" << (dtxUrpcFallbackEmulated ? 1 : 0)
                      << " emu34=" << (dtxUrpcFallbackCreate34 ? 1 : 0)
                      << std::endl;
            ++dtxUrpcLogCount;
        }
    }

    if (endFunc)
    {
        bool callbackInvoked = rpcInvokeFunction(rdram, ctx, runtime, endFunc, endParam, 0, 0, 0, nullptr);

        // Some generated callsites may pass 0x2fac20/0x2fac30 instead of
        // 0x2eac20/0x2eac30 for sound-driver RPC callbacks.
        if (!callbackInvoked && (endFunc == 0x2fac20u || endFunc == 0x2fac30u))
        {
            const uint32_t normalizedEndFunc = endFunc - 0x10000u;
            callbackInvoked = rpcInvokeFunction(rdram, ctx, runtime, normalizedEndFunc, endParam, 0, 0, 0, nullptr);
        }

        // Guard against callback dispatch gaps that would leak the semaphore
        // acquired in SdrSendReq/SdrGetStateSend.
        const bool isSoundRpcCallback =
            (endFunc == 0x2eac20u || endFunc == 0x2eac30u ||
             endFunc == 0x2fac20u || endFunc == 0x2fac30u);
        if (isSoundRpcCallback)
        {
            (void)signalRpcCompletionSema(endParam);
            if (rdram && (endFunc == 0x2eac30u || endFunc == 0x2fac30u))
            {
                constexpr uint32_t kSndBusyFlagAddr = 0x01E212C8u;
                if (uint32_t *busy = reinterpret_cast<uint32_t *>(getMemPtr(rdram, kSndBusyFlagAddr)))
                {
                    *busy = 0u;
                }
            }
        }

        if (!callbackInvoked)
        {
            const bool fallbackSignaledSema = signalRpcCompletionSema(endParam);

            static uint32_t unresolvedEndFuncWarnCount = 0;
            if (unresolvedEndFuncWarnCount < 32u)
            {
                std::cerr << "[SifCallRpc] unresolved end callback endFunc=0x" << std::hex << endFunc
                          << " endParam=0x" << endParam
                          << " fallbackSignal=" << std::dec << (fallbackSignaledSema ? 1 : 0)
                          << std::endl;
                ++unresolvedEndFuncWarnCount;
            }
        }
    }

    static int logCount = 0;
    if (logCount < 10)
    {
        std::cout << "[SifCallRpc] client=0x" << std::hex << clientPtr
                  << " sid=0x" << sid
                  << " rpcNum=0x" << rpcNum
                  << " mode=0x" << mode
                  << " sendBuf=0x" << sendBuf
                  << " recvBuf=0x" << recvBuf
                  << " recvSize=0x" << recvSize
                  << " size=" << std::dec << sendSize << std::endl;
        ++logCount;
    }

    {
        std::lock_guard<std::mutex> lock(g_rpc_mutex);
        g_rpc_clients[clientPtr].busy = false;
    }

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

    {
        std::lock_guard<std::mutex> lock(g_rpc_mutex);
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

    std::cout << "[SifRegisterRpc] sid=0x" << std::hex << sid << " sd=0x" << sdPtr << std::dec << std::endl;
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
        std::cout << "[sceSifSendCmd] cid=0x" << std::hex << cid
                  << " packet=0x" << packetAddr
                  << " psize=0x" << packetSize
                  << " extra=0x" << destExtra << std::dec << std::endl;
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
