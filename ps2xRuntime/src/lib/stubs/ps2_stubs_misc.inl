void calloc_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t count = getRegU32(ctx, 5); // $a1
    const uint32_t size = getRegU32(ctx, 6);  // $a2
    const uint32_t guestAddr = runtime ? runtime->guestCalloc(count, size) : 0u;
    setReturnU32(ctx, guestAddr);
}

void ret0(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnU32(ctx, 0u);
    ctx->pc = getRegU32(ctx, 31);
}

void ret1(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnU32(ctx, 1u);
    ctx->pc = getRegU32(ctx, 31);
}

void reta0(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnU32(ctx, getRegU32(ctx, 4));
    ctx->pc = getRegU32(ctx, 31);
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
    const uint32_t wcAddr = getRegU32(ctx, 5);                 // $a1
    const uint32_t strAddr = getRegU32(ctx, 6);                // $a2
    const int32_t n = static_cast<int32_t>(getRegU32(ctx, 7)); // $a3
    if (n <= 0 || strAddr == 0u)
    {
        setReturnS32(ctx, 0);
        return;
    }

    const uint8_t *src = getConstMemPtr(rdram, strAddr);
    if (!src)
    {
        setReturnS32(ctx, -1);
        return;
    }

    const uint8_t ch = *src;
    if (wcAddr != 0u)
    {
        if (uint8_t *dst = getMemPtr(rdram, wcAddr))
        {
            const uint32_t out = static_cast<uint32_t>(ch);
            std::memcpy(dst, &out, sizeof(out));
        }
    }
    setReturnS32(ctx, (ch == 0u) ? 0 : 1);
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
    setReturnS32(ctx, 0);
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
    setReturnS32(ctx, 0);
}

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

void sceVu0ecossin(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0ecossin", rdram, ctx, runtime);
}

void abs(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int32_t value = static_cast<int32_t>(getRegU32(ctx, 4));
    if (value == std::numeric_limits<int32_t>::min())
    {
        setReturnS32(ctx, std::numeric_limits<int32_t>::max());
        return;
    }
    setReturnS32(ctx, value < 0 ? -value : value);
}

void atan(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    float in = ctx ? ctx->f[12] : 0.0f;
    if (in == 0.0f)
    {
        uint32_t raw = getRegU32(ctx, 4);
        std::memcpy(&in, &raw, sizeof(in));
    }
    const float out = std::atan(in);
    if (ctx)
    {
        ctx->f[0] = out;
    }

    uint32_t outRaw = 0u;
    std::memcpy(&outRaw, &out, sizeof(outRaw));
    setReturnU32(ctx, outRaw);
}

void close(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    ps2_syscalls::fioClose(rdram, ctx, runtime);
}

void DmaAddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnU32(ctx, getRegU32(ctx, 4));
}

void exit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (runtime)
    {
        runtime->requestStop();
    }
    setReturnS32(ctx, 0);
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
    setReturnS32(ctx, 1);
}

void iopGetArea(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnU32(ctx, kIopHeapBase);
}

void lseek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    ps2_syscalls::fioLseek(rdram, ctx, runtime);
}

void memchr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t srcAddr = getRegU32(ctx, 4);
    const uint8_t needle = static_cast<uint8_t>(getRegU32(ctx, 5) & 0xFFu);
    const uint32_t size = getRegU32(ctx, 6);

    for (uint32_t i = 0; i < size; ++i)
    {
        const uint8_t *src = getConstMemPtr(rdram, srcAddr + i);
        if (!src)
        {
            break;
        }
        if (*src == needle)
        {
            setReturnU32(ctx, srcAddr + i);
            return;
        }
    }

    setReturnU32(ctx, 0u);
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
    setReturnS32(ctx, 1);
}

void rand(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, std::rand() & 0x7FFF);
}

namespace
{
    std::mutex g_mcStateMutex;
    int32_t g_mcNextFd = 1;
    int32_t g_mcLastResult = 0;
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
    setReturnS32(ctx, 0);
}

void sceFsReset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}

static void writeU32AtGp(uint8_t *rdram, uint32_t gp, int32_t offset, uint32_t value)
{
    const uint32_t addr = gp + static_cast<uint32_t>(offset);
    if (uint8_t *p = getMemPtr(rdram, addr))
        *reinterpret_cast<uint32_t *>(p) = value;
}

void sceeFontInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t gp = getRegU32(ctx, 28);
    const uint32_t a0 = getRegU32(ctx, 4);
    const uint32_t a1 = getRegU32(ctx, 5);
    const uint32_t a2 = getRegU32(ctx, 6);
    const uint32_t a3 = getRegU32(ctx, 7);
    writeU32AtGp(rdram, gp, -0x7b60, a1);
    writeU32AtGp(rdram, gp, -0x7b5c, a2);
    writeU32AtGp(rdram, gp, -0x7b64, a0);
    writeU32AtGp(rdram, gp, -0x7c98, a3);
    writeU32AtGp(rdram, gp, -0x7b4c, 0x7f7f7f7f);
    writeU32AtGp(rdram, gp, -0x7b50, 0x3f800000);
    writeU32AtGp(rdram, gp, -0x7b54, 0x3f800000);
    writeU32AtGp(rdram, gp, -0x7b58, 0);

    if (runtime && a0 != 0u)
    {
        if ((a0 * 256u) + 64u <= PS2_GS_VRAM_SIZE)
        {
            uint32_t clutData[16];
            for (uint32_t i = 0; i < 16u; ++i)
            {
                uint8_t alpha = static_cast<uint8_t>((i * 0x80u) / 15u);
                clutData[i] = (i == 0)
                    ? 0x00000000u
                    : (0x80u | (0x80u << 8) | (0x80u << 16) | (static_cast<uint32_t>(alpha) << 24));
            }
            constexpr uint32_t kClutQwc = 4u;
            constexpr uint32_t kHeaderQwc = 6u;
            constexpr uint32_t kTotalQwc = kHeaderQwc + kClutQwc;
            uint32_t pktAddr = runtime->guestMalloc(kTotalQwc * 16u, 16u);
            if (pktAddr != 0u)
            {
                uint8_t *pkt = getMemPtr(rdram, pktAddr);
                if (pkt)
                {
                    uint64_t *q = reinterpret_cast<uint64_t *>(pkt);
                    const uint32_t dbp = a0 & 0x3FFFu;
                    constexpr uint8_t psm = 0u;
                    q[0] = (4ULL << 60) | (1ULL << 56) | 1ULL;
                    q[1] = 0x0E0E0E0E0E0E0E0EULL;
                    q[2] = (static_cast<uint64_t>(dbp) << 32) | (1ULL << 48) | (static_cast<uint64_t>(psm) << 56);
                    q[3] = 0x50ULL;
                    q[4] = 0ULL;
                    q[5] = 0x51ULL;
                    q[6] = 16ULL | (1ULL << 32);
                    q[7] = 0x52ULL;
                    q[8] = 0ULL;
                    q[9] = 0x53ULL;
                    q[10] = (2ULL << 58) | (kClutQwc & 0x7FFF) | (1ULL << 15);
                    q[11] = 0ULL;
                    std::memcpy(pkt + 12u * 8u, clutData, 64u);
                    constexpr uint32_t GIF_CHANNEL = 0x1000A000;
                    constexpr uint32_t CHCR_STR_MODE0 = 0x101u;
                    runtime->memory().writeIORegister(GIF_CHANNEL + 0x10u, pktAddr);
                    runtime->memory().writeIORegister(GIF_CHANNEL + 0x20u, kTotalQwc & 0xFFFFu);
                    runtime->memory().writeIORegister(GIF_CHANNEL + 0x00u, CHCR_STR_MODE0);
                    runtime->memory().processPendingTransfers();
                }
            }
        }
    }

    setReturnS32(ctx, static_cast<int32_t>(a0 + 4));
}

