#pragma once

#include "ps2_stubs.h"

namespace ps2_stubs
{
    void scePrintf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceResetttyinit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceTtyHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceTtyInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceTtyRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceTtyWrite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
