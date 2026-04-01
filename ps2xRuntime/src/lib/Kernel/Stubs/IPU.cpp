#include "Common.h"
#include "IPU.h"

namespace ps2_stubs
{
void sceIpuInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static constexpr uint32_t REG_IPU_CTRL = 0x10002010u;
    static constexpr uint32_t REG_IPU_CMD = 0x10002000u;
    static constexpr uint32_t REG_IPU_IN_FIFO = 0x10007010u;
    static constexpr uint32_t IQVAL_BASE = 0x1721e0u;
    static constexpr uint32_t VQVAL_BASE = 0x172230u;
    static constexpr uint32_t SETD4_CHCR_ENTRY = 0x126428u;

    if (!runtime)
        return;

    if (!runtime->memory().getRDRAM())
    {
        if (!runtime->memory().initialize())
        {
            setReturnS32(ctx, -1);
            return;
        }
    }

    if (!runtime->ensureCoreSubsystemsInitialized())
    {
        setReturnS32(ctx, -1);
        return;
    }

    PS2Memory &mem = runtime->memory();

    if (runtime->hasFunction(SETD4_CHCR_ENTRY))
    {
        auto setD4 = runtime->lookupFunction(SETD4_CHCR_ENTRY);
        ctx->r[4] = _mm_set_epi64x(0, 1);
        {
            PS2Runtime::GuestExecutionScope guestExecution(runtime);
            setD4(rdram, ctx, runtime);
        }
    }

    mem.write32(REG_IPU_CTRL, 0x40000000u);
    mem.write32(REG_IPU_CMD, 0u);

    __m128i v;
    v = runtime->Load128(rdram, ctx, IQVAL_BASE + 0x00u);
    mem.write128(REG_IPU_IN_FIFO, v);
    v = runtime->Load128(rdram, ctx, IQVAL_BASE + 0x10u);
    mem.write128(REG_IPU_IN_FIFO, v);
    v = runtime->Load128(rdram, ctx, IQVAL_BASE + 0x20u);
    mem.write128(REG_IPU_IN_FIFO, v);
    v = runtime->Load128(rdram, ctx, IQVAL_BASE + 0x30u);
    mem.write128(REG_IPU_IN_FIFO, v);
    v = runtime->Load128(rdram, ctx, IQVAL_BASE + 0x40u);
    mem.write128(REG_IPU_IN_FIFO, v);
    mem.write128(REG_IPU_IN_FIFO, v);
    mem.write128(REG_IPU_IN_FIFO, v);
    mem.write128(REG_IPU_IN_FIFO, v);

    mem.write32(REG_IPU_CMD, 0x50000000u);
    mem.write32(REG_IPU_CMD, 0x58000000u);

    v = runtime->Load128(rdram, ctx, VQVAL_BASE + 0x00u);
    mem.write128(REG_IPU_IN_FIFO, v);
    v = runtime->Load128(rdram, ctx, VQVAL_BASE + 0x10u);
    mem.write128(REG_IPU_IN_FIFO, v);

    mem.write32(REG_IPU_CMD, 0x60000000u);
    mem.write32(REG_IPU_CMD, 0x90000000u);

    mem.write32(REG_IPU_CTRL, 0x40000000u);
    mem.write32(REG_IPU_CMD, 0u);

    setReturnS32(ctx, 0);
}


void sceIpuRestartDMA(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}


void sceIpuStopDMA(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}


void sceIpuSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}
}