void sceeFontLoadFont(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static constexpr uint32_t kFontBase = 0x176148u;
    static constexpr uint32_t kFontEntrySz = 0x24u;

    const uint32_t fontDataAddr = getRegU32(ctx, 4);
    const int fontId = static_cast<int>(getRegU32(ctx, 5));
    const int tbp0 = static_cast<int>(getRegU32(ctx, 7));

    if (!fontDataAddr || !runtime)
    {
        setReturnS32(ctx, tbp0);
        return;
    }

    const uint8_t *fontPtr = getConstMemPtr(rdram, fontDataAddr);
    if (!fontPtr)
    {
        setReturnS32(ctx, tbp0);
        return;
    }

    int width = static_cast<int>(*reinterpret_cast<const uint32_t *>(fontPtr + 0x00u));
    int height = static_cast<int>(*reinterpret_cast<const uint32_t *>(fontPtr + 0x04u));
    uint32_t raw8 = *reinterpret_cast<const uint32_t *>(fontPtr + 0x08u);
    int fontDataSz = static_cast<int>(*reinterpret_cast<const uint32_t *>(fontPtr + 0x0cu));

    uint32_t pointsize = raw8;
    uint32_t fontOff = static_cast<uint32_t>(fontId * static_cast<int>(kFontEntrySz));
    if (raw8 & 0x40000000u)
    {
        pointsize = raw8 - 0x40000000u;
        if (uint8_t *p = getMemPtr(rdram, kFontBase + fontOff + 0x20u))
            *reinterpret_cast<uint32_t *>(p) = 1u;
    }
    else
    {
        if (uint8_t *p = getMemPtr(rdram, kFontBase + fontOff + 0x20u))
            *reinterpret_cast<uint32_t *>(p) = 0u;
    }

    int tw = (width >= 0) ? (width >> 6) : ((width + 0x3f) >> 6);
    int qwc = (fontDataSz >= 0) ? (fontDataSz >> 4) : ((fontDataSz + 0xf) >> 4);

    uint32_t glyphSrc = fontDataAddr + static_cast<uint32_t>(fontDataSz) + 0x10u;
    uint32_t glyphAlloc = runtime->guestMalloc(0x2010u, 0x40u);
    if (uint8_t *p = getMemPtr(rdram, kFontBase + fontOff))
        *reinterpret_cast<uint32_t *>(p) = glyphAlloc;

    if (glyphAlloc != 0u)
    {
        uint8_t *dst = getMemPtr(rdram, glyphAlloc);
        const uint8_t *src = getConstMemPtr(rdram, glyphSrc);
        if (dst && src)
            std::memcpy(dst, src, 0x2010u);
    }

    uint32_t isDoubleByte = 0;
    if (const uint8_t *p = getConstMemPtr(rdram, kFontBase + fontOff + 0x20u))
        isDoubleByte = *reinterpret_cast<const uint32_t *>(p);
    if (isDoubleByte == 0u)
    {
        uint32_t kernSrc = glyphSrc + 0x2010u;
        uint32_t kernAlloc = runtime->guestMalloc(0xc400u, 0x40u);
        if (glyphAlloc != 0u)
            *reinterpret_cast<uint32_t *>(getMemPtr(rdram, glyphAlloc + 0x2000u)) = kernAlloc;
        if (kernAlloc != 0u)
        {
            uint8_t *dst = getMemPtr(rdram, kernAlloc);
            const uint8_t *src = getConstMemPtr(rdram, kernSrc);
            if (dst && src)
                std::memcpy(dst, src, 0xc400u);
        }
    }

    auto writeFontField = [&](uint32_t off, uint32_t val)
    {
        if (uint8_t *p = getMemPtr(rdram, kFontBase + fontOff + off))
            *reinterpret_cast<uint32_t *>(p) = val;
    };
    writeFontField(0x18u, pointsize);
    writeFontField(0x08u, static_cast<uint32_t>(tbp0));
    writeFontField(0x0cu, static_cast<uint32_t>(tw));

    int logW = 0;
    for (int w = width; w != 1 && w != 0; w = static_cast<int>(static_cast<uint32_t>(w) >> 1))
        logW++;
    writeFontField(0x10u, static_cast<uint32_t>(logW));

    int logH = 0;
    for (int h = height; h != 1 && h != 0; h = static_cast<int>(static_cast<uint32_t>(h) >> 1))
        logH++;
    writeFontField(0x14u, static_cast<uint32_t>(logH));
    writeFontField(0x04u, 0u);
    writeFontField(0x1cu, getRegU32(ctx, 6));

    if (qwc > 0)
    {
        const uint32_t imageBytes = static_cast<uint32_t>(qwc) * 16u;
        const uint8_t psm = 20u;
        const uint32_t headerQwc = 12u;
        const uint32_t imageQwc = static_cast<uint32_t>(qwc);
        const uint32_t totalQwc = headerQwc + imageQwc;
        uint32_t pktAddr = runtime->guestMalloc(totalQwc * 16u, 16u);
        if (pktAddr != 0u)
        {
            uint8_t *pkt = getMemPtr(rdram, pktAddr);
            const uint8_t *imgSrc = getConstMemPtr(rdram, fontDataAddr + 0x10u);
            if (pkt && imgSrc)
            {
                uint64_t *q = reinterpret_cast<uint64_t *>(pkt);
                const uint32_t dbp = static_cast<uint32_t>(tbp0) & 0x3FFFu;
                const uint32_t dbw = static_cast<uint32_t>(tw > 0 ? tw : 1) & 0x3Fu;
                const uint32_t rrw = static_cast<uint32_t>(width > 0 ? width : 64);
                const uint32_t rrh = static_cast<uint32_t>(height > 0 ? height : 1);

                q[0] = (4ULL << 60) | (1ULL << 56) | 1ULL;
                q[1] = 0x0E0E0E0E0E0E0E0EULL;
                q[2] = (static_cast<uint64_t>(psm) << 24) | (1ULL << 16) |
                       (static_cast<uint64_t>(dbp) << 32) | (static_cast<uint64_t>(dbw) << 48) |
                       (static_cast<uint64_t>(psm) << 56);
                q[3] = 0x50ULL;
                q[4] = 0ULL;
                q[5] = 0x51ULL;
                q[6] = (static_cast<uint64_t>(rrh) << 32) | static_cast<uint64_t>(rrw);
                q[7] = 0x52ULL;
                q[8] = 0ULL;
                q[9] = 0x53ULL;
                q[10] = (2ULL << 58) | (imageQwc & 0x7FFF) | (1ULL << 15);
                q[11] = 0ULL;
                std::memcpy(pkt + 12 * 8, imgSrc, imageBytes);

                constexpr uint32_t GIF_CHANNEL = 0x1000A000;
                constexpr uint32_t CHCR_STR_MODE0 = 0x101u;
                runtime->memory().writeIORegister(GIF_CHANNEL + 0x10u, pktAddr);
                runtime->memory().writeIORegister(GIF_CHANNEL + 0x20u, totalQwc & 0xFFFFu);
                runtime->memory().writeIORegister(GIF_CHANNEL + 0x00u, CHCR_STR_MODE0);
            }
        }
    }

    int retTbp = tbp0 + ((fontDataSz >= 0 ? fontDataSz : fontDataSz + 0x7f) >> 7);
    setReturnS32(ctx, retTbp);
}

static constexpr uint32_t kFontBase     = 0x176148u;
static constexpr uint32_t kFontEntrySz = 0x24u;

