void calloc_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t count = getRegU32(ctx, 5); // $a1
    const uint32_t size = getRegU32(ctx, 6);  // $a2
    const uint32_t guestAddr = runtime ? runtime->guestCalloc(count, size) : 0u;
    setReturnU32(ctx, guestAddr);
}

void free_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t guestAddr = getRegU32(ctx, 5); // $a1
    if (runtime && guestAddr != 0u)
    {
        runtime->guestFree(guestAddr);
    }
}

void malloc_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t size = getRegU32(ctx, 5); // $a1
    const uint32_t guestAddr = runtime ? runtime->guestMalloc(size) : 0u;
    setReturnU32(ctx, guestAddr);
}

void malloc_trim_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}

void mbtowc_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mbtowc_r", rdram, ctx, runtime);
}

void printf_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
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
        std::cerr << "printf_r error: Invalid format string address provided: 0x" << std::hex << format_addr << std::dec << std::endl;
    }

    setReturnS32(ctx, ret);
}

void sceCdRI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceCdRI", rdram, ctx, runtime);
}

void sceCdRM(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceCdRM", rdram, ctx, runtime);
}

void sceFsDbChk(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceFsDbChk", rdram, ctx, runtime);
}

void sceFsIntrSigSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceFsIntrSigSema", rdram, ctx, runtime);
}

void sceFsSemExit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceFsSemExit", rdram, ctx, runtime);
}

void sceFsSemInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceFsSemInit", rdram, ctx, runtime);
}

void sceFsSigSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceFsSigSema", rdram, ctx, runtime);
}

void sceIDC(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceIDC", rdram, ctx, runtime);
}

void sceMpegFlush(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceMpegFlush", rdram, ctx, runtime);
}

void sceRpcFreePacket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceRpcFreePacket", rdram, ctx, runtime);
}

void sceRpcGetFPacket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceRpcGetFPacket", rdram, ctx, runtime);
}

void sceRpcGetFPacket2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceRpcGetFPacket2", rdram, ctx, runtime);
}

void sceSDC(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceSDC", rdram, ctx, runtime);
}

void sceSifCmdIntrHdlr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceSifCmdIntrHdlr", rdram, ctx, runtime);
}

void sceSifLoadModule(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceSifLoadModule", rdram, ctx, runtime);
}

void sceSifSendCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceSifSendCmd", rdram, ctx, runtime);
}

void sceVu0ecossin(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0ecossin", rdram, ctx, runtime);
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
    (void)rdram;
    (void)runtime;
    setReturnS32(ctx, 1);
}

void scePadEnterPressMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    (void)runtime;
    setReturnS32(ctx, 1);
}

void scePadExitPressMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    (void)runtime;
    setReturnS32(ctx, 1);
}

void scePadGetButtonMask(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    (void)runtime;
    // Report all buttons supported.
    setReturnS32(ctx, 0xFFFF);
}

void scePadGetDmaStr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    (void)runtime;
    // No DMA structure exposed in this minimal implementation.
    setReturnS32(ctx, 0);
}

void scePadGetFrameCount(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    (void)runtime;
    static std::atomic<uint32_t> frameCount{0};
    setReturnU32(ctx, frameCount++);
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
    (void)rdram;
    (void)runtime;
    // No actuators supported.
    setReturnS32(ctx, 0);
}

void scePadInfoComb(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    (void)runtime;
    // No combined modes reported.
    setReturnS32(ctx, 0);
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

    PadInputState state;
    bool useOverride = false;
    {
        std::lock_guard<std::mutex> lock(g_padOverrideMutex);
        if (g_padOverrideEnabled)
        {
            state = g_padOverrideState;
            useOverride = true;
        }
    }

    if (!useOverride)
    {
        applyGamepadState(state);
        applyKeyboardState(state, true);
    }

    fillPadStatus(data, state);

    if (padDebugEnabled())
    {
        static uint32_t logCounter = 0;
        if ((logCounter++ % 60u) == 0u)
        {
            std::cout << "[pad] buttons=0x" << std::hex << state.buttons << std::dec
                      << " lx=" << static_cast<int>(state.lx)
                      << " ly=" << static_cast<int>(state.ly)
                      << " rx=" << static_cast<int>(state.rx)
                      << " ry=" << static_cast<int>(state.ry)
                      << (useOverride ? " (override)" : "") << std::endl;
        }
    }

    setReturnS32(ctx, 1);
}

void scePadReqIntToStr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)runtime;
    const uint32_t state = getRegU32(ctx, 4);
    const uint32_t strAddr = getRegU32(ctx, 5);
    char *buf = reinterpret_cast<char *>(getMemPtr(rdram, strAddr));
    if (!buf)
    {
        setReturnS32(ctx, -1);
        return;
    }

    const char *text = (state == 0) ? "COMPLETE" : "BUSY";
    std::strncpy(buf, text, 31);
    buf[31] = '\0';
    setReturnS32(ctx, 0);
}

void scePadSetActAlign(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    (void)runtime;
    setReturnS32(ctx, 1);
}

void scePadSetActDirect(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    (void)runtime;
    setReturnS32(ctx, 1);
}

void scePadSetButtonInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    (void)runtime;
    setReturnS32(ctx, 1);
}

void scePadSetMainMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    (void)runtime;
    setReturnS32(ctx, 1);
}

void scePadSetReqState(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    (void)runtime;
    setReturnS32(ctx, 1);
}

void scePadSetVrefParam(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    (void)runtime;
    setReturnS32(ctx, 1);
}

void scePadSetWarningLevel(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    (void)runtime;
    setReturnS32(ctx, 0);
}

void scePadStateIntToStr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)runtime;
    const uint32_t state = getRegU32(ctx, 4);
    const uint32_t strAddr = getRegU32(ctx, 5);
    char *buf = reinterpret_cast<char *>(getMemPtr(rdram, strAddr));
    if (!buf)
    {
        setReturnS32(ctx, -1);
        return;
    }

    const char *text = "UNKNOWN";
    if (state == 6)
    {
        text = "STABLE";
    }
    else if (state == 1)
    {
        text = "FINDPAD";
    }
    else if (state == 0)
    {
        text = "DISCONNECTED";
    }

    std::strncpy(buf, text, 31);
    buf[31] = '\0';
    setReturnS32(ctx, 0);
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

    bool copyGuestByteRange(uint8_t *rdram, uint32_t dstAddr, uint32_t srcAddr, uint32_t sizeBytes)
    {
        if (!rdram || sizeBytes == 0u)
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
        if (!copyGuestByteRange(rdram, xfer.dest, xfer.src, sizeBytes))
        {
            ok = false;
            break;
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

    setReturnS32(ctx, static_cast<int32_t>(allocateSifDmaTransferId()));
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
