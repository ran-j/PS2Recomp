#include "MiniTest.h"
#include "ps2_memory.h"
#include "ps2_runtime.h"
#include "ps2_stubs.h"
#include "ps2_syscalls.h"
#include "ps2_gs_gpu.h"
#include "ps2_gs_psmt4.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

using namespace ps2_syscalls;

namespace
{
    std::atomic<uint32_t> g_gsSyncCallbackHits{0u};
    std::atomic<uint32_t> g_gsSyncCallbackLastTick{0u};

    void setRegU32(R5900Context &ctx, int reg, uint32_t value)
    {
        ctx.r[reg] = _mm_set_epi64x(0, static_cast<int64_t>(value));
    }

    uint32_t getRegU32Test(const R5900Context &ctx, int reg)
    {
        return ::getRegU32(&ctx, reg);
    }

    uint64_t getReturnU64(const R5900Context &ctx)
    {
        const uint64_t lo = static_cast<uint64_t>(getRegU32Test(ctx, 2));
        const uint64_t hi = static_cast<uint64_t>(getRegU32Test(ctx, 3));
        return lo | (hi << 32);
    }

    uint64_t makeGifTag(uint16_t nloop, uint8_t flg, uint8_t nreg, bool eop = true)
    {
        uint64_t tag = static_cast<uint64_t>(nloop & 0x7FFFu);
        if (eop)
            tag |= (1ull << 15);
        tag |= (static_cast<uint64_t>(flg & 0x3u) << 58);
        tag |= (static_cast<uint64_t>(nreg & 0xFu) << 60);
        return tag;
    }

    void appendU64(std::vector<uint8_t> &dst, uint64_t value)
    {
        const size_t pos = dst.size();
        dst.resize(pos + sizeof(uint64_t));
        std::memcpy(dst.data() + pos, &value, sizeof(uint64_t));
    }

    template <typename Predicate>
    bool waitUntil(Predicate pred, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (pred())
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        return pred();
    }

    void testGsSyncVCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;

        g_gsSyncCallbackLastTick.store(getRegU32(ctx, 4), std::memory_order_relaxed);
        g_gsSyncCallbackHits.fetch_add(1u, std::memory_order_relaxed);
        ctx->pc = 0u;
    }
}