void sceeFontGenerateString(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const float sclx = ctx->f[12];
    const float scly = ctx->f[13];
    const uint32_t bufAddr  = getRegU32(ctx, 4);
    const uint64_t paramX   = GPR_U64(ctx, 5);
    const int64_t  paramY   = GPR_S64(ctx, 6);
    const int      paramW   = static_cast<int>(getRegU32(ctx, 7));
    const int      paramH   = static_cast<int>(getRegU32(ctx, 8));
    const uint32_t colour   = getRegU32(ctx, 9);
    const int      alignCh  = static_cast<int8_t>(getRegU32(ctx, 10) & 0xffu);
    const int      fontId   = static_cast<int>(getRegU32(ctx, 11));

    const uint32_t sp = getRegU32(ctx, 29);
    const uint32_t strAddr = FAST_READ32(sp + 0x00u);
    const uint32_t param14 = FAST_READ32(sp + 0x18u);

    if (bufAddr == 0u)
    {
        setReturnS32(ctx, 0);
        ctx->pc = getRegU32(ctx, 31);
        return;
    }

    const uint32_t gp = getRegU32(ctx, 28);
    const uint32_t fontModeAdj = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7c98)));
    const uint32_t shiftAmt = fontModeAdj & 0x1fu;
    const int scrHeight = static_cast<int>(FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b5c))));
    const int scrWidth  = static_cast<int>(FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b60))));
    const uint32_t fontClut = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b64)));

    const uint32_t fontOff = static_cast<uint32_t>(fontId * static_cast<int>(kFontEntrySz));
    const int lineH = static_cast<int>(FAST_READ32(kFontBase + fontOff + 0x18u));

    int iVar21 = 0;
    int iStack_dc = 0;
    uint32_t uStack_d8 = 0;
    int iVar15 = 0;

    int16_t sVar8;
    {
        int yStepRaw = static_cast<int>(static_cast<float>((lineH + 6) * 16) * scly);
        sVar8 = static_cast<int16_t>((static_cast<int>(paramY) + 0x700) * 16) + static_cast<int16_t>(yStepRaw >> static_cast<int>(shiftAmt));
    }

    int16_t baseX = static_cast<int16_t>((static_cast<int>(paramX) + 0x6c0) * 16);

    if (param14 != 0u)
    {
        int64_t clipY1 = static_cast<int64_t>(static_cast<int>(paramY) + paramH);
        int64_t clipX1 = static_cast<int64_t>(static_cast<int>(paramX) + paramW);
        if (clipY1 > scrHeight - 1) clipY1 = static_cast<int64_t>(scrHeight - 1);
        if (clipX1 > scrWidth - 1)  clipX1 = static_cast<int64_t>(scrWidth - 1);
        int64_t clipY0 = 0;
        if (paramY > 0) clipY0 = paramY;
        uint64_t clipX0 = 0;
        if (static_cast<int64_t>(paramX) > 0) clipX0 = paramX;

        uint64_t scissor = clipX0 | (static_cast<uint64_t>(static_cast<uint32_t>(clipX1)) << 16)
                         | (static_cast<uint64_t>(static_cast<uint32_t>(clipY0)) << 32) | (static_cast<uint64_t>(static_cast<uint32_t>(clipY1)) << 48);

        FAST_WRITE64(bufAddr + 0x00, 0x1000000000000005ull);
        FAST_WRITE64(bufAddr + 0x08, 0x0eull);
        FAST_WRITE64(bufAddr + 0x10, scissor);
        FAST_WRITE64(bufAddr + 0x18, 0x40ull);
        FAST_WRITE64(bufAddr + 0x20, 0x20000ull);
        FAST_WRITE64(bufAddr + 0x28, 0x47ull);
        FAST_WRITE64(bufAddr + 0x30, 0x44ull);
        FAST_WRITE64(bufAddr + 0x38, 0x42ull);
        FAST_WRITE64(bufAddr + 0x40, 0x160ull);
        FAST_WRITE64(bufAddr + 0x48, 0x14ull);
        FAST_WRITE64(bufAddr + 0x50, 0x156ull);
        FAST_WRITE64(bufAddr + 0x58, 0ull);
        FAST_WRITE64(bufAddr + 0x60, 0x1000000000000001ull);
        FAST_WRITE64(bufAddr + 0x68, 0x0eull);

        uint64_t iVar5  = static_cast<uint64_t>(FAST_READ32(kFontBase + fontOff + 0x08u));
        uint64_t iVar22 = static_cast<uint64_t>(FAST_READ32(kFontBase + fontOff + 0x0cu));
        uint64_t iVar3  = static_cast<uint64_t>(FAST_READ32(kFontBase + fontOff + 0x10u));
        uint64_t iVar4  = static_cast<uint64_t>(FAST_READ32(kFontBase + fontOff + 0x14u));

        uint64_t tex0 = iVar5
                       | 0x2000000000000000ull
                       | (iVar22 << 14)
                       | 0x400000000ull
                       | (iVar3 << 26)
                       | 0x1400000ull
                       | (iVar4 << 30)
                       | (static_cast<uint64_t>(fontClut) << 37);

        FAST_WRITE64(bufAddr + 0x70, tex0);
        FAST_WRITE64(bufAddr + 0x78, 6ull);
        FAST_WRITE64(bufAddr + 0x80, 0x1000000000000001ull);
        FAST_WRITE64(bufAddr + 0x88, 0x0eull);
        FAST_WRITE64(bufAddr + 0x90, static_cast<uint64_t>(colour));
        FAST_WRITE64(bufAddr + 0x98, 1ull);

        iVar21 = 10;
    }

    int iVar22_qw = iVar21 + 1;
    uint32_t s2 = bufAddr + static_cast<uint32_t>(iVar22_qw * 16);
    uint32_t uVar20 = 0;

    size_t sLen = 0;
    {
        const char *hostStr = reinterpret_cast<const char *>(getConstMemPtr(rdram, strAddr));
        if (hostStr) sLen = ::strlen(hostStr);
    }

    while (uVar20 < sLen)
    {
        uint8_t bVar1 = FAST_READ8(strAddr + uVar20);
        uint32_t uVar9 = static_cast<uint32_t>(bVar1);
        int8_t chSigned = static_cast<int8_t>(bVar1);

        if (uStack_d8 < 0x21u)
        {
            goto label_check_printable;
        }

        if (uVar9 > 0x20u)
        {
            uint32_t dat176168 = FAST_READ32(kFontBase + fontOff + 0x20u);
            if (dat176168 == 0u)
            {
                uint32_t fontPtr0 = FAST_READ32(kFontBase + fontOff);
                uint32_t tableAddr = FAST_READ32(fontPtr0 + 0x2000u);
                int8_t kern = static_cast<int8_t>(FAST_READ8(tableAddr - 0x1c20u + uStack_d8 * 0xe0u + uVar9));
                iVar15 += static_cast<int>(static_cast<float>(static_cast<int>(kern)) * sclx);
            }
            goto label_check_printable;
        }

        goto label_space;

label_check_printable:
        if (uVar9 < 0x21u)
        {
            goto label_space;
        }

        {
            int glyphIdx = static_cast<int>(chSigned);
            uint32_t iVar19_off = static_cast<uint32_t>(glyphIdx * 0x20);

            if (param14 != 0u)
            {
                uint32_t fontPtr = FAST_READ32(kFontBase + fontOff);
                int16_t sVar7 = baseX + static_cast<int16_t>(iVar15);

                iVar22_qw += 2;
                iStack_dc += 1;

                uint16_t wU0 = FAST_READ16(fontPtr + iVar19_off + 0);
                uint16_t wV0 = FAST_READ16(fontPtr + iVar19_off + 2);
                FAST_WRITE16(s2 + 0x00, wU0);
                FAST_WRITE16(s2 + 0x02, wV0);

                int16_t dx0 = static_cast<int16_t>(FAST_READ16(fontPtr + iVar19_off + 8));
                int16_t dy0 = static_cast<int16_t>(FAST_READ16(fontPtr + iVar19_off + 10));
                uint16_t wX0 = static_cast<uint16_t>(sVar7 + static_cast<int16_t>(static_cast<int>(static_cast<float>(static_cast<int>(dx0)) * sclx)));
                int yVal0 = static_cast<int>(static_cast<float>(static_cast<int>(dy0)) * scly) >> static_cast<int>(shiftAmt);
                uint16_t wY0 = static_cast<uint16_t>(sVar8 + static_cast<int16_t>(yVal0));
                FAST_WRITE16(s2 + 0x08, wX0);
                FAST_WRITE16(s2 + 0x0a, wY0);
                FAST_WRITE32(s2 + 0x0c, 1u);

                s2 += 0x10u;

                uint16_t wU1 = FAST_READ16(fontPtr + iVar19_off + 4);
                uint16_t wV1 = FAST_READ16(fontPtr + iVar19_off + 6);
                FAST_WRITE16(s2 + 0x00, wU1);
                FAST_WRITE16(s2 + 0x02, wV1);

                int16_t dx1 = static_cast<int16_t>(FAST_READ16(fontPtr + iVar19_off + 12));
                int16_t dy1 = static_cast<int16_t>(FAST_READ16(fontPtr + iVar19_off + 14));
                uint16_t wX1 = static_cast<uint16_t>(sVar7 + static_cast<int16_t>(static_cast<int>(static_cast<float>(static_cast<int>(dx1)) * sclx)));
                int yVal1 = static_cast<int>(static_cast<float>(static_cast<int>(dy1)) * scly) >> static_cast<int>(shiftAmt);
                uint16_t wY1 = static_cast<uint16_t>(sVar8 + static_cast<int16_t>(yVal1));
                FAST_WRITE16(s2 + 0x08, wX1);
                FAST_WRITE16(s2 + 0x0a, wY1);
                FAST_WRITE32(s2 + 0x0c, 1u);

                s2 += 0x10u;
            }

            {
                uint32_t fontPtr = FAST_READ32(kFontBase + fontOff);
                uint32_t advOff = static_cast<uint32_t>((glyphIdx * 2 + 1) * 16 + 8);
                int16_t advW = static_cast<int16_t>(FAST_READ16(fontPtr + advOff));
                iVar15 += static_cast<int>(static_cast<float>(static_cast<int>(advW)) * sclx);
            }
        }
        goto label_next;

label_space:
        {
            int spaceW = static_cast<int>(FAST_READ32(kFontBase + fontOff + 0x1cu));
            iVar15 += static_cast<int>(static_cast<float>(spaceW) * sclx);
        }

label_next:
        uStack_d8 = uVar9;
        uVar20++;
    }

    if (param14 != 0u)
    {
        if (alignCh != 'L')
        {
            if (alignCh == 'C' || alignCh == 'R')
            {
                int shift = paramW * 16 - iVar15;
                if (alignCh == 'C') shift >>= 1;
                if (iStack_dc > 0)
                {
                    uint32_t adj = bufAddr + static_cast<uint32_t>(iVar21 * 16) + 0x20u;
                    for (int k = 0; k < iStack_dc; k++)
                    {
                        int16_t oldX0 = static_cast<int16_t>(FAST_READ16(adj - 8u));
                        int16_t oldX1 = static_cast<int16_t>(FAST_READ16(adj + 8u));
                        FAST_WRITE16(adj - 8u, static_cast<uint16_t>(oldX0 + static_cast<int16_t>(shift)));
                        FAST_WRITE16(adj + 8u, static_cast<uint16_t>(oldX1 + static_cast<int16_t>(shift)));
                        adj += 0x20u;
                    }
                }
            }
            else if (alignCh == 'J' && sLen > 1)
            {
                int iVar19_div = static_cast<int>(sLen) - 1;
                if (iVar19_div == 0) iVar19_div = 1;
                int spacePer = (paramW * 16 - iVar15) / iVar19_div;
                uint32_t adj = bufAddr + static_cast<uint32_t>(iVar21 * 16) + 0x20u;
                int accum = 0;
                for (uint32_t jj = 0; jj < sLen; jj++)
                {
                    int8_t jch = static_cast<int8_t>(FAST_READ8(strAddr + jj));
                    if (jch > 0x20)
                    {
                        int16_t oldX0 = static_cast<int16_t>(FAST_READ16(adj - 8u));
                        int16_t oldX1 = static_cast<int16_t>(FAST_READ16(adj + 8u));
                        FAST_WRITE16(adj - 8u, static_cast<uint16_t>(oldX0 + static_cast<int16_t>(accum)));
                        FAST_WRITE16(adj + 8u, static_cast<uint16_t>(oldX1 + static_cast<int16_t>(accum)));
                        adj += 0x20u;
                    }
                    accum += spacePer;
                }
            }
        }

        if (param14 != 0u)
        {
            uint32_t tagAddr = bufAddr + static_cast<uint32_t>(iVar21 * 16);
            FAST_WRITE64(tagAddr + 0x00, static_cast<uint64_t>(static_cast<uint32_t>(iStack_dc)) | 0x4400000000000000ull);
            FAST_WRITE64(tagAddr + 0x08, 0x5353ull);

            uint32_t endAddr = bufAddr + static_cast<uint32_t>(iVar22_qw * 16);
            FAST_WRITE64(endAddr + 0x00, 0x1000000000008001ull);
            FAST_WRITE64(endAddr + 0x08, 0x0eull);

            int iVar19_end = iVar22_qw + 1;
            uint32_t endAddr2 = bufAddr + static_cast<uint32_t>(iVar19_end * 16);
            FAST_WRITE64(endAddr2 + 0x00, 0x01ff0000027f0000ull);
            FAST_WRITE64(endAddr2 + 0x08, 0x40ull);

            iVar22_qw += 2;
        }
    }

    int ret = 0;
    if (param14 != 0u) ret = iVar22_qw;
    setReturnS32(ctx, ret);
    ctx->pc = getRegU32(ctx, 31);
}

