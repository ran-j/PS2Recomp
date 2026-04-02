#include "Common.h"
#include "DMA.h"

namespace ps2_stubs
{
    namespace
    {
        struct SceDmaEnv
        {
            uint8_t sts = 0;
            uint8_t std = 0;
            uint8_t mfd = 0;
            uint8_t rele = 0;
            uint32_t pcr = 0;
            uint32_t sqwc = 0;
            uint32_t rbor = 0;
            uint32_t rbsr = 0;
        };

        static_assert(sizeof(SceDmaEnv) == 0x14, "sceDmaEnv must match the guest ABI");

        constexpr uint32_t DMA_REG_CTRL = 0x1000E000u;
        constexpr uint32_t DMA_REG_PCR = 0x1000E020u;
        constexpr uint32_t DMA_REG_SQWC = 0x1000E030u;
        constexpr uint32_t DMA_REG_RBSR = 0x1000E040u;
        constexpr uint32_t DMA_REG_RBOR = 0x1000E050u;
        constexpr uint32_t DMA_REG_STADR = 0x1000E060u;

        constexpr std::array<uint8_t, 10> kStsTable = {0u, 0u, 0u, 3u, 0u, 1u, 0u, 0u, 2u, 0u};
        constexpr std::array<uint8_t, 10> kStdTable = {0u, 1u, 2u, 0u, 0u, 0u, 3u, 0u, 0u, 0u};
        constexpr std::array<uint8_t, 10> kMfdTable = {0u, 2u, 3u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};

        std::mutex g_dmaEnvMutex;
        SceDmaEnv g_dmaCurrentEnv;
    }

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
        const uint32_t envAddr = getRegU32(ctx, 4);
        if (uint8_t *dst = getMemPtr(rdram, envAddr))
        {
            std::lock_guard<std::mutex> lock(g_dmaEnvMutex);
            std::memcpy(dst, &g_dmaCurrentEnv, sizeof(g_dmaCurrentEnv));
        }
        setReturnU32(ctx, envAddr);
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
        const uint32_t envAddr = getRegU32(ctx, 4);
        const uint8_t *src = getConstMemPtr(rdram, envAddr);
        if (!src || !runtime)
        {
            setReturnS32(ctx, -1);
            return;
        }

        SceDmaEnv env{};
        std::memcpy(&env, src, sizeof(env));

        if (env.sts >= kStsTable.size())
        {
            setReturnS32(ctx, -1);
            return;
        }
        if (env.std >= kStdTable.size())
        {
            setReturnS32(ctx, -2);
            return;
        }
        if (env.mfd >= kMfdTable.size())
        {
            setReturnS32(ctx, -3);
            return;
        }
        if (env.rele >= 7u)
        {
            setReturnS32(ctx, -4);
            return;
        }

        PS2Memory &mem = runtime->memory();
        uint32_t ctrl = mem.readIORegister(DMA_REG_CTRL);
        ctrl = (ctrl & 0xFFFFFFCFu) | (static_cast<uint32_t>(kStsTable[env.sts]) << 4);
        ctrl = (ctrl & 0xFFFFFF3Fu) | (static_cast<uint32_t>(kStdTable[env.std]) << 6);
        ctrl = (ctrl & 0xFFFFFFF3u) | (static_cast<uint32_t>(kMfdTable[env.mfd]) << 2);
        if (env.rele == 0u)
        {
            ctrl &= 0xFFFFFFFDu;
        }
        else
        {
            ctrl = ((ctrl | 0x2u) & 0xFFFFFCFFu) | ((static_cast<uint32_t>(env.rele - 1u) & 0x7u) << 8);
        }

        mem.writeIORegister(DMA_REG_CTRL, ctrl);
        mem.writeIORegister(DMA_REG_PCR, env.pcr);
        mem.writeIORegister(DMA_REG_SQWC, env.sqwc);
        mem.writeIORegister(DMA_REG_RBOR, env.rbor);
        mem.writeIORegister(DMA_REG_RBSR, env.rbsr);

        {
            std::lock_guard<std::mutex> lock(g_dmaEnvMutex);
            g_dmaCurrentEnv = env;
        }

        setReturnS32(ctx, 0);
    }

    void sceDmaPutStallAddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t newAddr = getRegU32(ctx, 4);
        uint32_t oldAddr = 0;
        if (runtime)
        {
            PS2Memory &mem = runtime->memory();
            oldAddr = mem.readIORegister(DMA_REG_STADR);
            if (newAddr != 0xFFFFFFFFu)
            {
                mem.writeIORegister(DMA_REG_STADR, newAddr);
            }
        }
        setReturnU32(ctx, oldAddr);
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
