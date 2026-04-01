#include "Common.h"
#include "CD.h"
#include "MpegSource.h"

namespace ps2_stubs
{
void sceCdRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t a0 = getRegU32(ctx, 4); // usually lbn
    const uint32_t a1 = getRegU32(ctx, 5); // usually sector count
    const uint32_t a2 = getRegU32(ctx, 6); // usually destination buffer

    struct CdReadArgs
    {
        uint32_t lbn = 0;
        uint32_t sectors = 0;
        uint32_t buf = 0;
        const char *tag = "";
    };

    auto clampReadBytes = [](uint32_t sectors, uint32_t offset) -> size_t
    {
        const uint64_t requested = static_cast<uint64_t>(sectors) * static_cast<uint64_t>(kCdSectorSize);
        if (requested == 0)
        {
            return 0;
        }

        const uint64_t maxBytes = static_cast<uint64_t>(PS2_RAM_SIZE - offset);
        const uint64_t clamped = std::min<uint64_t>(requested, maxBytes);
        return static_cast<size_t>(clamped);
    };

    auto tryRead = [&](const CdReadArgs &args) -> bool
    {
        const uint32_t offset = args.buf & PS2_RAM_MASK;
        const size_t bytes = clampReadBytes(args.sectors, offset);
        if (bytes == 0)
        {
            return true;
        }

        return readCdSectors(args.lbn, args.sectors, rdram + offset, bytes);
    };

    CdReadArgs selected{a0, a1, a2, "a0/a1/a2"};
    bool ok = tryRead(selected);

    if (!ok)
    {
        // Some game-side wrappers use a nonstandard register layout.
        // If primary decode does not resolve to a known LBN, try safe alternatives.
        constexpr uint32_t kMaxReasonableSectors = PS2_RAM_SIZE / kCdSectorSize;
        if (!isResolvableCdLbn(selected.lbn))
        {
            const std::array<CdReadArgs, 5> alternatives = {
                CdReadArgs{a2, a1, a0, "a2/a1/a0"},
                CdReadArgs{a0, a2, a1, "a0/a2/a1"},
                CdReadArgs{a1, a0, a2, "a1/a0/a2"},
                CdReadArgs{a1, a2, a0, "a1/a2/a0"},
                CdReadArgs{a2, a0, a1, "a2/a0/a1"}};

            for (const CdReadArgs &candidate : alternatives)
            {
                if (candidate.sectors > kMaxReasonableSectors)
                {
                    continue;
                }
                if (!isResolvableCdLbn(candidate.lbn))
                {
                    continue;
                }

                if (tryRead(candidate))
                {
                    static uint32_t recoverLogCount = 0;
                    if (recoverLogCount < 16)
                    {
                        RUNTIME_LOG("[sceCdRead] recovered with alternate args " << candidate.tag
                                  << " (pc=0x" << std::hex << ctx->pc
                                  << " ra=0x" << getRegU32(ctx, 31)
                                  << " a0=0x" << a0
                                  << " a1=0x" << a1
                                  << " a2=0x" << a2 << std::dec << ")" << std::endl);
                        ++recoverLogCount;
                    }
                    selected = candidate;
                    ok = true;
                    break;
                }
            }
        }

        if (!ok)
        {
            const uint32_t offset = a2 & PS2_RAM_MASK;
            const size_t bytes = clampReadBytes(a1, offset);
            if (bytes > 0)
            {
                std::memset(rdram + offset, 0, bytes);
            }

            static uint32_t unresolvedLogCount = 0;
            if (unresolvedLogCount < 32)
            {
                std::cerr << "[sceCdRead] unresolved request pc=0x" << std::hex << ctx->pc
                          << " ra=0x" << getRegU32(ctx, 31)
                          << " a0=0x" << a0
                          << " a1=0x" << a1
                          << " a2=0x" << a2 << std::dec << std::endl;
                ++unresolvedLogCount;
            }
        }
    }

    if (ok)
    {
        g_cdStreamingLbn = selected.lbn + selected.sectors;
        detail::setCurrentCdStreamLbnForMpeg(g_cdStreamingLbn);
        setReturnS32(ctx, 1); // command accepted/success
        return;
    }

    setReturnS32(ctx, 0);
}


void sceCdSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0); // 0 = completed/not busy
}


void sceCdGetError(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, g_lastCdError);
}


void sceCdRI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceCdRI", rdram, ctx, runtime);
}


void sceCdRM(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceCdRM", rdram, ctx, runtime);
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
        detail::setCurrentCdStreamLbnForMpeg(g_cdStreamingLbn);
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
        RUNTIME_LOG("[sceCdSearchFile] pc=0x" << std::hex << ctx->pc
                  << " ra=0x" << callerRa
                  << " file=0x" << fileAddr
                  << " pathAddr=0x" << pathAddr
                  << " path=\"" << sanitizeForLog(path) << "\""
                  << std::dec << std::endl);
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

    if (!writeCdSearchResult(rdram, fileAddr, path, resolvedEntry))
    {
        g_lastCdError = -1;
        setReturnS32(ctx, 0);
        return;
    }

    g_cdStreamingLbn = resolvedEntry.baseLbn;
    detail::trackCdFileForMpeg(resolvedEntry.baseLbn,
                               resolvedEntry.sizeBytes,
                               resolvedEntry.hostPath);
    detail::setCurrentCdStreamLbnForMpeg(g_cdStreamingLbn);
    if (shouldTrace)
    {
        RUNTIME_LOG("[sceCdSearchFile:ok] path=\"" << sanitizeForLog(path)
                  << "\" lsn=0x" << std::hex << resolvedEntry.baseLbn
                  << " size=0x" << resolvedEntry.sizeBytes
                  << " sectors=0x" << resolvedEntry.sectors
                  << std::dec << std::endl);
    }
    setReturnS32(ctx, 1);
}

void sceCdSeek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    g_cdStreamingLbn = getRegU32(ctx, 4);
    detail::setCurrentCdStreamLbnForMpeg(g_cdStreamingLbn);
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
        detail::setCurrentCdStreamLbnForMpeg(g_cdStreamingLbn);
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
    detail::setCurrentCdStreamLbnForMpeg(g_cdStreamingLbn);
    setReturnS32(ctx, 1);
}

void sceCdStSeekF(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    g_cdStreamingLbn = getRegU32(ctx, 4);
    detail::setCurrentCdStreamLbnForMpeg(g_cdStreamingLbn);
    setReturnS32(ctx, 1);
}

void sceCdStStart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    g_cdStreamingLbn = getRegU32(ctx, 4);
    detail::setCurrentCdStreamLbnForMpeg(g_cdStreamingLbn);
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
}
