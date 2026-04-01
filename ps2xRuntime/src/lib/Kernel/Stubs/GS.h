#pragma once

#include "ps2_stubs.h"

namespace ps2_stubs
{
    void resetGsSyncVCallbackState();
    void dispatchGsSyncVCallback(uint8_t *rdram, PS2Runtime *runtime, uint64_t tick);
    void sceGsExecLoadImage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceGsExecStoreImage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceGsGetGParam(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceGsPutDispEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceGsPutDrawEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceGsResetGraph(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceGsResetPath(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceGsSetDefClear(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceGsSetDefDBuffDc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceGsSetDefDispEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceGsSetDefDrawEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceGsSetDefDrawEnv2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceGsSetDefLoadImage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceGsSetDefStoreImage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceGsSwapDBuffDc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceGsSyncPath(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceGsSyncV(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceGsSyncVCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceGszbufaddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void Ps2SwapDBuff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
