#pragma once

#include "ps2_stubs.h"

#include <cstddef>
#include <cstdint>

namespace ps2_stubs
{
    void PadSyncCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadEnterPressMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadExitPressMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadGetButtonMask(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadGetDmaStr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadGetFrameCount(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadGetModVersion(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadGetPortMax(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadGetReqState(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadGetSlotMax(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadGetState(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadInfoAct(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadInfoComb(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadInfoMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadInfoPressMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadInit2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadPortClose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadPortOpen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadReqIntToStr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadSetActAlign(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadSetActDirect(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadSetButtonInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadSetMainMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadSetReqState(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadSetVrefParam(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadSetWarningLevel(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadStateIntToStr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    static constexpr size_t kPadDebugPortCount = 2u;
    static constexpr size_t kPadDebugSlotCount = 1u;
    static constexpr size_t kPadDebugDataSize = 32u;

    struct PadDebugPortSnapshot
    {
        bool open = false;
        bool analogMode = false;
        bool pressureEnabled = false;
        bool lastUsedOverride = false;
        bool lastUsedBackend = false;
        bool lastReadOk = false;
        uint16_t buttonMask = 0xFFFFu;
        uint16_t lastButtons = 0xFFFFu;
        uint32_t dmaAddr = 0u;
        uint32_t reqState = 0u;
        uint32_t readCount = 0u;
        uint32_t lastReadDataAddr = 0u;
        uint8_t rx = 0x80u;
        uint8_t ry = 0x80u;
        uint8_t lx = 0x80u;
        uint8_t ly = 0x80u;
        uint8_t lastData[kPadDebugDataSize]{};
    };

    struct PadDebugSnapshot
    {
        bool overrideEnabled = false;
        uint16_t overrideButtons = 0xFFFFu;
        uint8_t overrideRx = 0x80u;
        uint8_t overrideRy = 0x80u;
        uint8_t overrideLx = 0x80u;
        uint8_t overrideLy = 0x80u;
        int readLogCount = 0;
        PadDebugPortSnapshot ports[kPadDebugPortCount][kPadDebugSlotCount]{};
    };

    PadDebugSnapshot getPadDebugSnapshot();
    void setPadOverrideState(uint16_t buttons, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry);
    void clearPadOverrideState();
}
