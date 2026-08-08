#include "Common.h"
#include "CD.h"
#include "MPEG.h"
#include "runtime/ee_scheduler.h"

namespace ps2_stubs
{
    namespace
    {
        constexpr uint32_t kCdStreamBlocking = 1u;
        constexpr uint32_t kDvdSectorsPerSecondX1 = 675u;
        constexpr uint32_t kDvdSectorsPerSecondX4 = kDvdSectorsPerSecondX1 * 4u;
        constexpr uint64_t kNtScFieldsPerSecondNumerator = 60000u;
        constexpr uint64_t kNtScFieldsPerSecondDenominator = 1001u;

        struct CdStreamTimingState
        {
            bool initialized = false;
            bool active = false;
            bool paused = false;
            uint32_t capacitySectors = 64u;
            uint32_t bankCount = 4u;
            uint32_t sectorsPerBank = 16u;
            uint32_t sectorsPerSecond = kDvdSectorsPerSecondX4;
            uint64_t producedSectors = 0u;
            uint64_t consumedSectors = 0u;
            uint64_t productionRemainder = 0u;
            uint64_t lastVSyncTick = 0u;
        };

        uint32_t g_cdStReadTraceCount = 0u;
        CdStreamTimingState g_cdStreamTiming;

        uint64_t currentCdStreamTick(PS2Runtime *runtime)
        {
            return runtime != nullptr ? runtime->eeScheduler().currentVSyncTick() : 0u;
        }

        uint32_t dvdStreamSectorsPerSecond(uint8_t spindleControl)
        {
            switch (spindleControl)
            {
            case 2u: // SCECdSpinX1
                return kDvdSectorsPerSecondX1;
            case 3u: // SCECdSpinX2
                return kDvdSectorsPerSecondX1 * 2u;
            case 11u: // SCECdSpin1p6
                return 1080u;
            case 4u:  // SCECdSpinX4
            case 0u:  // SCECdSpinStm / max
            case 1u:  // optimized
            case 20u: // max
            default:
                return kDvdSectorsPerSecondX4;
            }
        }

        void resetCdStreamProduction(PS2Runtime *runtime)
        {
            g_cdStreamTiming.producedSectors = 0u;
            g_cdStreamTiming.consumedSectors = 0u;
            g_cdStreamTiming.productionRemainder = 0u;
            g_cdStreamTiming.lastVSyncTick = currentCdStreamTick(runtime);
        }

        uint64_t totalCdStreamSectors()
        {
            if (g_cdStreamingEndLbn == 0xFFFFFFFFu || g_cdStreamingEndLbn < g_cdStreamingLbn)
            {
                return std::numeric_limits<uint64_t>::max();
            }
            return g_cdStreamTiming.consumedSectors + static_cast<uint64_t>(g_cdStreamingEndLbn - g_cdStreamingLbn);
        }

        void updateCdStreamProduction(PS2Runtime *runtime)
        {
            if (!g_cdStreamTiming.active || g_cdStreamTiming.paused || runtime == nullptr)
            {
                return;
            }

            const uint64_t tick = currentCdStreamTick(runtime);
            if (tick <= g_cdStreamTiming.lastVSyncTick)
            {
                return;
            }

            const uint64_t elapsedTicks = tick - g_cdStreamTiming.lastVSyncTick;
            g_cdStreamTiming.lastVSyncTick = tick;

            // 59.94 Hz field clock: sectors = fields * sectors/s * 1001 / 60000.
            const uint64_t unitsPerTick = static_cast<uint64_t>(g_cdStreamTiming.sectorsPerSecond) * kNtScFieldsPerSecondDenominator;
            const uint64_t accumulated = g_cdStreamTiming.productionRemainder + elapsedTicks * unitsPerTick;
            const uint64_t elapsedProduction = accumulated / kNtScFieldsPerSecondNumerator;
            g_cdStreamTiming.productionRemainder = accumulated % kNtScFieldsPerSecondNumerator;
            if (elapsedProduction == 0u)
            {
                return;
            }

            const uint64_t buffered = g_cdStreamTiming.producedSectors - g_cdStreamTiming.consumedSectors;
            const uint64_t effectiveCapacity = std::max<uint32_t>(1u, g_cdStreamTiming.capacitySectors);
            const uint64_t space = buffered < effectiveCapacity ? effectiveCapacity - buffered : 0u;
            uint64_t newlyProduced = std::min(elapsedProduction, space);

            const uint64_t streamTotal = totalCdStreamSectors();
            if (streamTotal != std::numeric_limits<uint64_t>::max())
            {
                const uint64_t remainingToProduce = streamTotal > g_cdStreamTiming.producedSectors
                                                        ? streamTotal - g_cdStreamTiming.producedSectors
                                                        : 0u;
                newlyProduced = std::min(newlyProduced, remainingToProduce);
            }

            g_cdStreamTiming.producedSectors += newlyProduced;
            if (elapsedProduction > newlyProduced)
            {
                g_cdStreamTiming.productionRemainder = 0u;
            }
        }

