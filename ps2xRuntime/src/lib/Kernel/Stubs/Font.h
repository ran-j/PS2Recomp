#pragma once

#include "ps2_stubs.h"

namespace ps2_stubs
{
    void sceeFontInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceeFontLoadFont(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceeFontGenerateString(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceeFontPrintfAt(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceeFontPrintfAt2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceeFontClose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceeFontSetColour(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceeFontSetMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceeFontSetFont(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceeFontSetScale(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