void sceeFontPrintfAt(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t oldSp = getRegU32(ctx, 29);
    const uint32_t frame = oldSp - 0x900u;

    const uint32_t bufAddr = getRegU32(ctx, 4);
    const uint32_t paramX = getRegU32(ctx, 5);
    const uint32_t paramY = getRegU32(ctx, 6);
    const uint32_t fmtAddr = getRegU32(ctx, 7);

    const uint8_t *callerVa = getConstMemPtr(rdram, oldSp + 16u);
    uint8_t *frameVa = getMemPtr(rdram, frame + 0x8f8u);
    if (callerVa && frameVa)
        std::memcpy(frameVa, callerVa, 64u);

    SET_GPR_U32(ctx, 4, frame + 0x20u);
    SET_GPR_U32(ctx, 5, fmtAddr);
    SET_GPR_U32(ctx, 6, frame + 0x8f8u);
    vsprintf(rdram, ctx, runtime);

    const uint32_t gp = getRegU32(ctx, 28);
    uint32_t defaultSclxBits = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b54)));
    uint32_t defaultSclyBits = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b50)));
    uint32_t defaultColour = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b4c)));
    uint32_t defaultFontId = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b58)));
    uint32_t scrWidth = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b60)));
    uint32_t scrHeight = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b5c)));

    std::memcpy(&ctx->f[12], &defaultSclxBits, sizeof(float));
    std::memcpy(&ctx->f[13], &defaultSclyBits, sizeof(float));

    FAST_WRITE32(frame + 0x00u, frame + 0x20u);
    FAST_WRITE32(frame + 0x08u, frame + 0x820u);
    FAST_WRITE32(frame + 0x10u, frame + 0x824u);
    FAST_WRITE32(frame + 0x18u, 1u);

    SET_GPR_U32(ctx, 29, frame);
    SET_GPR_U32(ctx, 4, bufAddr);
    SET_GPR_U32(ctx, 5, paramX);
    SET_GPR_U32(ctx, 6, paramY);
    SET_GPR_U32(ctx, 7, scrWidth);
    SET_GPR_U32(ctx, 8, scrHeight);
    SET_GPR_U32(ctx, 9, defaultColour);
    SET_GPR_U32(ctx, 10, 0x4cu);
    SET_GPR_U32(ctx, 11, defaultFontId);

    sceeFontGenerateString(rdram, ctx, runtime);

    SET_GPR_U32(ctx, 29, oldSp);
    ctx->pc = getRegU32(ctx, 31);
}

void sceeFontPrintfAt2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t oldSp = getRegU32(ctx, 29);
    const uint32_t frame = oldSp - 0x900u;

    const uint32_t bufAddr   = getRegU32(ctx, 4);
    const uint32_t paramX    = getRegU32(ctx, 5);
    const uint32_t paramY    = getRegU32(ctx, 6);
    const uint32_t paramW    = getRegU32(ctx, 7);
    const uint32_t paramH    = getRegU32(ctx, 8);
    const uint32_t alignRaw  = getRegU32(ctx, 9);
    const uint32_t fmtAddr   = getRegU32(ctx, 10);
    const uint64_t param8    = GPR_U64(ctx, 11);

    int8_t alignChar = static_cast<int8_t>(alignRaw & 0xffu);

    FAST_WRITE64(frame + 0x8f8u, param8);

    SET_GPR_U32(ctx, 4, frame + 0x20u);
    SET_GPR_U32(ctx, 5, fmtAddr);
    SET_GPR_U32(ctx, 6, frame + 0x8f8u);
    vsprintf(rdram, ctx, runtime);

    const uint32_t gp = getRegU32(ctx, 28);
    uint32_t defaultSclxBits = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b54)));
    uint32_t defaultSclyBits = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b50)));
    uint32_t defaultColour   = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b4c)));
    uint32_t defaultFontId   = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b58)));

    std::memcpy(&ctx->f[12], &defaultSclxBits, sizeof(float));
    std::memcpy(&ctx->f[13], &defaultSclyBits, sizeof(float));

    FAST_WRITE32(frame + 0x00u, frame + 0x20u);
    FAST_WRITE32(frame + 0x08u, frame + 0x820u);
    FAST_WRITE32(frame + 0x10u, frame + 0x824u);
    FAST_WRITE32(frame + 0x18u, 1u);

    SET_GPR_U32(ctx, 29, frame);
    SET_GPR_U32(ctx, 4, bufAddr);
    SET_GPR_U32(ctx, 5, paramX);
    SET_GPR_U32(ctx, 6, paramY);
    SET_GPR_U32(ctx, 7, paramW);
    SET_GPR_U32(ctx, 8, paramH);
    SET_GPR_U32(ctx, 9, defaultColour);
    SET_GPR_U32(ctx, 10, static_cast<uint32_t>(static_cast<uint8_t>(alignChar)));
    SET_GPR_U32(ctx, 11, defaultFontId);

    sceeFontGenerateString(rdram, ctx, runtime);

    SET_GPR_U32(ctx, 29, oldSp);
    ctx->pc = getRegU32(ctx, 31);
}

