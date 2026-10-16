#include "Common.h"
#include "IPU.h"

namespace
{
    constexpr uint32_t REG_IPU_CTRL = 0x10002010u;
    constexpr uint32_t REG_IPU_CMD = 0x10002000u;
    constexpr uint32_t REG_IPU_IN_FIFO = 0x10007010u;

    // libipu's sceIpuInit tables: the MPEG default intra quantiser matrix in
    // zigzag order, the flat-16 non-intra matrix, and the default VQ CLUT.
    // Byte-identical to Stuntman's own copies at 0x00273B00 / 0x00273B50
    // (guest sceIpuInit@0x00246308). They were previously read from guest
    // addresses 0x001721E0 / 0x00172230 — another title's table locations,
    // which in Stuntman are MIPS code inside sub_1721E0 — so the IPU decoded
    // with code bytes as quantiser matrices and VQ palette.
    alignas(16) constexpr uint8_t kIntraQuantMatrix[64] = {
        0x08, 0x10, 0x10, 0x13, 0x10, 0x13, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x1a, 0x18, 0x1a, 0x1b,
        0x1b, 0x1b, 0x1a, 0x1a, 0x1a, 0x1a, 0x1b, 0x1b, 0x1b, 0x1d, 0x1d, 0x1d, 0x22, 0x22, 0x22, 0x1d,
        0x1d, 0x1d, 0x1b, 0x1b, 0x1d, 0x1d, 0x20, 0x20, 0x22, 0x22, 0x25, 0x26, 0x25, 0x23, 0x23, 0x22,
        0x23, 0x26, 0x26, 0x28, 0x28, 0x28, 0x30, 0x30, 0x2e, 0x2e, 0x38, 0x38, 0x3a, 0x45, 0x45, 0x53,
    };
    alignas(16) constexpr uint8_t kNonIntraQuantRow[16] = {
        0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    };
    alignas(16) constexpr uint8_t kVqClut[32] = {
        0x00, 0x00, 0x21, 0x04, 0x42, 0x08, 0xe0, 0x03, 0x84, 0x10, 0xa5, 0x14, 0xc6, 0x18, 0xe7, 0x1c,
        0x1f, 0x00, 0x29, 0x25, 0x4a, 0x29, 0x00, 0x7c, 0x8c, 0x31, 0xad, 0x35, 0xff, 0x7f, 0xce, 0x39,
    };

    __m128i loadQword(const uint8_t *bytes)
    {
        return _mm_load_si128(reinterpret_cast<const __m128i *>(bytes));
    }

    // Same register sequence as the guest sceIpuInit: reset, SETIQ intra +
    // non-intra from 128 FIFO bytes, SETVQ from 32, SETTH, reset.
    void completeIpuInit(R5900Context *ctx, PS2Runtime *runtime)
    {
        PS2Memory &mem = runtime->memory();
        mem.write32(REG_IPU_CTRL, 0x40000000u);
        mem.write32(REG_IPU_CMD, 0u);

        for (uint32_t offset = 0; offset < sizeof(kIntraQuantMatrix); offset += 16u)
            mem.write128(REG_IPU_IN_FIFO, loadQword(kIntraQuantMatrix + offset));
        for (int row = 0; row < 4; ++row)
            mem.write128(REG_IPU_IN_FIFO, loadQword(kNonIntraQuantRow));

        mem.write32(REG_IPU_CMD, 0x50000000u);
        mem.write32(REG_IPU_CMD, 0x58000000u);

        mem.write128(REG_IPU_IN_FIFO, loadQword(kVqClut));
        mem.write128(REG_IPU_IN_FIFO, loadQword(kVqClut + 16));

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

        // No guest "kick" call here: a previous version invoked a hardcoded
        // guest address (0x00126428) before completing init. That address is
        // title-specific -- in Stuntman (SLUS-20250) it lands mid-sub_126110
        // (pad poll), whose tail JALR through *(this+164)+20 then faulted on
        // the caller's unrelated heap object. Per-title guest entry points
        // belong in game_overrides.h, never as unconditional constants in a
        // shared stub. sceIpuInit is HLE: complete the init directly.
        completeIpuInit(ctx, runtime);
    }

    void sceIpuRestartDMA(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceIpuStopDMA(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    // Guest sceIpuSync@0x00246238: mode 0 polls until CTRL.BUSY clears and
    // returns 0; mode 1 returns CTRL.BUSY; any other mode returns 0. Mode 1
    // now reads the real busy bit; mode 0 still returns immediately, as
    // before, rather than spinning inside the stub.
    void sceIpuSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t mode = getRegU32(ctx, 4);
        setReturnS32(ctx, mode == 1u ? static_cast<int32_t>(runtime->memory().read32(REG_IPU_CTRL) >> 31) : 0);
    }
}
