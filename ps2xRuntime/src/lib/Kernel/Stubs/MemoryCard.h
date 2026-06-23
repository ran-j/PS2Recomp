#pragma once

#include "ps2_stubs.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ps2_stubs
{

    struct MemoryCardDebugOpenFile
    {
        int32_t fd = 0;
        int32_t port = 0;
        std::filesystem::path hostPath;
    };

    struct MemoryCardDebugPort
    {
        int32_t port = 0;
        std::string currentDir;
        bool formatted = false;
        std::filesystem::path rootPath;
    };

    struct MemoryCardDebugSnapshot
    {
        int32_t nextFd = 0;
        int32_t lastCmd = 0;
        int32_t lastResult = 0;
        int32_t cvFileCursor = 0;
        std::array<MemoryCardDebugPort, 2> ports{};
        std::vector<MemoryCardDebugOpenFile> openFiles;
    };

    MemoryCardDebugSnapshot getMemoryCardDebugSnapshot();
    void sceMcChangeThreadPriority(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMcChdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMcClose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMcDelete(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMcEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMcFlush(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMcFormat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMcGetDir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMcGetEntSpace(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMcGetInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMcGetSlotMax(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMcInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMcMkdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMcOpen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMcRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMcRename(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMcSeek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMcSetFileInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMcSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMcUnformat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMcWrite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcCallMessageTypeSe(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcCheckReadStartConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcCheckReadStartSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcCheckWriteStartConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcCheckWriteStartSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcCreateConfigInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcCreateFileSelectWindow(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcCreateIconInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcCreateSaveFileInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcDispFileName(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcDispFileNumber(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcDisplayFileSelectWindow(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcDisplaySelectFileInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcDisplaySelectFileInfoMesCount(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcDispWindowCurSol(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcDispWindowFoundtion(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mceGetInfoApdx(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mceIntrReadFixAlign(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mceStorePwd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcGetConfigCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcGetFileSelectWindowCursol(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcGetFreeCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcGetIconCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcGetIconFileCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcGetPortSelectDirInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcGetSaveFileCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcGetStringEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcMoveFileSelectWindowCursor(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcNewCreateConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcNewCreateIcon(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcNewCreateSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcReadIconData(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcReadStartConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcReadStartSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcSelectFileInfoInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcSelectSaveFileCheck(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcSetFileSelectWindowCursol(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcSetFileSelectWindowCursolInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcSetStringSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcSetTyepWriteMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcWriteIconData(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcWriteStartConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mcWriteStartSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