void sceeFontClose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static constexpr uint32_t kFontBase = 0x176148u;
    static constexpr uint32_t kFontEntrySz = 0x24u;
    const int fontId = static_cast<int>(getRegU32(ctx, 4));
    const uint32_t fontOff = static_cast<uint32_t>(fontId * static_cast<int>(kFontEntrySz));
    uint32_t glyphPtr = 0;
    if (const uint8_t *p = getConstMemPtr(rdram, kFontBase + fontOff))
        glyphPtr = *reinterpret_cast<const uint32_t *>(p);
    if (glyphPtr != 0u)
    {
        if (runtime)
        {
            uint32_t kernPtr = 0;
            if (const uint8_t *kp = getConstMemPtr(rdram, glyphPtr + 0x2000u))
                kernPtr = *reinterpret_cast<const uint32_t *>(kp);
            if (kernPtr != 0u)
                runtime->guestFree(kernPtr);
            runtime->guestFree(glyphPtr);
        }
        if (uint8_t *p = getMemPtr(rdram, kFontBase + fontOff))
            *reinterpret_cast<uint32_t *>(p) = 0u;
        setReturnS32(ctx, 0);
    }
    else
    {
        setReturnS32(ctx, -1);
    }
}

void sceeFontSetColour(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t gp = getRegU32(ctx, 28);
    writeU32AtGp(rdram, gp, -0x7b4c, getRegU32(ctx, 4));
}

void sceeFontSetMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t gp = getRegU32(ctx, 28);
    writeU32AtGp(rdram, gp, -0x7c98, getRegU32(ctx, 4));
    setReturnS32(ctx, 0);
}

void sceeFontSetFont(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t gp = getRegU32(ctx, 28);
    writeU32AtGp(rdram, gp, -0x7b58, getRegU32(ctx, 4));
}

void sceeFontSetScale(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t gp = getRegU32(ctx, 28);
    uint32_t sclx_bits, scly_bits;
    std::memcpy(&sclx_bits, &ctx->f[12], sizeof(float));
    std::memcpy(&scly_bits, &ctx->f[13], sizeof(float));
    writeU32AtGp(rdram, gp, -0x7b54, sclx_bits);
    writeU32AtGp(rdram, gp, -0x7b50, scly_bits);
}

void sceIoctl(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}

void sceIpuInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static constexpr uint32_t REG_IPU_CTRL = 0x10002010u;
    static constexpr uint32_t REG_IPU_CMD = 0x10002000u;
    static constexpr uint32_t REG_IPU_IN_FIFO = 0x10007010u;
    static constexpr uint32_t IQVAL_BASE = 0x1721e0u;
    static constexpr uint32_t VQVAL_BASE = 0x172230u;
    static constexpr uint32_t SETD4_CHCR_ENTRY = 0x126428u;

    if (!runtime)
        return;

    PS2Memory &mem = runtime->memory();

    auto setD4 = runtime->lookupFunction(SETD4_CHCR_ENTRY);
    if (setD4)
    {
        ctx->r[4] = _mm_set_epi64x(0, 1);
        setD4(rdram, ctx, runtime);
    }

    mem.write32(REG_IPU_CTRL, 0x40000000u);
    mem.write32(REG_IPU_CMD, 0u);

    __m128i v;
    v = runtime->Load128(rdram, ctx, IQVAL_BASE + 0x00u);
    mem.write128(REG_IPU_IN_FIFO, v);
    v = runtime->Load128(rdram, ctx, IQVAL_BASE + 0x10u);
    mem.write128(REG_IPU_IN_FIFO, v);
    v = runtime->Load128(rdram, ctx, IQVAL_BASE + 0x20u);
    mem.write128(REG_IPU_IN_FIFO, v);
    v = runtime->Load128(rdram, ctx, IQVAL_BASE + 0x30u);
    mem.write128(REG_IPU_IN_FIFO, v);
    v = runtime->Load128(rdram, ctx, IQVAL_BASE + 0x40u);
    mem.write128(REG_IPU_IN_FIFO, v);
    mem.write128(REG_IPU_IN_FIFO, v);
    mem.write128(REG_IPU_IN_FIFO, v);
    mem.write128(REG_IPU_IN_FIFO, v);

    mem.write32(REG_IPU_CMD, 0x50000000u);
    mem.write32(REG_IPU_CMD, 0x58000000u);

    v = runtime->Load128(rdram, ctx, VQVAL_BASE + 0x00u);
    mem.write128(REG_IPU_IN_FIFO, v);
    v = runtime->Load128(rdram, ctx, VQVAL_BASE + 0x10u);
    mem.write128(REG_IPU_IN_FIFO, v);

    mem.write32(REG_IPU_CMD, 0x60000000u);
    mem.write32(REG_IPU_CMD, 0x90000000u);

    mem.write32(REG_IPU_CTRL, 0x40000000u);
    mem.write32(REG_IPU_CMD, 0u);
}

void sceIpuRestartDMA(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}

void sceIpuStopDMA(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}

void sceIpuSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}

void sceLseek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    ps2_syscalls::fioLseek(rdram, ctx, runtime);
}

void sceMcChangeThreadPriority(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}

void sceMcChdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    {
        std::lock_guard<std::mutex> lock(g_mcStateMutex);
        g_mcLastResult = 0;
    }
    setReturnS32(ctx, 0);
}

void sceMcClose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    {
        std::lock_guard<std::mutex> lock(g_mcStateMutex);
        g_mcLastResult = 0;
    }
    setReturnS32(ctx, 0);
}

void sceMcDelete(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    {
        std::lock_guard<std::mutex> lock(g_mcStateMutex);
        g_mcLastResult = 0;
    }
    setReturnS32(ctx, 0);
}

void sceMcFlush(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    {
        std::lock_guard<std::mutex> lock(g_mcStateMutex);
        g_mcLastResult = 0;
    }
    setReturnS32(ctx, 0);
}

void sceMcFormat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    {
        std::lock_guard<std::mutex> lock(g_mcStateMutex);
        g_mcLastResult = 0;
    }
    setReturnS32(ctx, 0);
}

void sceMcGetDir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    {
        std::lock_guard<std::mutex> lock(g_mcStateMutex);
        g_mcLastResult = 0;
    }
    setReturnS32(ctx, 0);
}

void sceMcGetEntSpace(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 1024);
}

void sceMcGetInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t typePtr = getRegU32(ctx, 6);
    const uint32_t freePtr = getRegU32(ctx, 7);
    const uint32_t formatPtr = readStackU32(rdram, ctx, 16);

    const int32_t cardType = 2; // PS2 memory card.
    const int32_t freeBlocks = 0x2000;
    const int32_t format = 2; // formatted.

    if (typePtr != 0u)
    {
        if (uint8_t *out = getMemPtr(rdram, typePtr))
        {
            std::memcpy(out, &cardType, sizeof(cardType));
        }
    }
    if (freePtr != 0u)
    {
        if (uint8_t *out = getMemPtr(rdram, freePtr))
        {
            std::memcpy(out, &freeBlocks, sizeof(freeBlocks));
        }
    }
    if (formatPtr != 0u)
    {
        if (uint8_t *out = getMemPtr(rdram, formatPtr))
        {
            std::memcpy(out, &format, sizeof(format));
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_mcStateMutex);
        g_mcLastResult = 0;
    }
    setReturnS32(ctx, 0);
}

void sceMcGetSlotMax(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 1);
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
    {
        std::lock_guard<std::mutex> lock(g_mcStateMutex);
        g_mcLastResult = 0;
    }
    setReturnS32(ctx, 0);
}

void sceMcOpen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    int32_t fd = 0;
    {
        std::lock_guard<std::mutex> lock(g_mcStateMutex);
        fd = g_mcNextFd++;
        if (g_mcNextFd <= 0)
        {
            g_mcNextFd = 1;
        }
        g_mcLastResult = fd;
    }
    setReturnS32(ctx, 0);
}

void sceMcRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int32_t size = static_cast<int32_t>(getRegU32(ctx, 7));
    if (size > 0)
    {
        const uint32_t dstAddr = readStackU32(rdram, ctx, 16);
        if (uint8_t *dst = getMemPtr(rdram, dstAddr))
        {
            std::memset(dst, 0, static_cast<size_t>(size));
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_mcStateMutex);
        g_mcLastResult = std::max<int32_t>(0, size);
    }
    setReturnS32(ctx, 0);
}