        uint32_t bufferedCdStreamSectors(PS2Runtime *runtime)
        {
            updateCdStreamProduction(runtime);
            const uint64_t buffered = g_cdStreamTiming.producedSectors - g_cdStreamTiming.consumedSectors;
            return static_cast<uint32_t>(std::min<uint64_t>(buffered, std::numeric_limits<uint32_t>::max()));
        }

        uint32_t readableCdStreamSectors(PS2Runtime *runtime)
        {
            const uint32_t buffered = bufferedCdStreamSectors(runtime);
            if (buffered == 0u)
            {
                return 0u;
            }

            const uint64_t streamTotal = totalCdStreamSectors();
            if (streamTotal != std::numeric_limits<uint64_t>::max() && g_cdStreamTiming.producedSectors >= streamTotal)
            {
                return buffered;
            }

            const uint32_t bank = std::max(1u, g_cdStreamTiming.sectorsPerBank);
            return (buffered / bank) * bank;
        }

        uint64_t cdStreamWakeTickForSectors(PS2Runtime *runtime, uint32_t sectorsNeeded)
        {
            const uint64_t now = currentCdStreamTick(runtime);
            if (sectorsNeeded == 0u || g_cdStreamTiming.sectorsPerSecond == 0u)
            {
                return now;
            }

            const uint64_t requiredUnits = static_cast<uint64_t>(sectorsNeeded) * kNtScFieldsPerSecondNumerator;
            const uint64_t remainingUnits = requiredUnits > g_cdStreamTiming.productionRemainder
                                                ? requiredUnits - g_cdStreamTiming.productionRemainder
                                                : 0u;
            const uint64_t unitsPerTick = static_cast<uint64_t>(g_cdStreamTiming.sectorsPerSecond) * kNtScFieldsPerSecondDenominator;
            const uint64_t ticks = std::max<uint64_t>(1u, (remainingUnits + unitsPerTick - 1u) / unitsPerTick);
            return now + ticks;
        }

        void restartCdStreamAt(uint32_t lbn, PS2Runtime *runtime)
        {
            g_cdStreamingLbn = lbn;
            g_cdStreamingEndLbn = cdStreamingEndLbnForStart(lbn);
            resetCdStreamProduction(runtime);
        }
    }

