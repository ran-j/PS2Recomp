#pragma once

#include "ps2_stubs.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ps2_stubs
{

    struct CdDebugFileEntry
    {
        std::string key;
        std::filesystem::path hostPath;
        uint32_t sizeBytes = 0;
        uint32_t baseLbn = 0;
        uint32_t sectors = 0;
    };

    struct CdDebugSnapshot
    {
        bool initialized = false;
        int32_t lastError = 0;
        uint32_t mode = 0;
        uint32_t streamingLbn = 0;
        uint32_t streamingEndLbn = 0;
        uint32_t nextPseudoLbn = 0;
        uint64_t imageSizeBytes = 0;
        bool imageSizeValid = false;
        std::filesystem::path cdRoot;
        std::filesystem::path cdImage;
        std::filesystem::path imageSizePath;
        std::filesystem::path leafIndexRoot;
        bool leafIndexBuilt = false;
        size_t leafIndexCount = 0;
        size_t loosePathIndexCount = 0;
        std::vector<CdDebugFileEntry> files;
    };

    CdDebugSnapshot getCdDebugSnapshot();
    void sceCdRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdGetError(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdRI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdRM(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdApplyNCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdBreak(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdChangeThreadPriority(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdDelayThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdDiskReady(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdGetDiskType(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdGetReadPos(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdGetToc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdInitEeCB(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdIntToPos(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdMmode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdNcmdDiskReady(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdPause(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdPosToInt(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdReadChain(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdReadClock(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdReadIOPm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdSearchFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdSeek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdStandby(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdStInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdStop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdStPause(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdStRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdStream(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdStResume(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdStSeek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdStSeekF(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdStStart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdStStat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdStStop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdSyncS(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdTrayReq(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
