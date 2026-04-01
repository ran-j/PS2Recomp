#include "Common.h"
#include "DMA.h"

namespace ps2_stubs
{
    void DmaAddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnU32(ctx, getRegU32(ctx, 4));
    }

    void sceDmaCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaCallback", rdram, ctx, runtime);
    }

    void sceDmaDebug(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaDebug", rdram, ctx, runtime);
    }

    void sceDmaGetChan(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t chanArg = getRegU32(ctx, 4);
        const uint32_t channelBase = resolveDmaChannelBase(rdram, chanArg);
        setReturnU32(ctx, channelBase);
    }

    void sceDmaGetEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaGetEnv", rdram, ctx, runtime);
    }

    void sceDmaLastSyncTime(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaLastSyncTime", rdram, ctx, runtime);
    }

    void sceDmaPause(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaPause", rdram, ctx, runtime);
    }

    void sceDmaPutEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaPutEnv", rdram, ctx, runtime);
    }

    void sceDmaPutStallAddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaPutStallAddr", rdram, ctx, runtime);
    }

    void sceDmaRecv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaRecv", rdram, ctx, runtime);
    }

    void sceDmaRecvI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaRecvI", rdram, ctx, runtime);
    }

    void sceDmaRecvN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaRecvN", rdram, ctx, runtime);
    }

    void sceDmaReset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceDmaRestart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaRestart", rdram, ctx, runtime);
    }

    void sceDmaSend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, submitDmaSend(rdram, ctx, runtime, false));
    }

    void sceDmaSendI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, submitDmaSend(rdram, ctx, runtime, false));
    }

    void sceDmaSendM(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, submitDmaSend(rdram, ctx, runtime, false));
    }

    void sceDmaSendN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, submitDmaSend(rdram, ctx, runtime, true));
    }

    void sceDmaSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, submitDmaSync(rdram, ctx, runtime));
    }

    void sceDmaSyncN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, submitDmaSync(rdram, ctx, runtime));
    }

    void sceDmaWatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaWatch", rdram, ctx, runtime);
    }
}