    CdDebugSnapshot getCdDebugSnapshot()
    {
        CdDebugSnapshot snapshot{};
        snapshot.initialized = g_cdInitialized;
        snapshot.lastError = g_lastCdError;
        snapshot.mode = g_cdMode;
        snapshot.streamingLbn = g_cdStreamingLbn;
        snapshot.streamingEndLbn = g_cdStreamingEndLbn;
        snapshot.nextPseudoLbn = g_nextPseudoLbn;
        snapshot.imageSizeBytes = g_cdImageSizeBytes;
        snapshot.imageSizeValid = g_cdImageSizeValid;
        snapshot.cdRoot = getCdRootPath();
        snapshot.cdImage = getCdImagePath();
        snapshot.imageSizePath = g_cdImageSizePath;
        snapshot.leafIndexRoot = g_cdLeafIndexRoot;
        snapshot.leafIndexBuilt = g_cdLeafIndexBuilt;
        snapshot.leafIndexCount = g_cdLeafIndex.size();
        snapshot.loosePathIndexCount = g_cdLoosePathIndex.size();

        snapshot.files.reserve(g_cdFilesByKey.size());
        for (const auto &[key, entry] : g_cdFilesByKey)
        {
            CdDebugFileEntry row{};
            row.key = key;
            row.hostPath = entry.hostPath;
            row.sizeBytes = entry.sizeBytes;
            row.baseLbn = entry.baseLbn;
            row.sectors = entry.sectors;
            snapshot.files.push_back(std::move(row));
        }
        std::sort(snapshot.files.begin(), snapshot.files.end(), [](const CdDebugFileEntry &a, const CdDebugFileEntry &b)
                  { return a.baseLbn < b.baseLbn; });
        return snapshot;
    }

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
        g_cdStreamTiming = {};
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
        g_cdStreamingEndLbn = resolvedEntry.baseLbn + resolvedEntry.sectors;
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
        restartCdStreamAt(getRegU32(ctx, 4), runtime);
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
        const uint32_t bufferSectors = getRegU32(ctx, 4);
        const uint32_t bankCount = getRegU32(ctx, 5);
        const uint32_t bufferAddr = getRegU32(ctx, 6);

        if (bufferSectors == 0u || bankCount == 0u || bufferAddr == 0u || bufferSectors / bankCount == 0u)
        {
            setReturnS32(ctx, 0);
            return;
        }