void sceMcRename(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    {
        std::lock_guard<std::mutex> lock(g_mcStateMutex);
        g_mcLastResult = 0;
    }
    setReturnS32(ctx, 0);
}

void sceMcSeek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int32_t offset = static_cast<int32_t>(getRegU32(ctx, 5));
    {
        std::lock_guard<std::mutex> lock(g_mcStateMutex);
        g_mcLastResult = std::max<int32_t>(0, offset);
    }
    setReturnS32(ctx, 0);
}

void sceMcSetFileInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    {
        std::lock_guard<std::mutex> lock(g_mcStateMutex);
        g_mcLastResult = 0;
    }
    setReturnS32(ctx, 0);
}

void sceMcSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t cmdPtr = getRegU32(ctx, 5);
    const uint32_t resultPtr = getRegU32(ctx, 6);
    int32_t result = 0;
    {
        std::lock_guard<std::mutex> lock(g_mcStateMutex);
        result = g_mcLastResult;
    }

    if (cmdPtr != 0u)
    {
        if (uint8_t *out = getMemPtr(rdram, cmdPtr))
        {
            const int32_t cmd = 0;
            std::memcpy(out, &cmd, sizeof(cmd));
        }
    }
    if (resultPtr != 0u)
    {
        if (uint8_t *out = getMemPtr(rdram, resultPtr))
        {
            std::memcpy(out, &result, sizeof(result));
        }
    }

    // 1 = command finished in this runtime's immediate model.
    setReturnS32(ctx, 1);
}

void sceMcUnformat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    {
        std::lock_guard<std::mutex> lock(g_mcStateMutex);
        g_mcLastResult = 0;
    }
    setReturnS32(ctx, 0);
}

void sceMcWrite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int32_t size = static_cast<int32_t>(getRegU32(ctx, 7));
    {
        std::lock_guard<std::mutex> lock(g_mcStateMutex);
        g_mcLastResult = std::max<int32_t>(0, size);
    }
    setReturnS32(ctx, 0);
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
    (void)rdram;
    (void)runtime;
    setReturnU32(ctx, 0u);
}

void sceMpegClearRefBuff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)ctx;
    (void)runtime;
    static const uint32_t kRefGlobalAddrs[] = {
        0x171800u, 0x17180Cu, 0x171818u, 0x171804u, 0x171810u, 0x17181Cu
    };
    for (uint32_t addr : kRefGlobalAddrs)
    {
        uint8_t *p = getMemPtr(rdram, addr);
        if (!p)
            continue;
        uint32_t ptr = *reinterpret_cast<uint32_t *>(p);
        if (ptr != 0u)
        {
            uint8_t *q = getMemPtr(rdram, ptr + 0x28u);
            if (q)
                *reinterpret_cast<uint32_t *>(q) = 0u;
        }
    }
    setReturnU32(ctx, 1u);
}

static void mpegGuestWrite32(uint8_t *rdram, uint32_t addr, uint32_t value)
{
    if (uint8_t *p = getMemPtr(rdram, addr))
        *reinterpret_cast<uint32_t *>(p) = value;
}
static void mpegGuestWrite64(uint8_t *rdram, uint32_t addr, uint64_t value)
{
    if (uint8_t *p = getMemPtr(rdram, addr))
    {
        *reinterpret_cast<uint32_t *>(p) = static_cast<uint32_t>(value);
        *reinterpret_cast<uint32_t *>(p + 4) = static_cast<uint32_t>(value >> 32);
    }
}

void sceMpegCreate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t param_1 = getRegU32(ctx, 4);   // a0
    const uint32_t param_2 = getRegU32(ctx, 5);   // a1
    const uint32_t param_3 = getRegU32(ctx, 6);   // a2

    const uint32_t uVar3 = (param_2 + 3u) & 0xFFFFFFFCu;
    const int32_t iVar2_signed = static_cast<int32_t>(param_3) - static_cast<int32_t>(uVar3 - param_2);

    if (iVar2_signed <= 0x117)
    {
        setReturnU32(ctx, 0u);
        return;
    }

    const uint32_t puVar4 = uVar3 + 0x108u;
    const uint32_t innerSize = static_cast<uint32_t>(iVar2_signed) - 0x118u;

    mpegGuestWrite32(rdram, param_1 + 0x40, uVar3);

    const uint32_t a1_init = uVar3 + 0x118u;
    mpegGuestWrite32(rdram, puVar4 + 0x0, a1_init);
    mpegGuestWrite32(rdram, puVar4 + 0x4, innerSize);
    mpegGuestWrite32(rdram, puVar4 + 0x8, a1_init);
    mpegGuestWrite32(rdram, puVar4 + 0xC, a1_init);

    const uint32_t allocResult = runtime ? runtime->guestMalloc(0x600, 8u) : (uVar3 + 0x200u);
    mpegGuestWrite32(rdram, uVar3 + 0x44, allocResult);

    // param_1[0..2] = 0; param_1[4..0xe] = 0xffffffff/0 as per decompilation
    mpegGuestWrite32(rdram, param_1 + 0x00, 0);
    mpegGuestWrite32(rdram, param_1 + 0x04, 0);
    mpegGuestWrite32(rdram, param_1 + 0x08, 0);
    mpegGuestWrite64(rdram, param_1 + 0x10, 0xFFFFFFFFFFFFFFFFULL);
    mpegGuestWrite64(rdram, param_1 + 0x18, 0xFFFFFFFFFFFFFFFFULL);
    mpegGuestWrite64(rdram, param_1 + 0x20, 0);
    mpegGuestWrite64(rdram, param_1 + 0x28, 0xFFFFFFFFFFFFFFFFULL);
    mpegGuestWrite64(rdram, param_1 + 0x30, 0xFFFFFFFFFFFFFFFFULL);
    mpegGuestWrite64(rdram, param_1 + 0x38, 0);

    static const unsigned s_zeroOffsets[] = {
        0xB4, 0xB8, 0xBC, 0xC0, 0xC4, 0xC8, 0xCC, 0xD0, 0xD4, 0xD8, 0xDC, 0xE0, 0xE4, 0xE8, 0xF8,
        0x0C, 0x14, 0x2C, 0x34, 0x3C,
        0x48, 0xFC, 0x100, 0x104, 0x70, 0x90, 0xAC
    };
    for (unsigned off : s_zeroOffsets)
        mpegGuestWrite32(rdram, uVar3 + off, 0u);
    mpegGuestWrite64(rdram, uVar3 + 0x78, 0);
    mpegGuestWrite64(rdram, uVar3 + 0x88, 0);

    mpegGuestWrite64(rdram, uVar3 + 0xF0, 0xFFFFFFFFFFFFFFFFULL);
    mpegGuestWrite32(rdram, uVar3 + 0x1C, 0x1209F8u);
    mpegGuestWrite32(rdram, uVar3 + 0x24, 0x120A08u);
    mpegGuestWrite32(rdram, uVar3 + 0xB0, 1u);
    mpegGuestWrite32(rdram, uVar3 + 0x9C, 0xFFFFFFFFu);
    mpegGuestWrite32(rdram, uVar3 + 0x80, 0xFFFFFFFFu);
    mpegGuestWrite32(rdram, uVar3 + 0x94, 0xFFFFFFFFu);
    mpegGuestWrite32(rdram, uVar3 + 0x98, 0xFFFFFFFFu);

    mpegGuestWrite32(rdram, 0x1717BCu, param_1);

    static const uint32_t s_refValues[] = {
        0x171A50u, 0x171C58u, 0x171CC0u, 0x171D28u, 0x171D90u,
        0x171AB8u, 0x171B20u, 0x171B88u, 0x171BF0u
    };
    for (unsigned i = 0; i < 9u; ++i)
        mpegGuestWrite32(rdram, 0x171800u + i * 4u, s_refValues[i]);

    uint32_t setDynamicRet = a1_init;
    if (uint8_t *p = getMemPtr(rdram, puVar4 + 8))
        setDynamicRet = *reinterpret_cast<uint32_t *>(p);
    mpegGuestWrite32(rdram, puVar4 + 12, setDynamicRet);

    setReturnU32(ctx, setDynamicRet);
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
    (void)runtime;
    const uint32_t param_1 = getRegU32(ctx, 4);
    if (uint8_t *base = getMemPtr(rdram, param_1))
    {
        const uint32_t iVar1 = *reinterpret_cast<uint32_t *>(base + 0x40);
        if (uint8_t *inner = getMemPtr(rdram, iVar1))
        {
            *reinterpret_cast<uint32_t *>(inner + 0xb0) = 1;
            *reinterpret_cast<uint32_t *>(inner + 0xd8) = (getRegU32(ctx, 5) & 0x0FFFFFFFu) | 0x20000000u;
            *reinterpret_cast<uint32_t *>(inner + 0xe4) = getRegU32(ctx, 6);
            *reinterpret_cast<uint32_t *>(inner + 0xdc) = 0;
            *reinterpret_cast<uint32_t *>(inner + 0xe0) = 0;
        }
    }
    setReturnU32(ctx, 0u);
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
    (void)runtime;
    const uint32_t param_1 = getRegU32(ctx, 4);
    uint8_t *base = getMemPtr(rdram, param_1 + 0x40u);
    if (base)
    {
        uint32_t ptrAddr = *reinterpret_cast<uint32_t *>(base);
        if (ptrAddr != 0u)
        {
            uint8_t *p = getMemPtr(rdram, ptrAddr);
            if (p)
                *reinterpret_cast<uint32_t *>(p) = 1u;
        }
    }
    setReturnS32(ctx, 1);
}

