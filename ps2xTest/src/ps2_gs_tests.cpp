#include "MiniTest.h"
#include "ps2_memory.h"
#include "ps2_runtime.h"
#include "ps2_syscalls.h"
#include "ps2_gs_gpu.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace ps2_syscalls;

namespace
{
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
    });
}