        g_cdStreamTiming.initialized = true;
        g_cdStreamTiming.active = false;
        g_cdStreamTiming.paused = false;
        g_cdStreamTiming.capacitySectors = bufferSectors;
        g_cdStreamTiming.bankCount = bankCount;
        g_cdStreamTiming.sectorsPerBank = std::max(1u, bufferSectors / bankCount);
        resetCdStreamProduction(runtime);
        setReturnS32(ctx, 1);
    }

    void sceCdStop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void sceCdStPause(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        updateCdStreamProduction(runtime);
        g_cdStreamTiming.paused = true;
        setReturnS32(ctx, 1);
    }

    namespace
    {
        struct CdStReadContinuation
        {
            uint32_t requestedSectors = 0u;
            uint32_t buffer = 0u;
            uint32_t errorAddress = 0u;
            uint32_t sectorsRead = 0u;
        };

        void finishCdStRead(uint8_t *rdram, R5900Context *ctx, const CdStReadContinuation &state, int32_t error)
        {
            if (int32_t *errorOut = reinterpret_cast<int32_t *>(getMemPtr(rdram, state.errorAddress)); errorOut)
            {
                *errorOut = error;
            }
            setReturnS32(ctx, static_cast<int32_t>(state.sectorsRead));
        }

        void continueCdStRead(uint8_t *rdram,
                              R5900Context *ctx,
                              PS2Runtime *runtime,
                              CdStReadContinuation state)
        {
            if (!g_cdStreamTiming.active || state.requestedSectors == 0u)
            {
                finishCdStRead(rdram, ctx, state, 0);
                return;
            }

            for (;;)
            {
                uint32_t remaining = state.requestedSectors - state.sectorsRead;
                bool atEnd = false;
                if (g_cdStreamingEndLbn != 0xFFFFFFFFu)
                {
                    if (g_cdStreamingLbn >= g_cdStreamingEndLbn)
                    {
                        remaining = 0u;
                        atEnd = true;
                    }
                    else
                    {
                        const uint32_t streamRemaining = g_cdStreamingEndLbn - g_cdStreamingLbn;
                        if (remaining > streamRemaining)
                        {
                            remaining = streamRemaining;
                            atEnd = true;
                        }
                    }
                }

                if (remaining == 0u)
                {
                    if (atEnd || (g_cdStreamingEndLbn != 0xFFFFFFFFu && g_cdStreamingLbn >= g_cdStreamingEndLbn))
                    {
                        notifyMpegCdStreamEof(runtime);
                    }
                    finishCdStRead(rdram, ctx, state, 0);
                    return;
                }

                uint32_t available = readableCdStreamSectors(runtime);
                if (runtime == nullptr)
                {
                    available = remaining;
                }

                if (available == 0u)
                {
                    if (!runtime)
                    {
                        finishCdStRead(rdram, ctx, state, 0);
                        return;
                    }

                    const uint32_t bank = std::max(1u, g_cdStreamTiming.sectorsPerBank);
                    const uint32_t wakeSectors = std::min(remaining, bank);
                    const uint32_t buffered = bufferedCdStreamSectors(runtime);
                    const uint32_t needed = wakeSectors > buffered ? wakeSectors - buffered : 1u;
                    const uint64_t wakeTick = cdStreamWakeTickForSectors(runtime, needed);
                    runtime->eeScheduler().waitVSync(
                        wakeTick - 1u,
                        -1,
                        [rdram, runtime, state](R5900Context &resumeContext)
                        {
                            if (static_cast<int32_t>(getRegU32(&resumeContext, 2)) < 0)
                            {
                                return;
                            }
                            continueCdStRead(rdram, &resumeContext, runtime, state);
                        });
                }

                uint32_t sectors = std::min(remaining, available);
                const uint64_t destination64 = static_cast<uint64_t>(state.buffer) + static_cast<uint64_t>(state.sectorsRead) * kCdSectorSize;
                const uint32_t destination = static_cast<uint32_t>(destination64);
                const uint32_t offset = destination & PS2_RAM_MASK;
                const size_t maxBytes = PS2_RAM_SIZE - offset;
                sectors = std::min<uint32_t>(sectors, static_cast<uint32_t>(maxBytes / kCdSectorSize));

                if (sectors == 0u)
                {
                    g_lastCdError = -1;
                    finishCdStRead(rdram, ctx, state, g_lastCdError);
                    return;
                }

                const uint32_t readLbn = g_cdStreamingLbn;
                const size_t readBytes = static_cast<size_t>(sectors) * kCdSectorSize;
                if (!readCdSectors(readLbn, sectors, rdram + offset, readBytes))
                {
                    finishCdStRead(rdram, ctx, state, g_lastCdError);
                    return;
                }

                g_cdStreamingLbn += sectors;
                if (runtime == nullptr)
                {
                    g_cdStreamTiming.producedSectors += sectors;
                }
                g_cdStreamTiming.consumedSectors += sectors;
                state.sectorsRead += sectors;

                const bool hitStreamEnd = g_cdStreamingEndLbn != 0xFFFFFFFFu && g_cdStreamingLbn >= g_cdStreamingEndLbn;
                notifyMpegCdStreamDataProduced(static_cast<uint32_t>(readBytes), hitStreamEnd);

                if (g_cdStReadTraceCount < 32u)
                {
                    std::cerr << "[sceCdStRead] requested=" << state.requestedSectors
                              << " accumulated=" << state.sectorsRead
                              << " chunk=" << sectors
                              << " buffered=" << readableCdStreamSectors(runtime)
                              << " lbn=0x" << std::hex << readLbn
                              << " end=0x" << g_cdStreamingEndLbn
                              << std::dec << std::endl;
                    ++g_cdStReadTraceCount;
                }

                if (state.sectorsRead >= state.requestedSectors || hitStreamEnd)
                {
                    finishCdStRead(rdram, ctx, state, 0);
                    return;
                }

                // STMBLK is implemented by repeatedly consuming the stream ring.
                // If a complete bank is still available, keep draining it before
                // yielding.  Otherwise the next iteration parks on the producer.
            }
        }
    }

    void sceCdStRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t requestedSectors = getRegU32(ctx, 4);
        const uint32_t buffer = getRegU32(ctx, 5);
        const uint32_t mode = getRegU32(ctx, 6);
        const uint32_t errorAddress = getRegU32(ctx, 7);

        CdStReadContinuation state{};
        state.requestedSectors = requestedSectors;
        state.buffer = buffer;
        state.errorAddress = errorAddress;

        if (int32_t *errorOut = reinterpret_cast<int32_t *>(getMemPtr(rdram, errorAddress)); errorOut)
        {
            *errorOut = 0;
        }

        if (!g_cdStreamTiming.active || requestedSectors == 0u)
        {
            setReturnS32(ctx, 0);
            return;
        }

        if (mode == kCdStreamBlocking)
        {
            continueCdStRead(rdram, ctx, runtime, state);
            return;
        }

        uint32_t remaining = requestedSectors;
        if (g_cdStreamingEndLbn != 0xFFFFFFFFu)
        {
            remaining = g_cdStreamingLbn < g_cdStreamingEndLbn
                            ? std::min(remaining, g_cdStreamingEndLbn - g_cdStreamingLbn)
                            : 0u;
        }

        const uint32_t available = runtime != nullptr
                                       ? readableCdStreamSectors(runtime)
                                       : remaining;
        const uint32_t sectors = std::min(remaining, available);
        if (sectors == 0u)
        {
            if (g_cdStreamingEndLbn != 0xFFFFFFFFu && g_cdStreamingLbn >= g_cdStreamingEndLbn)
            {
                notifyMpegCdStreamEof(runtime);
            }
            setReturnS32(ctx, 0);
            return;
        }

        state.requestedSectors = sectors;
        continueCdStRead(rdram, ctx, runtime, state);
    }

    void sceCdStream(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void sceCdStResume(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_cdStreamTiming.paused = false;
        g_cdStreamTiming.lastVSyncTick = currentCdStreamTick(runtime);
        setReturnS32(ctx, 1);
    }

    void sceCdStSeek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        restartCdStreamAt(getRegU32(ctx, 4), runtime);
        setReturnS32(ctx, 1);
    }

    void sceCdStSeekF(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        restartCdStreamAt(getRegU32(ctx, 4), runtime);
        setReturnS32(ctx, 1);
    }

    void sceCdStStart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t lbn = getRegU32(ctx, 4);
        const uint32_t modeAddr = getRegU32(ctx, 5);
        uint8_t spindleControl = 0u;
        if (const uint8_t *mode = getConstMemPtr(rdram, modeAddr); mode)
        {
            spindleControl = mode[1u];
        }

        restartCdStreamAt(lbn, runtime);
        g_cdStreamTiming.active = true;
        g_cdStreamTiming.paused = false;
        g_cdStreamTiming.sectorsPerSecond = dvdStreamSectorsPerSecond(spindleControl);
        g_cdStReadTraceCount = 0u;

        notifyMpegCdStreamStart(runtime);

        std::cerr << "[sceCdStStart] lbn=0x" << std::hex << g_cdStreamingLbn
                  << " endLbn=0x" << g_cdStreamingEndLbn << std::dec
                  << " rate=" << g_cdStreamTiming.sectorsPerSecond << " sectors/s"
                  << " buffer=" << g_cdStreamTiming.capacitySectors << " sectors"
                  << " bank=" << g_cdStreamTiming.sectorsPerBank << " sectors"
                  << std::endl;
        setReturnS32(ctx, 1);
    }

    void sceCdStStat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t buffered = bufferedCdStreamSectors(runtime);
        const uint32_t bankSize = std::max(1u, g_cdStreamTiming.sectorsPerBank);
        setReturnS32(ctx, static_cast<int32_t>((buffered / bankSize) * bankSize));
    }

    void sceCdStStop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        updateCdStreamProduction(runtime);
        g_cdStreamTiming.active = false;
        g_cdStreamTiming.paused = false;
        notifyMpegCdStreamEof(runtime);
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
