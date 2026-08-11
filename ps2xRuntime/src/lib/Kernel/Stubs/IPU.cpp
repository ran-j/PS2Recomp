#include "Common.h"
#include "IPU.h"
#include "runtime/ee_scheduler.h"

namespace
{
    constexpr uint32_t REG_IPU_CTRL = 0x10002010u;
    constexpr uint32_t REG_IPU_CMD = 0x10002000u;
    constexpr uint32_t REG_IPU_IN_FIFO = 0x10007010u;
    constexpr uint32_t IQVAL_BASE = 0x1721e0u;
    constexpr uint32_t VQVAL_BASE = 0x172230u;
    constexpr uint32_t SETD4_CHCR_ENTRY = 0x126428u;

    void completeIpuInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        PS2Memory &mem = runtime->memory();
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
}

namespace ps2_stubs
{
    void sceIpuInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
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

        if (!runtime->syncCoreSubsystems())
        {
            setReturnS32(ctx, -1);
            return;
        }

        if (runtime->hasFunction(SETD4_CHCR_ENTRY))
        {
            EeScheduler &scheduler = runtime->eeScheduler();
            scheduler.bindMainContextForSyscall(*ctx, rdram);
            GuestInvocation invocation{};
            invocation.kind = GuestInvocationKind::HleCall;
            invocation.context = *ctx;
            invocation.context.pc = SETD4_CHCR_ENTRY;
            SET_GPR_U32(&invocation.context, 4, 1u);
            SET_GPR_U32(&invocation.context, 29, 0u);
            SET_GPR_U32(&invocation.context, 31, 0u);
            invocation.onComplete = [rdram, runtime](const R5900Context &, R5900Context &parent)
            {
                completeIpuInit(rdram, &parent, runtime);
            };
            scheduler.invokeCurrent(std::move(invocation));
        }

        completeIpuInit(rdram, ctx, runtime);
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
