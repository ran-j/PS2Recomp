#pragma once

#include "ps2_stubs.h"

namespace ps2_stubs
{
    void sceFsDbChk(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceFsIntrSigSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceFsSemExit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceFsSemInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceFsSigSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void close(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void fstat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void lseek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void open(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void read(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceClose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceFsInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceFsReset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceIoctl(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceLseek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceOpen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceWrite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void stat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void write(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void cvFsSetDefDev(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