void sceMpegIsRefBuffEmpty(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceMpegIsRefBuffEmpty", rdram, ctx, runtime);
}

void sceMpegReset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)runtime;
    const uint32_t param_1 = getRegU32(ctx, 4);
    uint8_t *base = getMemPtr(rdram, param_1);
    if (!base)
        return;
    uint32_t inner = *reinterpret_cast<uint32_t *>(base + 0x40);
    if (inner == 0u)
        return;
    mpegGuestWrite32(rdram, inner + 0x00, 0u);
    mpegGuestWrite32(rdram, inner + 0x04, 0u);
    mpegGuestWrite32(rdram, inner + 0x08, 0u);
    mpegGuestWrite32(rdram, param_1 + 0x08, 0u);
    mpegGuestWrite32(rdram, inner + 0x80, 0xFFFFFFFFu);
    mpegGuestWrite32(rdram, inner + 0xAC, 0u);
    mpegGuestWrite32(rdram, 0x171904u, 0u);
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
    setReturnS32(ctx, 1);
}

void scePadEnterPressMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 1);
}

void scePadExitPressMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 1);
}

void scePadGetButtonMask(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, static_cast<int32_t>(0xFFFFu));
}

void scePadGetDmaStr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnU32(ctx, getRegU32(ctx, 6));
}

void scePadGetFrameCount(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static uint32_t frameCount = 0u;
    setReturnU32(ctx, ++frameCount);
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
    const int32_t act = static_cast<int32_t>(getRegU32(ctx, 6));
    if (act < 0)
    {
        setReturnS32(ctx, 1); // one actuator descriptor
        return;
    }
    setReturnS32(ctx, 0);
}

void scePadInfoComb(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
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
    const int port = static_cast<int>(getRegU32(ctx, 4));
    const int slot = static_cast<int>(getRegU32(ctx, 5));
    const uint32_t dataAddr = getRegU32(ctx, 6);
    uint8_t *data = getMemPtr(rdram, dataAddr);
    if (!data)
    {
        setReturnS32(ctx, 0);
        return;
    }
    if (runtime && runtime->padBackend().readState(port, slot, data, 32))
    {
        setReturnS32(ctx, 1);
        return;
    }
    std::memset(data, 0, 32);
    data[1] = 0x73;
    data[2] = data[3] = 0xFF;
    data[4] = data[5] = data[6] = data[7] = 0x80;
    setReturnS32(ctx, 1);
}

void scePadReqIntToStr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t outAddr = getRegU32(ctx, 5);
    if (uint8_t *out = getMemPtr(rdram, outAddr))
    {
        constexpr const char *kReq = "COMPLETE";
        std::memcpy(out, kReq, std::strlen(kReq) + 1u);
        setReturnU32(ctx, outAddr);
        return;
    }
    setReturnU32(ctx, 0u);
}

void scePadSetActAlign(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 1);
}

void scePadSetActDirect(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 1);
}

void scePadSetButtonInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 1);
}

void scePadSetMainMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 1);
}

void scePadSetReqState(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 1);
}

void scePadSetVrefParam(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 1);
}

void scePadSetWarningLevel(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 1);
}

void scePadStateIntToStr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int32_t state = static_cast<int32_t>(getRegU32(ctx, 4));
    const uint32_t outAddr = getRegU32(ctx, 5);
    const char *label = "UNKNOWN";
    switch (state)
    {
    case 0:
        label = "DISCONN";
        break;
    case 6:
        label = "STABLE";
        break;
    default:
        break;
    }

    if (uint8_t *out = getMemPtr(rdram, outAddr))
    {
        std::memcpy(out, label, std::strlen(label) + 1u);
        setReturnU32(ctx, outAddr);
        return;
    }
    setReturnU32(ctx, 0u);
}

void scePrintf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t format_addr = getRegU32(ctx, 4);
    const std::string formatOwned = readPs2CStringBounded(rdram, runtime, format_addr, 1024);
    if (format_addr == 0)
        return;
    std::string rendered = formatPs2StringWithArgs(rdram, ctx, runtime, formatOwned.c_str(), 1);
    if (rendered.size() > 2048)
        rendered.resize(2048);
    const std::string logLine = sanitizeForLog(rendered);
    uint32_t count = 0;
    {
        std::lock_guard<std::mutex> lock(g_printfLogMutex);
        count = ++g_printfLogCount;
    }
    if (count <= kMaxPrintfLogs)
    {
        std::cout << "PS2 scePrintf: " << logLine;
        std::cout << std::flush;
    }
    else if (count == kMaxPrintfLogs + 1)
    {
        std::cerr << "PS2 printf logging suppressed after " << kMaxPrintfLogs << " lines" << std::endl;
    }
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
    std::mutex g_sifCmdStateMutex;
    std::unordered_map<uint32_t, uint32_t> g_sifRegs;
    std::unordered_map<uint32_t, uint32_t> g_sifSregs;
    std::unordered_map<uint32_t, uint32_t> g_sifCmdHandlers;
    uint32_t g_sifCmdBuffer = 0u;
    uint32_t g_sifSysCmdBuffer = 0u;
    bool g_sifCmdInitialized = false;

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
    const uint32_t cid = getRegU32(ctx, 4);
    const uint32_t handler = getRegU32(ctx, 5);
    std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
    g_sifCmdHandlers[cid] = handler;
    setReturnS32(ctx, 0);
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
    std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
    g_sifCmdInitialized = false;
    g_sifCmdHandlers.clear();
    setReturnS32(ctx, 0);
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
    const uint32_t reg = getRegU32(ctx, 4);
    uint32_t value = 0u;
    {
        std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
        auto it = g_sifRegs.find(reg);
        if (it != g_sifRegs.end())
        {
            value = it->second;
        }
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
    g_iopHeapNext = kIopHeapBase;
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
    setReturnU32(ctx, getRegU32(ctx, 5));
}

void sceSifSetReg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t reg = getRegU32(ctx, 4);
    const uint32_t value = getRegU32(ctx, 5);
    uint32_t prev = 0u;
    {
        std::lock_guard<std::mutex> lock(g_sifCmdStateMutex);
        auto it = g_sifRegs.find(reg);
        if (it != g_sifRegs.end())
        {
            prev = it->second;
        }
        g_sifRegs[reg] = value;
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

namespace
{
    bool readVuVec4f(uint8_t *rdram, uint32_t addr, float (&out)[4])
    {
        const uint8_t *ptr = getConstMemPtr(rdram, addr);
        if (!ptr)
        {
            return false;
        }
        std::memcpy(out, ptr, sizeof(out));
        return true;
    }

    bool writeVuVec4f(uint8_t *rdram, uint32_t addr, const float (&in)[4])
    {
        uint8_t *ptr = getMemPtr(rdram, addr);
        if (!ptr)
        {
            return false;
        }
        std::memcpy(ptr, in, sizeof(in));
        return true;
    }

    bool readVuVec4i(uint8_t *rdram, uint32_t addr, int32_t (&out)[4])
    {
        const uint8_t *ptr = getConstMemPtr(rdram, addr);
        if (!ptr)
        {
            return false;
        }
        std::memcpy(out, ptr, sizeof(out));
        return true;
    }

    bool writeVuVec4i(uint8_t *rdram, uint32_t addr, const int32_t (&in)[4])
    {
        uint8_t *ptr = getMemPtr(rdram, addr);
        if (!ptr)
        {
            return false;
        }
        std::memcpy(ptr, in, sizeof(in));
        return true;
    }
}

void sceVpu0Reset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}

