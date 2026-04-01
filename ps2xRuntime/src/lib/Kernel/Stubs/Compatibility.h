#pragma once

#include "ps2_stubs.h"

namespace ps2_stubs
{
    void calloc_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void ret0(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void ret1(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void reta0(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void free_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void malloc_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void malloc_trim_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void mbtowc_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void printf_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
