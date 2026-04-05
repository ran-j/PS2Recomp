#pragma once

#include "ps2_stubs.h"

namespace ps2_stubs
{
    void sceRpcFreePacket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceRpcGetFPacket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceRpcGetFPacket2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
