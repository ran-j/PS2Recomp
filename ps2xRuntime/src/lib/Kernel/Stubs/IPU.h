#pragma once

#include "ps2_stubs.h"

namespace ps2_stubs
{
    void sceIpuInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceIpuRestartDMA(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceIpuStopDMA(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceIpuSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