void sceVu0AddVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t dstAddr = getRegU32(ctx, 4);
    const uint32_t lhsAddr = getRegU32(ctx, 5);
    const uint32_t rhsAddr = getRegU32(ctx, 6);
    float lhs[4]{}, rhs[4]{}, out[4]{};
    if (readVuVec4f(rdram, lhsAddr, lhs) && readVuVec4f(rdram, rhsAddr, rhs))
    {
        for (int i = 0; i < 4; ++i)
        {
            out[i] = lhs[i] + rhs[i];
        }
        (void)writeVuVec4f(rdram, dstAddr, out);
    }
    setReturnS32(ctx, 0);
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
    const uint32_t dstAddr = getRegU32(ctx, 4);
    const uint32_t srcAddr = getRegU32(ctx, 5);
    float src[4]{};
    int32_t out[4]{};
    if (readVuVec4f(rdram, srcAddr, src))
    {
        for (int i = 0; i < 4; ++i)
        {
            out[i] = static_cast<int32_t>(src[i]);
        }
        (void)writeVuVec4i(rdram, dstAddr, out);
    }
    setReturnS32(ctx, 0);
}

void sceVu0FTOI4Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t dstAddr = getRegU32(ctx, 4);
    const uint32_t srcAddr = getRegU32(ctx, 5);
    float src[4]{};
    int32_t out[4]{};
    if (readVuVec4f(rdram, srcAddr, src))
    {
        for (int i = 0; i < 4; ++i)
        {
            out[i] = static_cast<int32_t>(src[i] * 16.0f);
        }
        (void)writeVuVec4i(rdram, dstAddr, out);
    }
    setReturnS32(ctx, 0);
}

void sceVu0InnerProduct(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t lhsAddr = getRegU32(ctx, 4);
    const uint32_t rhsAddr = getRegU32(ctx, 5);
    float lhs[4]{}, rhs[4]{};
    float dot = 0.0f;
    if (readVuVec4f(rdram, lhsAddr, lhs) && readVuVec4f(rdram, rhsAddr, rhs))
    {
        dot = (lhs[0] * rhs[0]) + (lhs[1] * rhs[1]) + (lhs[2] * rhs[2]) + (lhs[3] * rhs[3]);
    }

    if (ctx)
    {
        ctx->f[0] = dot;
    }
    uint32_t raw = 0u;
    std::memcpy(&raw, &dot, sizeof(raw));
    setReturnU32(ctx, raw);
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
    const uint32_t dstAddr = getRegU32(ctx, 4);
    const uint32_t srcAddr = getRegU32(ctx, 5);
    int32_t src[4]{};
    float out[4]{};
    if (readVuVec4i(rdram, srcAddr, src))
    {
        for (int i = 0; i < 4; ++i)
        {
            out[i] = static_cast<float>(src[i]);
        }
        (void)writeVuVec4f(rdram, dstAddr, out);
    }
    setReturnS32(ctx, 0);
}

void sceVu0ITOF12Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t dstAddr = getRegU32(ctx, 4);
    const uint32_t srcAddr = getRegU32(ctx, 5);
    int32_t src[4]{};
    float out[4]{};
    if (readVuVec4i(rdram, srcAddr, src))
    {
        for (int i = 0; i < 4; ++i)
        {
            out[i] = static_cast<float>(src[i]) / 4096.0f;
        }
        (void)writeVuVec4f(rdram, dstAddr, out);
    }
    setReturnS32(ctx, 0);
}

void sceVu0ITOF4Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t dstAddr = getRegU32(ctx, 4);
    const uint32_t srcAddr = getRegU32(ctx, 5);
    int32_t src[4]{};
    float out[4]{};
    if (readVuVec4i(rdram, srcAddr, src))
    {
        for (int i = 0; i < 4; ++i)
        {
            out[i] = static_cast<float>(src[i]) / 16.0f;
        }
        (void)writeVuVec4f(rdram, dstAddr, out);
    }
    setReturnS32(ctx, 0);
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
    const uint32_t dstAddr = getRegU32(ctx, 4);
    const uint32_t srcAddr = getRegU32(ctx, 5);
    float src[4]{}, out[4]{};
    if (readVuVec4f(rdram, srcAddr, src))
    {
        const float len = std::sqrt((src[0] * src[0]) + (src[1] * src[1]) + (src[2] * src[2]) + (src[3] * src[3]));
        if (len > 1.0e-6f)
        {
            const float invLen = 1.0f / len;
            for (int i = 0; i < 4; ++i)
            {
                out[i] = src[i] * invLen;
            }
        }
        (void)writeVuVec4f(rdram, dstAddr, out);
    }
    setReturnS32(ctx, 0);
}

void sceVu0NormalLightMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0NormalLightMatrix", rdram, ctx, runtime);
}

void sceVu0OuterProduct(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t dstAddr = getRegU32(ctx, 4);
    const uint32_t lhsAddr = getRegU32(ctx, 5);
    const uint32_t rhsAddr = getRegU32(ctx, 6);
    float lhs[4]{}, rhs[4]{}, out[4]{};
    if (readVuVec4f(rdram, lhsAddr, lhs) && readVuVec4f(rdram, rhsAddr, rhs))
    {
        out[0] = (lhs[1] * rhs[2]) - (lhs[2] * rhs[1]);
        out[1] = (lhs[2] * rhs[0]) - (lhs[0] * rhs[2]);
        out[2] = (lhs[0] * rhs[1]) - (lhs[1] * rhs[0]);
        out[3] = 0.0f;
        (void)writeVuVec4f(rdram, dstAddr, out);
    }
    setReturnS32(ctx, 0);
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
    const uint32_t dstAddr = getRegU32(ctx, 4);
    const uint32_t srcAddr = getRegU32(ctx, 5);
    float src[4]{}, out[4]{};
    float scale = ctx ? ctx->f[12] : 0.0f;
    if (scale == 0.0f)
    {
        uint32_t raw = getRegU32(ctx, 6);
        std::memcpy(&scale, &raw, sizeof(scale));
        if (scale == 0.0f)
        {
            scale = static_cast<float>(getRegU32(ctx, 6));
        }
    }

    if (readVuVec4f(rdram, srcAddr, src))
    {
        for (int i = 0; i < 4; ++i)
        {
            out[i] = src[i] * scale;
        }
        (void)writeVuVec4f(rdram, dstAddr, out);
    }
    setReturnS32(ctx, 0);
}

void sceVu0ScaleVectorXYZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0ScaleVectorXYZ", rdram, ctx, runtime);
}

void sceVu0SubVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t dstAddr = getRegU32(ctx, 4);
    const uint32_t lhsAddr = getRegU32(ctx, 5);
    const uint32_t rhsAddr = getRegU32(ctx, 6);
    float lhs[4]{}, rhs[4]{}, out[4]{};
    if (readVuVec4f(rdram, lhsAddr, lhs) && readVuVec4f(rdram, rhsAddr, rhs))
    {
        for (int i = 0; i < 4; ++i)
        {
            out[i] = lhs[i] - rhs[i];
        }
        (void)writeVuVec4f(rdram, dstAddr, out);
    }
    setReturnS32(ctx, 0);
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
    std::srand(getRegU32(ctx, 4));
    setReturnS32(ctx, 0);
}

void stat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t statAddr = getRegU32(ctx, 5);
    uint8_t *statBuf = getMemPtr(rdram, statAddr);
    if (!statBuf)
    {
        setReturnS32(ctx, -1);
        return;
    }

    // Minimal fake stat payload: zeroed structure indicates a valid, readable file.
    std::memset(statBuf, 0, 128);
    setReturnS32(ctx, 0);
}

void strcasecmp(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t lhsAddr = getRegU32(ctx, 4);
    const uint32_t rhsAddr = getRegU32(ctx, 5);
    const std::string lhs = readPs2CStringBounded(rdram, runtime, lhsAddr, 1024);
    const std::string rhs = readPs2CStringBounded(rdram, runtime, rhsAddr, 1024);

    const size_t n = std::min(lhs.size(), rhs.size());
    for (size_t i = 0; i < n; ++i)
    {
        const int a = std::tolower(static_cast<unsigned char>(lhs[i]));
        const int b = std::tolower(static_cast<unsigned char>(rhs[i]));
        if (a != b)
        {
            setReturnS32(ctx, a - b);
            return;
        }
    }

    setReturnS32(ctx, static_cast<int32_t>(lhs.size()) - static_cast<int32_t>(rhs.size()));
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
