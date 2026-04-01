#pragma once

#include "ps2_stubs.h"

namespace ps2_stubs
{
    void builtin_set_imask(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceIDC(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceSDC(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void exit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void getpid(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceSetBrokenLink(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceSetPtm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDevVif0Reset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDevVu0Reset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
