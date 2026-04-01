#pragma once

#include "ps2_stubs.h"

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
    void setPadOverrideState(uint16_t buttons, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry);
    void clearPadOverrideState();
}