void register_ps2_gs_tests()
{
    MiniTest::Case("PS2GS", [](TestCase &tc)
    {
        tc.Run("GS CSR/IMR support coherent 64-bit and 32-bit access", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            constexpr uint32_t kGsCsr = 0x12001000u;
            constexpr uint32_t kGsImr = 0x12001010u;

            const uint64_t csrPattern = 0xA1B2C3D4E5F60718ull;
            mem.write64(kGsCsr, csrPattern);
            t.Equals(mem.read64(kGsCsr), csrPattern, "64-bit CSR read should match prior 64-bit write");
            t.Equals(mem.read32(kGsCsr), static_cast<uint32_t>(csrPattern & 0xFFFFFFFFull), "CSR low dword read should match");
            t.Equals(mem.read32(kGsCsr + 4u), static_cast<uint32_t>(csrPattern >> 32), "CSR high dword read should match");

            mem.write32(kGsCsr, 0x11223344u);
            t.Equals(mem.read64(kGsCsr), 0xA1B2C3D411223344ull, "32-bit low write should preserve CSR high dword");

            mem.write32(kGsCsr + 4u, 0x55667788u);
            t.Equals(mem.read64(kGsCsr), 0x5566778811223344ull, "32-bit high write should preserve CSR low dword");

            const uint64_t imrPattern = 0x0123456789ABCDEFull;
            mem.write64(kGsImr, imrPattern);
            t.Equals(mem.read64(kGsImr), imrPattern, "IMR 64-bit read should match prior write");
            t.Equals(mem.read32(kGsImr), 0x89ABCDEFu, "IMR low dword should match");
            t.Equals(mem.read32(kGsImr + 4u), 0x01234567u, "IMR high dword should match");
        });

        tc.Run("unknown GS privileged offsets are no-op and read as zero", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            constexpr uint32_t kKnownBusdir = 0x12001040u;
            constexpr uint32_t kUnknown = 0x12001008u; // inside GS priv range, but not mapped by gsRegPtr.

            mem.write64(kKnownBusdir, 0xCAFEBABE12345678ull);
            const uint64_t before = mem.read64(kKnownBusdir);
            mem.write32(kUnknown, 0xDEADBEEFu);
            t.Equals(mem.read32(kUnknown), 0u, "unknown GS offset should read as zero");
            t.Equals(mem.read64(kKnownBusdir), before, "unknown GS writes should not corrupt mapped GS registers");
        });

        tc.Run("GS writeIORegister increments GS write counter", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            constexpr uint32_t kGsPmode = 0x12000000u;
            constexpr uint32_t kGsImr = 0x12001010u;

            const uint64_t countBefore = mem.gsWriteCount();
            t.IsTrue(mem.writeIORegister(kGsPmode, 0x11u), "writeIORegister PMODE should succeed");
            t.IsTrue(mem.writeIORegister(kGsImr, 0x22u), "writeIORegister IMR should succeed");
            t.Equals(mem.gsWriteCount(), countBefore + 2ull, "GS IO writes should increment GS write counter");

            t.Equals(mem.readIORegister(kGsPmode), 0x11u, "writeIORegister PMODE value should be readable");
            t.Equals(mem.readIORegister(kGsImr), 0x22u, "writeIORegister IMR value should be readable");
        });

        tc.Run("GsPutIMR and GsGetIMR roundtrip old and new values", [](TestCase &t)
        {
            PS2Runtime runtime;
            t.IsTrue(runtime.memory().initialize(), "runtime memory initialize should succeed");
            runtime.memory().gs().imr = 0xAAAABBBBCCCCDDDDull;

            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            R5900Context ctx{};

            setRegU32(ctx, 4, 0x11112222u); // new IMR low
            setRegU32(ctx, 5, 0x33334444u); // new IMR high
            GsPutIMR(rdram.data(), &ctx, &runtime);

            const uint64_t oldImr = getReturnU64(ctx);
            t.Equals(oldImr, 0xAAAABBBBCCCCDDDDull, "GsPutIMR should return previous IMR");
            t.Equals(runtime.memory().gs().imr, 0x3333444411112222ull, "GsPutIMR should update GS IMR");

            std::memset(&ctx, 0, sizeof(ctx));
            GsGetIMR(rdram.data(), &ctx, &runtime);
            const uint64_t currentImr = getReturnU64(ctx);
            t.Equals(currentImr, 0x3333444411112222ull, "GsGetIMR should return current GS IMR");
        });

        tc.Run("sceGsSetDefDBuffDc seeds display envs and swap applies the selected page", [](TestCase &t)
        {
            PS2Runtime runtime;
            t.IsTrue(runtime.memory().initialize(), "runtime memory initialize should succeed");

            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            constexpr uint32_t kEnvAddr = 0x4000u;
            constexpr uint32_t kDispEnvSize = 40u;
            constexpr uint32_t kDBuffSize = 0x330u;
            constexpr uint32_t kDispFbOffset = 16u;
            constexpr uint32_t kDisplayOffset = 24u;
            constexpr uint32_t kDraw01Offset = 0x60u;
            constexpr uint32_t kFrame1Offset = kDraw01Offset + 0x00u;
            constexpr uint32_t kFrame1AddrOffset = kDraw01Offset + 0x08u;
            constexpr uint32_t kXYOffset1Offset = kDraw01Offset + 0x20u;
            constexpr uint32_t kXYOffset1AddrOffset = kDraw01Offset + 0x28u;

            R5900Context ctx{};
            setRegU32(ctx, 4, kEnvAddr);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 640u);
            setRegU32(ctx, 7, 448u);
            std::memset(rdram.data() + kEnvAddr, 0xCD, kDBuffSize);
            ps2_stubs::sceGsSetDefDBuffDc(rdram.data(), &ctx, &runtime);

            uint64_t dispfb0 = 0u;
            uint64_t display0 = 0u;
            uint64_t frame10 = 0u;
            uint64_t frame10Addr = 0u;
            uint64_t xyoffset10 = 0u;
            uint64_t xyoffset10Addr = 0u;
            std::memcpy(&dispfb0, rdram.data() + kEnvAddr + kDispFbOffset, sizeof(dispfb0));
            std::memcpy(&display0, rdram.data() + kEnvAddr + kDisplayOffset, sizeof(display0));
            std::memcpy(&frame10, rdram.data() + kEnvAddr + kFrame1Offset, sizeof(frame10));
            std::memcpy(&frame10Addr, rdram.data() + kEnvAddr + kFrame1AddrOffset, sizeof(frame10Addr));
            std::memcpy(&xyoffset10, rdram.data() + kEnvAddr + kXYOffset1Offset, sizeof(xyoffset10));
            std::memcpy(&xyoffset10Addr, rdram.data() + kEnvAddr + kXYOffset1AddrOffset, sizeof(xyoffset10Addr));

            t.Equals((dispfb0 >> 9) & 0x3Fu, 10ull, "dbuff display env should seed FBW from width");
            t.Equals((display0 >> 32) & 0x0FFFull, 639ull, "dbuff display env should seed DW from width");
            t.Equals((display0 >> 44) & 0x07FFull, 447ull, "dbuff display env should seed DH from height");
            t.Equals((frame10 >> 16) & 0x3Full, 10ull, "dbuff draw env should seed FRAME FBW from width");
            t.Equals(frame10Addr, 0x4Cull, "dbuff draw env should seed FRAME_1 register id");
            t.Equals(xyoffset10 & 0xFFFFull, 0x6C00ull, "dbuff draw env should seed OFX in 12.4 fixed point");
            t.Equals((xyoffset10 >> 32) & 0xFFFFull, 0x7200ull, "dbuff draw env should seed OFY in 12.4 fixed point");
            t.Equals(xyoffset10Addr, 0x18ull, "dbuff draw env should seed XYOFFSET_1 register id");

            dispfb0 = (dispfb0 & ~0x1FFull) | 150ull;
            std::memcpy(rdram.data() + kEnvAddr + kDispFbOffset, &dispfb0, sizeof(dispfb0));

            uint64_t dispfb1 = dispfb0;
            dispfb1 = (dispfb1 & ~0x1FFull) | 151ull;
            std::memcpy(rdram.data() + kEnvAddr + kDispEnvSize + kDispFbOffset, &dispfb1, sizeof(dispfb1));

            std::memset(&ctx, 0, sizeof(ctx));
            setRegU32(ctx, 4, kEnvAddr);
            setRegU32(ctx, 5, 1u);
            ps2_stubs::sceGsSwapDBuffDc(rdram.data(), &ctx, &runtime);

            t.Equals(runtime.memory().gs().dispfb1 & 0x1FFull, 151ull,
                     "sceGsSwapDBuffDc should program GS to the selected display page");
            t.Equals((runtime.memory().gs().display1 >> 32) & 0x0FFFull, 639ull,
                     "sceGsSwapDBuffDc should preserve the display width from the seeded env");
        });

        tc.Run("GIF PACKED A+D writes DISPFB1 and DISPLAY1 privileged registers", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GSRegisters regs{};
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), &regs);

            std::vector<uint8_t> packet;
            appendU64(packet, makeGifTag(2u, GIF_FMT_PACKED, 1u, true));
            appendU64(packet, 0x0Eull); // REGS[0] = A+D

            const uint64_t dispfb1 = 0x0123456789ABCDEFull;
            const uint64_t display1 = 0x1111222233334444ull;
            appendU64(packet, dispfb1);
            appendU64(packet, 0x59ull); // DISPFB1
            appendU64(packet, display1);
            appendU64(packet, 0x5Aull); // DISPLAY1

            gs.processGIFPacket(packet.data(), static_cast<uint32_t>(packet.size()));

            t.Equals(regs.dispfb1, dispfb1, "A+D should write GS DISPFB1");
            t.Equals(regs.display1, display1, "A+D should write GS DISPLAY1");
        });

        tc.Run("PSMT4 address mapping matches Veronica Conv4to32 layout", [](TestCase &t)
        {
            constexpr uint32_t kBaseBlock = 0u;
            constexpr uint32_t kWidth = 2u; // One 128x128 PSMT4 page.

            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 0u, 0u), 0u,
                     "PSMT4 origin should map to nibble offset 0");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 1u, 0u), 8u,
                     "PSMT4 x=1 should advance to the next packed nibble group");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 0u, 1u), 512u,
                     "PSMT4 second source row should land on the next CT32 row stride");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 0u, 2u), 33u,
                     "PSMT4 third source row should keep Veronica's interleaved ordering");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 0u, 3u), 545u,
                     "PSMT4 fourth source row should include both interleave and CT32 row stride");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 31u, 15u), 3647u,
                     "PSMT4 final texel in the first 32x16 block should match Veronica's block layout");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 32u, 0u), 4096u,
                     "PSMT4 x=32 should advance to the next CT32 block column");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 32u, 16u), 4160u,
                     "PSMT4 x=32,y=16 should include both block-column and block-row offsets");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 64u, 0u), 8192u,
                     "PSMT4 x=64 should advance to the third block column in the page");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 96u, 112u), 12736u,
                     "PSMT4 bottom-right block origin should match Veronica's page permutation");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 127u, 127u), 16383u,
                     "PSMT4 final texel in a 128x128 page should land at the end of the page");
        });

        tc.Run("GIF REGLIST with odd register count consumes 128-bit padding before next tag", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            const uint64_t bitblt =
                (static_cast<uint64_t>(0u) << 0) |
                (static_cast<uint64_t>(1u) << 16) |
                (static_cast<uint64_t>(0u) << 24) |
                (static_cast<uint64_t>(0u) << 32) |
                (static_cast<uint64_t>(1u) << 48) |
                (static_cast<uint64_t>(0u) << 56);
            gs.writeRegister(GS_REG_BITBLTBUF, bitblt);
            gs.writeRegister(GS_REG_TRXPOS, 0ull);
            gs.writeRegister(GS_REG_TRXREG, (4ull << 0) | (1ull << 32));
            gs.writeRegister(GS_REG_TRXDIR, 0ull);

            std::vector<uint8_t> packet;
            appendU64(packet, makeGifTag(1u, GIF_FMT_REGLIST, 1u, false));
            appendU64(packet, 0x0ull); // REGS[0] = PRIM
            appendU64(packet, 0x0000000000000006ull); // PRIM write
            appendU64(packet, 0xDEADBEEFCAFEBABEull); // required REGLIST pad qword

            appendU64(packet, makeGifTag(1u, GIF_FMT_IMAGE, 0u, true));
            appendU64(packet, 0ull);
            const uint8_t payload[16] = {
                0x31u, 0x32u, 0x33u, 0x34u,
                0x35u, 0x36u, 0x37u, 0x38u,
                0x39u, 0x3Au, 0x3Bu, 0x3Cu,
                0x3Du, 0x3Eu, 0x3Fu, 0x40u,
            };
            packet.insert(packet.end(), payload, payload + sizeof(payload));

            gs.processGIFPacket(packet.data(), static_cast<uint32_t>(packet.size()));

            bool imageOk = true;
            for (uint32_t i = 0; i < 16u; ++i)
            {
                if (vram[i] != payload[i])
                {
                    imageOk = false;
                    break;
                }
            }
            t.IsTrue(imageOk, "odd REGLIST payload should not corrupt alignment of the following IMAGE tag");
        });

        tc.Run("GIF REGLIST NREG=0 is treated as sixteen descriptors", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            const uint64_t bitblt =
                (static_cast<uint64_t>(0u) << 0) |
                (static_cast<uint64_t>(1u) << 16) |
                (static_cast<uint64_t>(0u) << 24) |
                (static_cast<uint64_t>(0u) << 32) |
                (static_cast<uint64_t>(1u) << 48) |
                (static_cast<uint64_t>(0u) << 56);
            gs.writeRegister(GS_REG_BITBLTBUF, bitblt);
            gs.writeRegister(GS_REG_TRXPOS, 0ull);
            gs.writeRegister(GS_REG_TRXREG, (4ull << 0) | (1ull << 32));
            gs.writeRegister(GS_REG_TRXDIR, 0ull);

            std::vector<uint8_t> packet;
            appendU64(packet, makeGifTag(1u, GIF_FMT_REGLIST, 0u, false)); // NREG=0 -> 16 regs
            appendU64(packet, 0ull); // 16x PRIM descriptors
            for (uint32_t i = 0; i < 16u; ++i)
            {
                appendU64(packet, static_cast<uint64_t>(i));
            }

            appendU64(packet, makeGifTag(1u, GIF_FMT_IMAGE, 0u, true));
            appendU64(packet, 0ull);
            const uint8_t payload[16] = {
                0x51u, 0x52u, 0x53u, 0x54u,
                0x55u, 0x56u, 0x57u, 0x58u,
                0x59u, 0x5Au, 0x5Bu, 0x5Cu,
                0x5Du, 0x5Eu, 0x5Fu, 0x60u,
            };
            packet.insert(packet.end(), payload, payload + sizeof(payload));

            gs.processGIFPacket(packet.data(), static_cast<uint32_t>(packet.size()));

            bool imageOk = true;
            for (uint32_t i = 0; i < 16u; ++i)
            {
                if (vram[i] != payload[i])
                {
                    imageOk = false;
                    break;
                }
            }
            t.IsTrue(imageOk, "NREG=0 REGLIST should consume 16 data words and keep following tag aligned");
        });

        tc.Run("GS SIGNAL and FINISH set CSR bits that clear by CSR write-one acknowledge", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            GS gs;
            gs.init(mem.getGSVRAM(), static_cast<uint32_t>(PS2_GS_VRAM_SIZE), &mem.gs());

            const uint64_t signalValue = (0xFFFFFFFFull << 32) | 0x11223344ull;
            gs.writeRegister(GS_REG_SIGNAL, signalValue);
            gs.writeRegister(GS_REG_FINISH, 0u);

            t.IsTrue((mem.gs().csr & 0x1ull) != 0ull, "SIGNAL should raise CSR.SIGNAL");
            t.IsTrue((mem.gs().csr & 0x2ull) != 0ull, "FINISH should raise CSR.FINISH");
            t.Equals(static_cast<uint32_t>(mem.gs().siglblid & 0xFFFFFFFFull), 0x11223344u, "SIGNAL should update SIGLBLID low dword");

            mem.write64(0x12001000u, 0x1ull);
            t.IsTrue((mem.gs().csr & 0x1ull) == 0ull, "writing CSR bit0 should acknowledge SIGNAL");
            t.IsTrue((mem.gs().csr & 0x2ull) != 0ull, "acknowledging SIGNAL should not clear FINISH");

            mem.write32(0x12001000u, 0x2u);
            t.IsTrue((mem.gs().csr & 0x2ull) == 0ull, "writing CSR bit1 should acknowledge FINISH");
        });

        tc.Run("GIF IMAGE packet writes host-to-local data into GS VRAM", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            // Setup for host->local transfer to DBP=0, DBW=1, PSMCT32, rect 2x2.
            const uint64_t bitblt =
                (static_cast<uint64_t>(0u) << 0) |      // SBP
                (static_cast<uint64_t>(1u) << 16) |     // SBW
                (static_cast<uint64_t>(0u) << 24) |     // SPSM
                (static_cast<uint64_t>(0u) << 32) |     // DBP
                (static_cast<uint64_t>(1u) << 48) |     // DBW
                (static_cast<uint64_t>(0u) << 56);      // DPSM (CT32)
            gs.writeRegister(GS_REG_BITBLTBUF, bitblt);
            gs.writeRegister(GS_REG_TRXPOS, 0ull);
            gs.writeRegister(GS_REG_TRXREG, (2ull << 0) | (2ull << 32));
            gs.writeRegister(GS_REG_TRXDIR, 0ull);

            std::vector<uint8_t> packet;
            appendU64(packet, makeGifTag(1u, GIF_FMT_IMAGE, 0u, true));
            appendU64(packet, 0ull);

            const uint8_t payload[16] = {
                0x10u, 0x11u, 0x12u, 0x13u,
                0x20u, 0x21u, 0x22u, 0x23u,
                0x30u, 0x31u, 0x32u, 0x33u,
                0x40u, 0x41u, 0x42u, 0x43u,
            };
            packet.insert(packet.end(), payload, payload + sizeof(payload));

            gs.processGIFPacket(packet.data(), static_cast<uint32_t>(packet.size()));

            bool same = true;
            for (size_t i = 0; i < 8u; ++i)
            {
                if (vram[i] != payload[i] || vram[256u + i] != payload[8u + i])
                {
                    same = false;
                    break;
                }
            }
            t.IsTrue(same, "GIF IMAGE transfer should write payload bytes into GS VRAM");
        });

        tc.Run("GS local-to-host transfer supports partial incremental reads", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            for (uint32_t i = 0; i < 16u; ++i)
            {
                vram[i] = static_cast<uint8_t>(0xA0u + i);
            }

            const uint64_t bitblt =
                (static_cast<uint64_t>(0u) << 0) |      // SBP
                (static_cast<uint64_t>(1u) << 16) |     // SBW
                (static_cast<uint64_t>(0u) << 24) |     // SPSM (CT32)
                (static_cast<uint64_t>(0u) << 32) |
                (static_cast<uint64_t>(1u) << 48) |
                (static_cast<uint64_t>(0u) << 56);
            gs.writeRegister(GS_REG_BITBLTBUF, bitblt);
            gs.writeRegister(GS_REG_TRXPOS, 0ull);
            gs.writeRegister(GS_REG_TRXREG, (4ull << 0) | (1ull << 32)); // 4 pixels, 1 row -> 16 bytes
            gs.writeRegister(GS_REG_TRXDIR, 1ull);

            uint8_t bufA[8] = {};
            uint8_t bufB[16] = {};

            const uint32_t nA = gs.consumeLocalToHostBytes(bufA, 6u);
            const uint32_t nB = gs.consumeLocalToHostBytes(bufB, 16u);
            const uint32_t nC = gs.consumeLocalToHostBytes(bufB, 4u);

            t.Equals(nA, 6u, "first partial read should consume requested bytes");
            t.Equals(nB, 10u, "second read should consume the remaining bytes");
            t.Equals(nC, 0u, "buffer should be empty after all bytes are consumed");

            bool bytesOk = true;
            for (uint32_t i = 0; i < 6u; ++i)
            {
                if (bufA[i] != static_cast<uint8_t>(0xA0u + i))
                    bytesOk = false;
            }
            for (uint32_t i = 0; i < 10u; ++i)
            {
                if (bufB[i] != static_cast<uint8_t>(0xA6u + i))
                    bytesOk = false;
            }
            t.IsTrue(bytesOk, "partial reads should return local->host data in-order");
        });

        tc.Run("GS CT24 host-local-host transfer preserves 24-bit RGB payload", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            const uint64_t bitblt =
                (static_cast<uint64_t>(0u) << 0) |      // SBP
                (static_cast<uint64_t>(1u) << 16) |     // SBW
                (static_cast<uint64_t>(1u) << 24) |     // SPSM CT24
                (static_cast<uint64_t>(0u) << 32) |     // DBP
                (static_cast<uint64_t>(1u) << 48) |     // DBW
                (static_cast<uint64_t>(1u) << 56);      // DPSM CT24
            gs.writeRegister(GS_REG_BITBLTBUF, bitblt);
            gs.writeRegister(GS_REG_TRXPOS, 0ull);
            gs.writeRegister(GS_REG_TRXREG, (2ull << 0) | (1ull << 32)); // 2 pixels
            gs.writeRegister(GS_REG_TRXDIR, 0ull);

            std::vector<uint8_t> packet;
            appendU64(packet, makeGifTag(1u, GIF_FMT_IMAGE, 0u, true));
            appendU64(packet, 0ull);
            const uint8_t rgbData[16] = {
                0x11u, 0x22u, 0x33u,
                0x44u, 0x55u, 0x66u,
                0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u
            };
            packet.insert(packet.end(), rgbData, rgbData + sizeof(rgbData));
            gs.processGIFPacket(packet.data(), static_cast<uint32_t>(packet.size()));

            // Read back from local to host in CT24.
            gs.writeRegister(GS_REG_TRXDIR, 1ull);
            uint8_t out[16] = {};
            const uint32_t outBytes = gs.consumeLocalToHostBytes(out, sizeof(out));

            t.Equals(outBytes, 6u, "CT24 local->host read should output 3 bytes per pixel");
            t.Equals(out[0], static_cast<uint8_t>(0x11u), "pixel0 R should roundtrip");
            t.Equals(out[1], static_cast<uint8_t>(0x22u), "pixel0 G should roundtrip");
            t.Equals(out[2], static_cast<uint8_t>(0x33u), "pixel0 B should roundtrip");
            t.Equals(out[3], static_cast<uint8_t>(0x44u), "pixel1 R should roundtrip");
            t.Equals(out[4], static_cast<uint8_t>(0x55u), "pixel1 G should roundtrip");
            t.Equals(out[5], static_cast<uint8_t>(0x66u), "pixel1 B should roundtrip");
        });

        tc.Run("GS PSMT4 host-local-host keeps nibble packing stable", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            const uint64_t bitblt =
                (static_cast<uint64_t>(0u) << 0) |      // SBP
                (static_cast<uint64_t>(1u) << 16) |     // SBW
                (static_cast<uint64_t>(20u) << 24) |    // SPSM PSMT4
                (static_cast<uint64_t>(0u) << 32) |     // DBP
                (static_cast<uint64_t>(1u) << 48) |     // DBW
                (static_cast<uint64_t>(20u) << 56);     // DPSM PSMT4
            gs.writeRegister(GS_REG_BITBLTBUF, bitblt);
            gs.writeRegister(GS_REG_TRXPOS, 0ull);
            gs.writeRegister(GS_REG_TRXREG, (4ull << 0) | (1ull << 32)); // 4 texels => 2 bytes
            gs.writeRegister(GS_REG_TRXDIR, 0ull);

            std::vector<uint8_t> packet;
            appendU64(packet, makeGifTag(1u, GIF_FMT_IMAGE, 0u, true));
            appendU64(packet, 0ull);
            const uint8_t nibbleData[16] = {0x21u, 0x43u};
            packet.insert(packet.end(), nibbleData, nibbleData + sizeof(nibbleData));
            gs.processGIFPacket(packet.data(), static_cast<uint32_t>(packet.size()));

            gs.writeRegister(GS_REG_TRXDIR, 1ull);
            uint8_t out[8] = {};
            const uint32_t outBytes = gs.consumeLocalToHostBytes(out, sizeof(out));

            t.Equals(outBytes, 2u, "PSMT4 local->host should return packed nibble bytes");
            t.Equals(out[0], static_cast<uint8_t>(0x21u), "packed nibble byte 0 should roundtrip");
            t.Equals(out[1], static_cast<uint8_t>(0x43u), "packed nibble byte 1 should roundtrip");
        });

        tc.Run("GS PSMT4 host-local upload keeps position across split IMAGE packets", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            const uint64_t bitblt =
                (static_cast<uint64_t>(0u) << 0) |
                (static_cast<uint64_t>(1u) << 16) |
                (static_cast<uint64_t>(GS_PSM_T4) << 24) |
                (static_cast<uint64_t>(0u) << 32) |
                (static_cast<uint64_t>(1u) << 48) |
                (static_cast<uint64_t>(GS_PSM_T4) << 56);
            gs.writeRegister(GS_REG_BITBLTBUF, bitblt);
            gs.writeRegister(GS_REG_TRXPOS, 0ull);
            gs.writeRegister(GS_REG_TRXREG, (8ull << 0) | (8ull << 32)); // 64 texels => 32 bytes
            gs.writeRegister(GS_REG_TRXDIR, 0ull);

            uint8_t packedSource[32] = {};
            for (uint32_t i = 0; i < 32u; ++i)
            {
                packedSource[i] = static_cast<uint8_t>(0x10u + i);
            }

            std::vector<uint8_t> packetA;
            appendU64(packetA, makeGifTag(1u, GIF_FMT_IMAGE, 0u, true));
            appendU64(packetA, 0ull);
            packetA.insert(packetA.end(), packedSource, packedSource + 16u);

            std::vector<uint8_t> packetB;
            appendU64(packetB, makeGifTag(1u, GIF_FMT_IMAGE, 0u, true));
            appendU64(packetB, 0ull);
            packetB.insert(packetB.end(), packedSource + 16u, packedSource + 32u);

            gs.processGIFPacket(packetA.data(), static_cast<uint32_t>(packetA.size()));
            gs.processGIFPacket(packetB.data(), static_cast<uint32_t>(packetB.size()));

            gs.writeRegister(GS_REG_TRXDIR, 1ull);
            uint8_t out[32] = {};
            const uint32_t outBytes = gs.consumeLocalToHostBytes(out, sizeof(out));

            t.Equals(outBytes, 32u, "split T4 IMAGE upload should fill the full packed byte range");
            for (uint32_t i = 0; i < 32u; ++i)
            {
                t.Equals(out[i], packedSource[i], "split T4 IMAGE upload should preserve packed nibble order");
            }
        });

        tc.Run("GS PSMT4 local-local copy respects swizzled page layout", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            constexpr uint32_t kSrcBp = 64u;
            constexpr uint32_t kDstBp = 96u;
            constexpr uint64_t kUploadBitblt =
                (static_cast<uint64_t>(0u) << 0) |
                (static_cast<uint64_t>(2u) << 16) |
                (static_cast<uint64_t>(GS_PSM_T4) << 24) |
                (static_cast<uint64_t>(kSrcBp) << 32) |
                (static_cast<uint64_t>(2u) << 48) |
                (static_cast<uint64_t>(GS_PSM_T4) << 56);
            constexpr uint64_t kCopyBitblt =
                (static_cast<uint64_t>(kSrcBp) << 0) |
                (static_cast<uint64_t>(2u) << 16) |
                (static_cast<uint64_t>(GS_PSM_T4) << 24) |
                (static_cast<uint64_t>(kDstBp) << 32) |
                (static_cast<uint64_t>(2u) << 48) |
                (static_cast<uint64_t>(GS_PSM_T4) << 56);
            constexpr uint64_t kCopyPos =
                (static_cast<uint64_t>(0u) << 0) |
                (static_cast<uint64_t>(0u) << 16) |
                (static_cast<uint64_t>(32u) << 32) |
                (static_cast<uint64_t>(16u) << 48);
            constexpr uint64_t kReadBitblt =
                (static_cast<uint64_t>(kDstBp) << 0) |
                (static_cast<uint64_t>(2u) << 16) |
                (static_cast<uint64_t>(GS_PSM_T4) << 24) |
                (static_cast<uint64_t>(0u) << 32) |
                (static_cast<uint64_t>(2u) << 48) |
                (static_cast<uint64_t>(GS_PSM_T4) << 56);
            constexpr uint64_t kReadPos =
                (static_cast<uint64_t>(32u) << 0) |
                (static_cast<uint64_t>(16u) << 16);
            constexpr uint64_t kRect = (8ull << 0) | (4ull << 32);
            const uint8_t packedSource[16] = {
                0x10u, 0x32u, 0x54u, 0x76u,
                0x98u, 0xBAu, 0xDCu, 0xFEu,
                0x01u, 0x23u, 0x45u, 0x67u,
                0x89u, 0xABu, 0xCDu, 0xEFu
            };

            gs.writeRegister(GS_REG_BITBLTBUF, kUploadBitblt);
            gs.writeRegister(GS_REG_TRXPOS, 0ull);
            gs.writeRegister(GS_REG_TRXREG, kRect);
            gs.writeRegister(GS_REG_TRXDIR, 0ull);

            std::vector<uint8_t> packet;
            appendU64(packet, makeGifTag(1u, GIF_FMT_IMAGE, 0u, true));
            appendU64(packet, 0ull);
            packet.insert(packet.end(), packedSource, packedSource + sizeof(packedSource));
            gs.processGIFPacket(packet.data(), static_cast<uint32_t>(packet.size()));

            gs.writeRegister(GS_REG_BITBLTBUF, kCopyBitblt);
            gs.writeRegister(GS_REG_TRXPOS, kCopyPos);
            gs.writeRegister(GS_REG_TRXREG, kRect);
            gs.writeRegister(GS_REG_TRXDIR, 2ull);

            gs.writeRegister(GS_REG_BITBLTBUF, kReadBitblt);
            gs.writeRegister(GS_REG_TRXPOS, kReadPos);
            gs.writeRegister(GS_REG_TRXREG, kRect);
            gs.writeRegister(GS_REG_TRXDIR, 1ull);

            uint8_t out[16] = {};
            const uint32_t outBytes = gs.consumeLocalToHostBytes(out, sizeof(out));
            t.Equals(outBytes, 16u, "PSMT4 local-local copy should preserve the full packed byte count");
            for (size_t i = 0; i < sizeof(packedSource); ++i)
            {
                t.Equals(out[i], packedSource[i], "PSMT4 local-local copy should preserve packed nibble order");
            }
        });

        tc.Run("GS T4 CSM1 lookup matches Veronica ClutCopy layout", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            constexpr uint32_t kTexTbp = 64u;
            constexpr uint32_t kClutCbp = 128u;
            constexpr uint32_t kFrameReg =
                (0ull << 0) |
                (1ull << 16) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 24);
            constexpr uint64_t kTex0 =
                (static_cast<uint64_t>(kTexTbp) << 0) |
                (1ull << 14) |
                (static_cast<uint64_t>(GS_PSM_T4) << 20) |
                (0ull << 26) |
                (0ull << 30) |
                (1ull << 34) |
                (1ull << 35) |
                (static_cast<uint64_t>(kClutCbp) << 37) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 51);
            constexpr uint64_t kPrim =
                static_cast<uint64_t>(GS_PRIM_SPRITE) |
                (1ull << 4) |  // TME
                (1ull << 8);   // FST
            constexpr uint32_t kExpectedColor = 0x800000FFu; // RGBA = (255,0,0,128)
            constexpr uint32_t kWrongColor = 0x8000FF00u;    // RGBA = (0,255,0,128)

            const uint32_t texNibbleAddr = GSPSMT4::addrPSMT4(kTexTbp, 1u, 0u, 0u);
            const uint32_t texByteOff = texNibbleAddr >> 1;
            vram[texByteOff] = static_cast<uint8_t>((vram[texByteOff] & 0xF0u) | 0x08u);

            // Veronica's ClutCopy stores logical entries 8..15 into physical slots 16..23.
            std::memcpy(vram.data() + kClutCbp * 256u + 8u * 4u, &kWrongColor, sizeof(kWrongColor));
            std::memcpy(vram.data() + kClutCbp * 256u + 16u * 4u, &kExpectedColor, sizeof(kExpectedColor));

            gs.writeRegister(GS_REG_FRAME_1, kFrameReg);
            gs.writeRegister(GS_REG_SCISSOR_1, 0ull);
            gs.writeRegister(GS_REG_XYOFFSET_1, 0ull);
            gs.writeRegister(GS_REG_TEST_1, 0ull);
            gs.writeRegister(GS_REG_ALPHA_1, 0ull);
            gs.writeRegister(GS_REG_TEX0_1, kTex0);
            gs.writeRegister(GS_REG_PRIM, kPrim);
            gs.writeRegister(GS_REG_RGBAQ, 0x80808080ull);
            gs.writeRegister(GS_REG_UV, 0ull);
            gs.writeRegister(GS_REG_XYZ2, 0ull);
            gs.writeRegister(GS_REG_UV, 0ull);
            gs.writeRegister(GS_REG_XYZ2, 0ull);

            uint32_t pixel = 0u;
            std::memcpy(&pixel, vram.data(), sizeof(pixel));
            t.Equals(pixel, kExpectedColor,
                     "T4 CSM1 lookup should follow Veronica's swizzled CLUT layout for logical index 8");
        });

        tc.Run("GS alpha test AFAIL framebuffer-only still writes the pixel", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            constexpr uint64_t kFrame =
                (0ull << 0) |
                (1ull << 16) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 24);
            constexpr uint64_t kScissor =
                (0ull << 0) |
                (0ull << 16) |
                (0ull << 32) |
                (0ull << 48);
            constexpr uint64_t kTest =
                1ull |                  // ATE
                (5ull << 1) |          // ATST = GEQUAL
                (0x80ull << 4) |       // AREF
                (1ull << 12);          // AFAIL = FB_ONLY
            constexpr uint64_t kPrim =
                static_cast<uint64_t>(GS_PRIM_POINT);
            constexpr uint64_t kRgbaq =
                (0x12ull << 0) |
                (0x34ull << 8) |
                (0x56ull << 16) |
                (0x00ull << 24) |
                (0x3F800000ull << 32); // q = 1.0f

            gs.writeRegister(GS_REG_FRAME_1, kFrame);
            gs.writeRegister(GS_REG_SCISSOR_1, kScissor);
            gs.writeRegister(GS_REG_TEST_1, kTest);
            gs.writeRegister(GS_REG_PRIM, kPrim);
            gs.writeRegister(GS_REG_RGBAQ, kRgbaq);
            gs.writeRegister(GS_REG_XYZ2, 0ull);

            uint32_t pixel = 0u;
            std::memcpy(&pixel, vram.data(), sizeof(pixel));
            t.Equals(pixel, 0x00563412u,
                     "AFAIL=FB_ONLY should still update the framebuffer when the alpha test fails");
        });

        tc.Run("GS alpha test AFAIL RGB-only preserves destination alpha", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            constexpr uint64_t kFrame =
                (0ull << 0) |
                (1ull << 16) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 24);
            constexpr uint64_t kScissor =
                (0ull << 0) |
                (0ull << 16) |
                (0ull << 32) |
                (0ull << 48);
            constexpr uint64_t kTest =
                1ull |                  // ATE
                (5ull << 1) |          // ATST = GEQUAL
                (0x80ull << 4) |       // AREF
                (3ull << 12);          // AFAIL = RGB_ONLY
            constexpr uint64_t kPrim =
                static_cast<uint64_t>(GS_PRIM_POINT);
            constexpr uint64_t kRgbaq =
                (0x12ull << 0) |
                (0x34ull << 8) |
                (0x56ull << 16) |
                (0x00ull << 24) |
                (0x3F800000ull << 32); // q = 1.0f
            constexpr uint32_t kExisting = 0xAB030201u;

            std::memcpy(vram.data(), &kExisting, sizeof(kExisting));

            gs.writeRegister(GS_REG_FRAME_1, kFrame);
            gs.writeRegister(GS_REG_SCISSOR_1, kScissor);
            gs.writeRegister(GS_REG_TEST_1, kTest);
            gs.writeRegister(GS_REG_PRIM, kPrim);
            gs.writeRegister(GS_REG_RGBAQ, kRgbaq);
            gs.writeRegister(GS_REG_XYZ2, 0ull);

            uint32_t pixel = 0u;
            std::memcpy(&pixel, vram.data(), sizeof(pixel));
            t.Equals(pixel, 0xAB563412u,
                     "AFAIL=RGB_ONLY should update RGB while preserving destination alpha");
        });

        tc.Run("sceGsSyncV waits on VBlank and reports interlaced field parity", [](TestCase &t)
        {
            notifyRuntimeStop();
            ps2_stubs::resetGsSyncVCallbackState();

            PS2Runtime runtime;
            t.IsTrue(runtime.memory().initialize(), "runtime memory initialize should succeed");
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);

            R5900Context resetCtx{};
            setRegU32(resetCtx, 4, 0u);
            setRegU32(resetCtx, 5, 1u);
            setRegU32(resetCtx, 6, 2u);
            setRegU32(resetCtx, 7, 1u);
            ps2_stubs::sceGsResetGraph(rdram.data(), &resetCtx, &runtime);

            R5900Context sync0{};
            ps2_stubs::sceGsSyncV(rdram.data(), &sync0, &runtime);
            t.Equals(static_cast<int32_t>(getRegU32Test(sync0, 2)), 0, "first interlaced sceGsSyncV should report even field");

            R5900Context sync1{};
            ps2_stubs::sceGsSyncV(rdram.data(), &sync1, &runtime);
            t.Equals(static_cast<int32_t>(getRegU32Test(sync1, 2)), 1, "second interlaced sceGsSyncV should report odd field");

            R5900Context resetProgCtx{};
            setRegU32(resetProgCtx, 4, 0u);
            setRegU32(resetProgCtx, 5, 0u);
            setRegU32(resetProgCtx, 6, 2u);
            setRegU32(resetProgCtx, 7, 1u);
            ps2_stubs::sceGsResetGraph(rdram.data(), &resetProgCtx, &runtime);

            R5900Context syncProg{};
            ps2_stubs::sceGsSyncV(rdram.data(), &syncProg, &runtime);
            t.Equals(static_cast<int32_t>(getRegU32Test(syncProg, 2)), 1, "progressive sceGsSyncV should always return one");

            runtime.requestStop();
            notifyRuntimeStop();
            ps2_stubs::resetGsSyncVCallbackState();
        });

        tc.Run("sceGsSyncVCallback uses the shared VBlank worker", [](TestCase &t)
        {
            notifyRuntimeStop();
            ps2_stubs::resetGsSyncVCallbackState();
            g_gsSyncCallbackHits.store(0u, std::memory_order_relaxed);
            g_gsSyncCallbackLastTick.store(0u, std::memory_order_relaxed);

            PS2Runtime runtime;
            t.IsTrue(runtime.memory().initialize(), "runtime memory initialize should succeed");
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);

            constexpr uint32_t kCallbackAddr = 0x120000u;
            runtime.registerFunction(kCallbackAddr, testGsSyncVCallback);

            R5900Context callbackCtx{};
            setRegU32(callbackCtx, 4, kCallbackAddr);
            ps2_stubs::sceGsSyncVCallback(rdram.data(), &callbackCtx, &runtime);
            t.Equals(getRegU32Test(callbackCtx, 2), 0u, "first sceGsSyncVCallback registration should return no previous callback");

            const bool callbackFired = waitUntil([]() {
                return g_gsSyncCallbackHits.load(std::memory_order_acquire) > 0u;
            }, std::chrono::milliseconds(80));

            t.IsTrue(callbackFired, "registered GS VSync callback should fire from the VBlank worker");
            t.IsTrue(g_gsSyncCallbackLastTick.load(std::memory_order_acquire) > 0u,
                     "VSync callback should receive a positive tick value");

            R5900Context clearCtx{};
            setRegU32(clearCtx, 4, 0u);
            ps2_stubs::sceGsSyncVCallback(rdram.data(), &clearCtx, &runtime);
            t.Equals(getRegU32Test(clearCtx, 2), kCallbackAddr, "clearing sceGsSyncVCallback should return the previous callback");

            runtime.requestStop();
            notifyRuntimeStop();
            ps2_stubs::resetGsSyncVCallbackState();
        });
    });
}
