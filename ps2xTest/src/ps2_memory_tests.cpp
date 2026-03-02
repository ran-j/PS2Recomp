#include "MiniTest.h"
#include "ps2_memory.h"
#include "ps2_gs_gpu.h"
#include "ps2_vu1.h"
#include "ps2_runtime_macros.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
    uint32_t makeVifCmd(uint8_t opcode, uint8_t num, uint16_t imm)
    {
        return (static_cast<uint32_t>(opcode) << 24) |
               (static_cast<uint32_t>(num) << 16) |
               static_cast<uint32_t>(imm);
    }

    void appendU32(std::vector<uint8_t> &dst, uint32_t value)
    {
        const size_t pos = dst.size();
        dst.resize(pos + sizeof(uint32_t));
        std::memcpy(dst.data() + pos, &value, sizeof(uint32_t));
    }

    void appendU64(std::vector<uint8_t> &dst, uint64_t value)
    {
        const size_t pos = dst.size();
        dst.resize(pos + sizeof(uint64_t));
        std::memcpy(dst.data() + pos, &value, sizeof(uint64_t));
    }

    uint64_t makeDmaTag(uint16_t qwc, uint8_t id, uint32_t addr, bool irq = false)
    {
        return static_cast<uint64_t>(qwc) |
               (static_cast<uint64_t>(id & 0x7u) << 28) |
               (irq ? (1ull << 31) : 0ull) |
               (static_cast<uint64_t>(addr & 0x7FFFFFFFu) << 32);
    }

    void writeDmaTag(uint8_t *rdram, uint32_t tagAddr, uint64_t tagLo)
    {
        std::memset(rdram + tagAddr, 0, 16);
        std::memcpy(rdram + tagAddr, &tagLo, sizeof(tagLo));
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

    uint32_t makeVuLowerSpecial(uint8_t funct, uint8_t is, uint8_t it = 0u, uint8_t id = 0u, uint8_t dest = 0u)
    {
        return (0x40u << 25) |
               (static_cast<uint32_t>(dest & 0xFu) << 21) |
               (static_cast<uint32_t>(it & 0x1Fu) << 16) |
               (static_cast<uint32_t>(is & 0x1Fu) << 11) |
               (static_cast<uint32_t>(id & 0x1Fu) << 6) |
               static_cast<uint32_t>(funct & 0x3Fu);
    }
}

void register_ps2_memory_tests()
{
    MiniTest::Case("PS2Memory", [](TestCase &tc)
    {
        tc.Run("uncached aliases map to same RDRAM bytes", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            mem.write32(0x00001000u, 0xDEADBEEFu);
            t.Equals(mem.read32(0x00001000u), 0xDEADBEEFu, "base readback should match");
            t.Equals(mem.read32(0x20001000u), 0xDEADBEEFu, "0x2000_0000 alias should map to RDRAM");

            // 0x3010_0000 maps to physical 0x0010_0000 (AboutPS2 memory map).
            mem.write32(0x00101000u, 0xDEADBEEFu);
            t.Equals(mem.read32(0x30101000u), 0xDEADBEEFu, "0x3010_0000 accelerated alias should map to RDRAM");

            mem.write32(0x20002000u, 0x13579BDFu);
            t.Equals(mem.read32(0x00002000u), 0x13579BDFu, "writes through 0x2000 alias should land in base RDRAM");

            mem.write32(0x30103000u, 0x2468ACE0u);
            t.Equals(mem.read32(0x00103000u), 0x2468ACE0u, "writes through 0x3010 alias should land in base RDRAM");
        });

        tc.Run("translateAddress handles kseg and uncached aliases", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            t.Equals(mem.translateAddress(0x80001234u), 0x00001234u, "KSEG0 should map directly to physical");
            t.Equals(mem.translateAddress(0xA0005678u), 0x00005678u, "KSEG1 should map directly to physical");
            t.Equals(mem.translateAddress(0x20001234u), 0x00001234u, "0x2000 uncached alias should map to RAM");
            t.Equals(mem.translateAddress(0x30105678u), 0x00105678u, "0x3010 accelerated alias should map to RAM");
        });

        tc.Run("fast memory helpers wrap safely at RAM boundary", [](TestCase &t)
        {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            const uint32_t tail = PS2_RAM_SIZE - 4u;

            // Build a wrapped 64-bit pattern: [tail..tail+3] + [0..3]
            rdram[tail + 0u] = 0xA1u;
            rdram[tail + 1u] = 0xB2u;
            rdram[tail + 2u] = 0xC3u;
            rdram[tail + 3u] = 0xD4u;
            rdram[0u] = 0x11u;
            rdram[1u] = 0x22u;
            rdram[2u] = 0x33u;
            rdram[3u] = 0x44u;

            const uint64_t wrappedRead = Ps2FastRead64(rdram.data(), tail);
            t.Equals(wrappedRead, 0x44332211D4C3B2A1ull,
                     "Ps2FastRead64 should wrap across the 32MB boundary");

            Ps2FastWrite64(rdram.data(), tail, 0x8877665544332211ull);
            t.Equals(static_cast<uint32_t>(rdram[tail + 0u]), 0x11u, "write byte 0 should land at tail+0");
            t.Equals(static_cast<uint32_t>(rdram[tail + 1u]), 0x22u, "write byte 1 should land at tail+1");
            t.Equals(static_cast<uint32_t>(rdram[tail + 2u]), 0x33u, "write byte 2 should land at tail+2");
            t.Equals(static_cast<uint32_t>(rdram[tail + 3u]), 0x44u, "write byte 3 should land at tail+3");
            t.Equals(static_cast<uint32_t>(rdram[0u]), 0x55u, "write byte 4 should wrap to address 0");
            t.Equals(static_cast<uint32_t>(rdram[1u]), 0x66u, "write byte 5 should wrap to address 1");
            t.Equals(static_cast<uint32_t>(rdram[2u]), 0x77u, "write byte 6 should wrap to address 2");
            t.Equals(static_cast<uint32_t>(rdram[3u]), 0x88u, "write byte 7 should wrap to address 3");
        });

        tc.Run("VIF MPG num zero uploads 256 instructions", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            std::vector<uint8_t> packet;
            packet.reserve(4u + 2048u);
            appendU32(packet, makeVifCmd(0x4Au, 0u, 0u)); // MPG, num=0 -> 256 instructions (2048 bytes)

            for (uint32_t i = 0; i < 2048u; ++i)
            {
                packet.push_back(static_cast<uint8_t>(i & 0xFFu));
            }

            std::memset(mem.getVU1Code(), 0, PS2_VU1_CODE_SIZE);
            mem.processVIF1Data(packet.data(), static_cast<uint32_t>(packet.size()));

            const uint8_t *vu1Code = mem.getVU1Code();
            bool matches = true;
            for (uint32_t i = 0; i < 2048u; ++i)
            {
                if (vu1Code[i] != static_cast<uint8_t>(i & 0xFFu))
                {
                    matches = false;
                    break;
                }
            }
            t.IsTrue(matches, "MPG num=0 should copy 2048 bytes into VU1 code memory");
        });

        tc.Run("VIF UNPACK num zero uploads 256 vectors", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            // UNPACK V4_32: opcode 0x6C (vn=3, vl=0), num=0 => 256 vectors, 16 bytes each.
            std::vector<uint8_t> packet;
            packet.reserve(4u + 4096u);
            appendU32(packet, makeVifCmd(0x6Cu, 0u, 0u));
            for (uint32_t i = 0; i < 4096u; ++i)
            {
                packet.push_back(static_cast<uint8_t>((i * 3u) & 0xFFu));
            }

            std::memset(mem.getVU1Data(), 0, PS2_VU1_DATA_SIZE);
            mem.processVIF1Data(packet.data(), static_cast<uint32_t>(packet.size()));

            const uint8_t *vu1Data = mem.getVU1Data();
            bool matches = true;
            for (uint32_t i = 0; i < 4096u; ++i)
            {
                if (vu1Data[i] != static_cast<uint8_t>((i * 3u) & 0xFFu))
                {
                    matches = false;
                    break;
                }
            }
            t.IsTrue(matches, "UNPACK num=0 should copy 256 V4_32 vectors (4096 bytes)");
        });

        tc.Run("VIF control commands update MARK MASK ROW and COL registers", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            std::vector<uint8_t> packet;
            appendU32(packet, makeVifCmd(0x07u, 0u, 0x1234u)); // MARK

            appendU32(packet, makeVifCmd(0x20u, 0u, 0u));      // STMASK
            appendU32(packet, 0x89ABCDEFu);

            appendU32(packet, makeVifCmd(0x30u, 0u, 0u));      // STROW
            appendU32(packet, 0x11111111u);
            appendU32(packet, 0x22222222u);
            appendU32(packet, 0x33333333u);
            appendU32(packet, 0x44444444u);

            appendU32(packet, makeVifCmd(0x31u, 0u, 0u));      // STCOL
            appendU32(packet, 0xAAAA0001u);
            appendU32(packet, 0xAAAA0002u);
            appendU32(packet, 0xAAAA0003u);
            appendU32(packet, 0xAAAA0004u);

            mem.processVIF1Data(packet.data(), static_cast<uint32_t>(packet.size()));

            t.Equals(mem.vif1_regs.mark, 0x1234u, "MARK should set VIF1 MARK register");
            t.Equals(mem.vif1_regs.mask, 0x89ABCDEFu, "STMASK should set VIF1 MASK register");

            t.Equals(mem.vif1_regs.row[0], 0x11111111u, "STROW should set row[0]");
            t.Equals(mem.vif1_regs.row[1], 0x22222222u, "STROW should set row[1]");
            t.Equals(mem.vif1_regs.row[2], 0x33333333u, "STROW should set row[2]");
            t.Equals(mem.vif1_regs.row[3], 0x44444444u, "STROW should set row[3]");

            t.Equals(mem.vif1_regs.col[0], 0xAAAA0001u, "STCOL should set col[0]");
            t.Equals(mem.vif1_regs.col[1], 0xAAAA0002u, "STCOL should set col[1]");
            t.Equals(mem.vif1_regs.col[2], 0xAAAA0003u, "STCOL should set col[2]");
            t.Equals(mem.vif1_regs.col[3], 0xAAAA0004u, "STCOL should set col[3]");
        });

        tc.Run("VIF UNPACK V4-16 sign and zero extension follow immediate bit14", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");
            std::memset(mem.getVU1Data(), 0, PS2_VU1_DATA_SIZE);

            // UNPACK V4-16 (opcode 0x6D), num=1, addr=0.
            // Payload components: x=0xFF80, y=0x0001, z=0x7FFF, w=0x8001.
            const uint16_t comps[4] = {0xFF80u, 0x0001u, 0x7FFFu, 0x8001u};

            std::vector<uint8_t> signPacket;
            appendU32(signPacket, makeVifCmd(0x6Du, 1u, 0x0000u)); // sign-extend
            for (uint16_t c : comps)
            {
                const size_t pos = signPacket.size();
                signPacket.resize(pos + sizeof(uint16_t));
                std::memcpy(signPacket.data() + pos, &c, sizeof(uint16_t));
            }
            mem.processVIF1Data(signPacket.data(), static_cast<uint32_t>(signPacket.size()));

            const uint8_t *vu1 = mem.getVU1Data();
            uint32_t sx = 0, sy = 0, sz = 0, sw = 0;
            std::memcpy(&sx, vu1 + 0, 4);
            std::memcpy(&sy, vu1 + 4, 4);
            std::memcpy(&sz, vu1 + 8, 4);
            std::memcpy(&sw, vu1 + 12, 4);
            t.Equals(sx, 0xFFFFFF80u, "sign-extend x");
            t.Equals(sy, 0x00000001u, "sign-extend y");
            t.Equals(sz, 0x00007FFFu, "sign-extend z");
            t.Equals(sw, 0xFFFF8001u, "sign-extend w");

            // Same UNPACK with imm bit14 set => zero-extend.
            std::vector<uint8_t> zeroPacket;
            appendU32(zeroPacket, makeVifCmd(0x6Du, 1u, 0x4000u)); // zero-extend
            for (uint16_t c : comps)
            {
                const size_t pos = zeroPacket.size();
                zeroPacket.resize(pos + sizeof(uint16_t));
                std::memcpy(zeroPacket.data() + pos, &c, sizeof(uint16_t));
            }
            mem.processVIF1Data(zeroPacket.data(), static_cast<uint32_t>(zeroPacket.size()));

            std::memcpy(&sx, vu1 + 0, 4);
            std::memcpy(&sy, vu1 + 4, 4);
            std::memcpy(&sz, vu1 + 8, 4);
            std::memcpy(&sw, vu1 + 12, 4);
            t.Equals(sx, 0x0000FF80u, "zero-extend x");
            t.Equals(sy, 0x00000001u, "zero-extend y");
            t.Equals(sz, 0x00007FFFu, "zero-extend z");
            t.Equals(sw, 0x00008001u, "zero-extend w");
        });

        tc.Run("VIF UNPACK bit15 adds TOPS to destination address", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");
            std::memset(mem.getVU1Data(), 0, PS2_VU1_DATA_SIZE);

            mem.vif1_regs.tops = 4u;

            // UNPACK V4-32, num=1, addr=2, bit15 set => effective addr = 6.
            std::vector<uint8_t> packet;
            appendU32(packet, makeVifCmd(0x6Cu, 1u, static_cast<uint16_t>(0x8000u | 0x0002u)));
            appendU32(packet, 0x11111111u);
            appendU32(packet, 0x22222222u);
            appendU32(packet, 0x33333333u);
            appendU32(packet, 0x44444444u);

            mem.processVIF1Data(packet.data(), static_cast<uint32_t>(packet.size()));

            const uint8_t *vu1 = mem.getVU1Data();

            uint32_t untouched = 0xDEADBEEFu;
            std::memcpy(&untouched, vu1 + (2u * 16u), 4);
            t.Equals(untouched, 0u, "base addr without TOPS should remain untouched");

            uint32_t x = 0, y = 0, z = 0, w = 0;
            const uint32_t dest = 6u * 16u;
            std::memcpy(&x, vu1 + dest + 0u, 4);
            std::memcpy(&y, vu1 + dest + 4u, 4);
            std::memcpy(&z, vu1 + dest + 8u, 4);
            std::memcpy(&w, vu1 + dest + 12u, 4);
            t.Equals(x, 0x11111111u, "TOPS-adjusted x");
            t.Equals(y, 0x22222222u, "TOPS-adjusted y");
            t.Equals(z, 0x33333333u, "TOPS-adjusted z");
            t.Equals(w, 0x44444444u, "TOPS-adjusted w");
        });

        tc.Run("VIF STCYCL skip mode advances destination by CL when CL>=WL", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");
            std::memset(mem.getVU1Data(), 0, PS2_VU1_DATA_SIZE);

            std::vector<uint8_t> packet;
            appendU32(packet, makeVifCmd(0x01u, 0u, static_cast<uint16_t>((1u << 8) | 3u))); // STCYCL: WL=1, CL=3
            appendU32(packet, makeVifCmd(0x6Cu, 2u, 0u)); // UNPACK V4-32, NUM=2, ADDR=0

            appendU32(packet, 0x11111111u);
            appendU32(packet, 0x22222222u);
            appendU32(packet, 0x33333333u);
            appendU32(packet, 0x44444444u);

            appendU32(packet, 0xAAAAAAAAu);
            appendU32(packet, 0xBBBBBBBBu);
            appendU32(packet, 0xCCCCCCCCu);
            appendU32(packet, 0xDDDDDDDDu);

            mem.processVIF1Data(packet.data(), static_cast<uint32_t>(packet.size()));

            const uint8_t *vu = mem.getVU1Data();

            uint32_t v0x = 0, v1x = 0, v2x = 0, v3x = 0;
            std::memcpy(&v0x, vu + 0u * 16u + 0u, 4);
            std::memcpy(&v1x, vu + 1u * 16u + 0u, 4);
            std::memcpy(&v2x, vu + 2u * 16u + 0u, 4);
            std::memcpy(&v3x, vu + 3u * 16u + 0u, 4);

            t.Equals(v0x, 0x11111111u, "first vector should write at addr 0");
            t.Equals(v1x, 0u, "skip mode should leave addr 1 untouched when WL=1 CL=3");
            t.Equals(v2x, 0u, "skip mode should leave addr 2 untouched when WL=1 CL=3");
            t.Equals(v3x, 0xAAAAAAAAu, "second vector should write at addr CL (addr 3)");
        });

        tc.Run("VIF masked UNPACK uses data row col and protect selectors", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");
            std::memset(mem.getVU1Data(), 0, PS2_VU1_DATA_SIZE);

            // Pre-fill destination W lane for write-protect verification.
            uint32_t preservedW = 0xDEADBEEFu;
            std::memcpy(mem.getVU1Data() + 12u, &preservedW, 4u);

            std::vector<uint8_t> packet;
            appendU32(packet, makeVifCmd(0x20u, 0u, 0u)); // STMASK
            appendU32(packet, 0x000000E4u); // m0=0(data), m1=1(row), m2=2(col), m3=3(protect)

            appendU32(packet, makeVifCmd(0x30u, 0u, 0u)); // STROW
            appendU32(packet, 0xAAAAB001u);
            appendU32(packet, 0xAAAAB002u);
            appendU32(packet, 0xAAAAB003u);
            appendU32(packet, 0xAAAAB004u);

            appendU32(packet, makeVifCmd(0x31u, 0u, 0u)); // STCOL
            appendU32(packet, 0x11110001u);
            appendU32(packet, 0x11110002u);
            appendU32(packet, 0x11110003u);
            appendU32(packet, 0x11110004u);

            appendU32(packet, makeVifCmd(0x7Cu, 1u, 0u)); // UNPACK V4-32 with CMD bit4 (mask enable)
            appendU32(packet, 0x01020304u);
            appendU32(packet, 0x11121314u);
            appendU32(packet, 0x21222324u);
            appendU32(packet, 0x31323334u);

            mem.processVIF1Data(packet.data(), static_cast<uint32_t>(packet.size()));

            const uint8_t *vu = mem.getVU1Data();
            uint32_t x = 0, y = 0, z = 0, w = 0;
            std::memcpy(&x, vu + 0u, 4u);
            std::memcpy(&y, vu + 4u, 4u);
            std::memcpy(&z, vu + 8u, 4u);
            std::memcpy(&w, vu + 12u, 4u);

            t.Equals(x, 0x01020304u, "mask=0 should write decompressed data");
            t.Equals(y, 0xAAAAB002u, "mask=1 should write row register for Y field");
            t.Equals(z, 0x11110001u, "mask=2 should write C0 on first write cycle");
            t.Equals(w, preservedW, "mask=3 should write-protect destination field");
        });

        tc.Run("VIF STMOD offset and difference modes apply to UNPACK data", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");
            std::memset(mem.getVU1Data(), 0, PS2_VU1_DATA_SIZE);

            std::vector<uint8_t> packet;
            appendU32(packet, makeVifCmd(0x30u, 0u, 0u)); // STROW
            appendU32(packet, 10u);
            appendU32(packet, 20u);
            appendU32(packet, 30u);
            appendU32(packet, 40u);

            appendU32(packet, makeVifCmd(0x05u, 0u, 1u)); // STMOD offset mode
            appendU32(packet, makeVifCmd(0x6Cu, 1u, 0u)); // UNPACK V4-32 -> addr 0
            appendU32(packet, 1u);
            appendU32(packet, 2u);
            appendU32(packet, 3u);
            appendU32(packet, 4u);

            appendU32(packet, makeVifCmd(0x30u, 0u, 0u)); // reset STROW for difference mode
            appendU32(packet, 100u);
            appendU32(packet, 100u);
            appendU32(packet, 100u);
            appendU32(packet, 100u);

            appendU32(packet, makeVifCmd(0x05u, 0u, 2u)); // STMOD difference mode
            appendU32(packet, makeVifCmd(0x6Cu, 2u, 1u)); // UNPACK V4-32 -> addr 1 and 2
            appendU32(packet, 1u);
            appendU32(packet, 1u);
            appendU32(packet, 1u);
            appendU32(packet, 1u);
            appendU32(packet, 2u);
            appendU32(packet, 2u);
            appendU32(packet, 2u);
            appendU32(packet, 2u);

            mem.processVIF1Data(packet.data(), static_cast<uint32_t>(packet.size()));

            const uint8_t *vu = mem.getVU1Data();
            uint32_t x0 = 0, y0 = 0, z0 = 0, w0 = 0;
            std::memcpy(&x0, vu + 0u * 16u + 0u, 4u);
            std::memcpy(&y0, vu + 0u * 16u + 4u, 4u);
            std::memcpy(&z0, vu + 0u * 16u + 8u, 4u);
            std::memcpy(&w0, vu + 0u * 16u + 12u, 4u);
            t.Equals(x0, 11u, "offset mode X");
            t.Equals(y0, 22u, "offset mode Y");
            t.Equals(z0, 33u, "offset mode Z");
            t.Equals(w0, 44u, "offset mode W");

            uint32_t x1 = 0, x2 = 0;
            std::memcpy(&x1, vu + 1u * 16u + 0u, 4u);
            std::memcpy(&x2, vu + 2u * 16u + 0u, 4u);
            t.Equals(x1, 101u, "difference mode first write should add initial row");
            t.Equals(x2, 103u, "difference mode second write should accumulate updated row");
            t.Equals(mem.vif1_regs.row[0], 103u, "difference mode should update row register");
            t.Equals(mem.vif1_regs.row[1], 103u, "difference mode should update row register for Y");
            t.Equals(mem.vif1_regs.row[2], 103u, "difference mode should update row register for Z");
            t.Equals(mem.vif1_regs.row[3], 103u, "difference mode should update row register for W");
        });

        tc.Run("VIF fill write uses STMASK and STROW when WL>CL", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");
            std::memset(mem.getVU1Data(), 0, PS2_VU1_DATA_SIZE);

            std::vector<uint8_t> packet;
            appendU32(packet, makeVifCmd(0x01u, 0u, static_cast<uint16_t>((3u << 8) | 1u))); // STCYCL: WL=3, CL=1

            appendU32(packet, makeVifCmd(0x20u, 0u, 0u)); // STMASK
            appendU32(packet, 0x55555555u); // all fields all cycles use row register

            appendU32(packet, makeVifCmd(0x30u, 0u, 0u)); // STROW
            appendU32(packet, 0x11111111u);
            appendU32(packet, 0x22222222u);
            appendU32(packet, 0x33333333u);
            appendU32(packet, 0x44444444u);

            appendU32(packet, makeVifCmd(0x7Cu, 3u, 0u)); // masked UNPACK V4-32, NUM=3 writes
            // Only one input vector should be consumed for CL=1, WL=3.
            appendU32(packet, 0xAAAABBBB);
            appendU32(packet, 0xCCCCDDDD);
            appendU32(packet, 0xEEEEFFFF);
            appendU32(packet, 0x12345678);

            mem.processVIF1Data(packet.data(), static_cast<uint32_t>(packet.size()));

            const uint8_t *vu = mem.getVU1Data();
            for (uint32_t i = 0; i < 3u; ++i)
            {
                uint32_t x = 0, y = 0, z = 0, w = 0;
                std::memcpy(&x, vu + i * 16u + 0u, 4u);
                std::memcpy(&y, vu + i * 16u + 4u, 4u);
                std::memcpy(&z, vu + i * 16u + 8u, 4u);
                std::memcpy(&w, vu + i * 16u + 12u, 4u);
                t.Equals(x, 0x11111111u, "fill write X should use row[0]");
                t.Equals(y, 0x22222222u, "fill write Y should use row[1]");
                t.Equals(z, 0x33333333u, "fill write Z should use row[2]");
                t.Equals(w, 0x44444444u, "fill write W should use row[3]");
            }
        });

        tc.Run("VIF irq command sets STAT.INT and CODE until FBRST.STC clears it", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            const uint32_t irqMarkCmd = 0x80000000u | makeVifCmd(0x07u, 0x12u, 0x3456u);
            mem.processVIF1Data(reinterpret_cast<const uint8_t *>(&irqMarkCmd), sizeof(irqMarkCmd));

            t.Equals(mem.vif1_regs.code, irqMarkCmd, "VIF CODE should capture the last processed command");
            t.IsTrue((mem.vif1_regs.stat & (1u << 11)) != 0u, "irq bit should raise VIF1 STAT.INT");
            t.Equals(mem.vif1_regs.mark, 0x3456u, "MARK command should still update MARK register");

            t.IsTrue(mem.writeIORegister(0x10003C10u, 0x8u), "FBRST STC write should succeed");
            t.IsTrue((mem.vif1_regs.stat & (1u << 11)) == 0u, "FBRST.STC should clear VIF1 STAT.INT");
        });

        tc.Run("VIF FBRST RST clears VIF1 command state", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            mem.vif1_regs.mark = 0x1234u;
            mem.vif1_regs.cycle = 0x0102u;
            mem.vif1_regs.mode = 2u;
            mem.vif1_regs.num = 7u;
            mem.vif1_regs.mask = 0x89ABCDEFu;
            mem.vif1_regs.code = 0xCAFEBABEu;
            mem.vif1_regs.stat = 0x3F00u;

            t.IsTrue(mem.writeIORegister(0x10003C10u, 0x1u), "FBRST RST write should succeed");

            t.Equals(mem.vif1_regs.mark, 0u, "RST should clear MARK");
            t.Equals(mem.vif1_regs.cycle, 0u, "RST should clear CYCLE");
            t.Equals(mem.vif1_regs.mode, 0u, "RST should clear MODE");
            t.Equals(mem.vif1_regs.num, 0u, "RST should clear NUM");
            t.Equals(mem.vif1_regs.mask, 0u, "RST should clear MASK");
            t.Equals(mem.vif1_regs.code, 0u, "RST should clear CODE");
            t.Equals(mem.vif1_regs.stat, 0u, "RST should clear STAT");
        });

        tc.Run("VIF double-buffer OFFSET BASE and MSCAL update TOPS and ITOPS", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            mem.vif1_regs.tops = 0x120u;
            mem.vif1_regs.stat = (1u << 7); // DBF=1 before OFFSET

            std::vector<std::pair<uint32_t, uint32_t>> mscalCalls;
            mem.setVu1MscalCallback([&](uint32_t startPC, uint32_t itop)
            {
                mscalCalls.emplace_back(startPC, itop);
            });

            const uint32_t offsetCmd = makeVifCmd(0x02u, 0u, 0x0022u);
            mem.processVIF1Data(reinterpret_cast<const uint8_t *>(&offsetCmd), sizeof(offsetCmd));
            t.Equals(mem.vif1_regs.ofst, 0x22u, "OFFSET should update OFST");
            t.Equals(mem.vif1_regs.base, 0x120u, "OFFSET should copy old TOPS into BASE");
            t.IsTrue((mem.vif1_regs.stat & (1u << 7)) == 0u, "OFFSET should clear DBF");
            t.Equals(mem.vif1_regs.tops, 0x120u, "DBF=0 should keep TOPS at BASE");

            const uint32_t baseCmd = makeVifCmd(0x03u, 0u, 0x0030u);
            mem.processVIF1Data(reinterpret_cast<const uint8_t *>(&baseCmd), sizeof(baseCmd));
            t.Equals(mem.vif1_regs.base, 0x30u, "BASE should update BASE register");
            t.Equals(mem.vif1_regs.tops, 0x30u, "DBF=0 keeps TOPS equal to BASE");

            const uint32_t itopCmd = makeVifCmd(0x04u, 0u, 0x0044u);
            mem.processVIF1Data(reinterpret_cast<const uint8_t *>(&itopCmd), sizeof(itopCmd));
            t.Equals(mem.vif1_regs.itop, 0x44u, "ITOP should update ITOP register");

            const uint32_t mscalCmd = makeVifCmd(0x14u, 0u, 0x0003u);
            mem.processVIF1Data(reinterpret_cast<const uint8_t *>(&mscalCmd), sizeof(mscalCmd));
            t.Equals(mscalCalls.size(), static_cast<size_t>(1u), "MSCAL should invoke callback once");
            t.Equals(mscalCalls[0].first, 0x18u, "MSCAL callback startPC should be IMMEDIATE*8");
            t.Equals(mscalCalls[0].second, 0x44u, "MSCAL callback should receive current ITOP");
            t.Equals(mem.vif1_regs.itops, 0x44u, "MSCAL should latch ITOPS from ITOP");
            t.IsTrue((mem.vif1_regs.stat & (1u << 7)) != 0u, "MSCAL should toggle DBF");
            t.Equals(mem.vif1_regs.tops, 0x52u, "DBF=1 should set TOPS to BASE+OFST");

            const uint32_t mscntCmd = makeVifCmd(0x17u, 0u, 0u);
            mem.processVIF1Data(reinterpret_cast<const uint8_t *>(&mscntCmd), sizeof(mscntCmd));
            t.IsTrue((mem.vif1_regs.stat & (1u << 7)) == 0u, "MSCNT should toggle DBF again");
            t.Equals(mem.vif1_regs.tops, 0x30u, "DBF=0 should restore TOPS to BASE");
        });

        tc.Run("VIF MSKPATH3 uses immediate bit15", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            const uint32_t setMask = makeVifCmd(0x06u, 0u, 0x8000u);
            mem.processVIF1Data(reinterpret_cast<const uint8_t *>(&setMask), sizeof(setMask));
            t.IsTrue(mem.isPath3Masked(), "MSKPATH3 with imm bit15 set should enable PATH3 mask");

            const uint32_t clearMask = makeVifCmd(0x06u, 0u, 0x0000u);
            mem.processVIF1Data(reinterpret_cast<const uint8_t *>(&clearMask), sizeof(clearMask));
            t.IsFalse(mem.isPath3Masked(), "MSKPATH3 with imm bit15 clear should disable PATH3 mask");
        });

        tc.Run("PATH3 mask queues packets until unmask", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            std::vector<std::vector<uint8_t>> captured;
            mem.setGifPacketCallback([&](const uint8_t *data, uint32_t sizeBytes)
            {
                captured.emplace_back(data, data + sizeBytes);
            });

            std::vector<uint8_t> packetA(16u);
            std::vector<uint8_t> packetB(16u);
            for (uint32_t i = 0; i < 16u; ++i)
            {
                packetA[i] = static_cast<uint8_t>(0x10u + i);
                packetB[i] = static_cast<uint8_t>(0x40u + i);
            }

            const uint32_t setMask = makeVifCmd(0x06u, 0u, 0x8000u);
            mem.processVIF1Data(reinterpret_cast<const uint8_t *>(&setMask), sizeof(setMask));
            t.IsTrue(mem.isPath3Masked(), "PATH3 mask should be enabled");

            mem.submitGifPacket(GifPathId::Path3, packetA.data(), static_cast<uint32_t>(packetA.size()));
            mem.submitGifPacket(GifPathId::Path3, packetB.data(), static_cast<uint32_t>(packetB.size()));
            t.Equals(captured.size(), static_cast<size_t>(0u), "masked PATH3 packets should be queued, not dropped/emitted");

            const uint32_t clearMask = makeVifCmd(0x06u, 0u, 0x0000u);
            mem.processVIF1Data(reinterpret_cast<const uint8_t *>(&clearMask), sizeof(clearMask));

            t.Equals(captured.size(), static_cast<size_t>(2u), "unmask should flush queued PATH3 packets");
            bool firstOk = true;
            bool secondOk = true;
            for (uint32_t i = 0; i < 16u; ++i)
            {
                if (captured[0][i] != static_cast<uint8_t>(0x10u + i))
                    firstOk = false;
                if (captured[1][i] != static_cast<uint8_t>(0x40u + i))
                    secondOk = false;
            }
            t.IsTrue(firstOk, "first queued PATH3 packet should flush in-order");
            t.IsTrue(secondOk, "second queued PATH3 packet should flush in-order");
        });

        tc.Run("GIF arbiter prioritizes PATH1 then PATH2 then PATH3", [](TestCase &t)
        {
            std::vector<uint8_t> order;
            GifArbiter arbiter([&](const uint8_t *data, uint32_t sizeBytes)
            {
                if (data && sizeBytes > 0u)
                    order.push_back(data[0]);
            });

            const std::vector<uint8_t> p1(16u, 0x11u);
            const std::vector<uint8_t> p2(16u, 0x22u);
            const std::vector<uint8_t> p3(16u, 0x33u);

            arbiter.submit(GifPathId::Path3, p3.data(), static_cast<uint32_t>(p3.size()));
            arbiter.submit(GifPathId::Path2, p2.data(), static_cast<uint32_t>(p2.size()));
            arbiter.submit(GifPathId::Path1, p1.data(), static_cast<uint32_t>(p1.size()));
            arbiter.drain();

            t.Equals(order.size(), static_cast<size_t>(3u), "all queued packets should be drained");
            t.Equals(order[0], static_cast<uint8_t>(0x11u), "PATH1 should be drained first");
            t.Equals(order[1], static_cast<uint8_t>(0x22u), "PATH2 should be drained second");
            t.Equals(order[2], static_cast<uint8_t>(0x33u), "PATH3 should be drained third");
        });

        tc.Run("VIF DIRECTHL stalls behind queued PATH3 IMAGE packets", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            std::vector<uint8_t> firstBytes;
            GifArbiter arbiter([&](const uint8_t *data, uint32_t sizeBytes)
            {
                if (data && sizeBytes > 0u)
                    firstBytes.push_back(data[0]);
            });
            mem.setGifArbiter(&arbiter);

            std::vector<uint8_t> path3Image;
            appendU64(path3Image, makeGifTag(0x00AAu, 2u, 0u, true)); // IMAGE packet marker: first byte 0xAA
            appendU64(path3Image, 0ull);
            mem.submitGifPacket(GifPathId::Path3, path3Image.data(), static_cast<uint32_t>(path3Image.size()), false);

            std::vector<uint8_t> vifPacket;
            appendU32(vifPacket, makeVifCmd(0x51u, 0u, 1u)); // DIRECTHL 1 QW
            for (uint32_t i = 0; i < 16u; ++i)
            {
                vifPacket.push_back(static_cast<uint8_t>(0xD2u + i));
            }
            mem.processVIF1Data(vifPacket.data(), static_cast<uint32_t>(vifPacket.size()));

            t.Equals(firstBytes.size(), static_cast<size_t>(2u), "PATH3 and DIRECTHL packets should both drain");
            t.Equals(firstBytes[0], static_cast<uint8_t>(0xAAu), "DIRECTHL should not preempt queued PATH3 IMAGE packet");
            t.Equals(firstBytes[1], static_cast<uint8_t>(0xD2u), "DIRECTHL packet should drain after PATH3 IMAGE packet");
        });

        tc.Run("GIF DMA mode0 copies RDRAM packet and clears channel", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            constexpr uint32_t kGifCh = 0x1000A000u;
            constexpr uint32_t kSrc = 0x00022000u;
            constexpr uint32_t kQwc = 2u; // 32 bytes

            uint8_t *rdram = mem.getRDRAM();
            for (uint32_t i = 0; i < kQwc * 16u; ++i)
            {
                rdram[kSrc + i] = static_cast<uint8_t>((0x40u + i) & 0xFFu);
            }

            std::vector<std::vector<uint8_t>> captured;
            mem.setGifPacketCallback([&](const uint8_t *data, uint32_t sizeBytes)
            {
                captured.emplace_back(data, data + sizeBytes);
            });

            t.IsTrue(mem.writeIORegister(kGifCh + 0x10u, kSrc), "write MADR should succeed");
            t.IsTrue(mem.writeIORegister(kGifCh + 0x20u, kQwc), "write QWC should succeed");
            t.IsTrue(mem.writeIORegister(kGifCh + 0x00u, 0x100u), "write CHCR STR should succeed");

            t.Equals(mem.dmaStartCount(), 1ull, "starting GIF DMA should increment dmaStartCount");

            mem.processPendingTransfers();

            t.Equals(captured.size(), static_cast<size_t>(1u), "GIF DMA should emit one packet");
            t.Equals(captured[0].size(), static_cast<size_t>(kQwc * 16u), "GIF packet size should match QWC");

            bool contentOk = true;
            for (uint32_t i = 0; i < kQwc * 16u; ++i)
            {
                if (captured[0][i] != static_cast<uint8_t>((0x40u + i) & 0xFFu))
                {
                    contentOk = false;
                    break;
                }
            }
            t.IsTrue(contentOk, "GIF DMA packet bytes should match source RDRAM");
            t.IsTrue(mem.hasSeenGifCopy(), "GIF DMA should mark seen GIF copy");
            t.Equals(mem.gifCopyCount(), 1ull, "GIF DMA should increment gifCopyCount");
            t.IsTrue((mem.readIORegister(kGifCh + 0x00u) & 0x100u) == 0u, "GIF CHCR STR bit should be cleared after drain");
            t.Equals(mem.readIORegister(kGifCh + 0x20u), 0u, "GIF QWC should be cleared after drain");
        });

        tc.Run("GIF DMA can source from scratchpad", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            constexpr uint32_t kGifCh = 0x1000A000u;
            constexpr uint32_t kSrcScratch = PS2_SCRATCHPAD_BASE + 0x80u;
            constexpr uint32_t kQwc = 1u; // 16 bytes

            uint8_t *scratch = mem.getScratchpad();
            for (uint32_t i = 0; i < 16u; ++i)
            {
                scratch[0x80u + i] = static_cast<uint8_t>((0xA0u + i) & 0xFFu);
            }

            std::vector<std::vector<uint8_t>> captured;
            mem.setGifPacketCallback([&](const uint8_t *data, uint32_t sizeBytes)
            {
                captured.emplace_back(data, data + sizeBytes);
            });

            t.IsTrue(mem.writeIORegister(kGifCh + 0x10u, kSrcScratch), "write MADR scratchpad should succeed");
            t.IsTrue(mem.writeIORegister(kGifCh + 0x20u, kQwc), "write QWC should succeed");
            t.IsTrue(mem.writeIORegister(kGifCh + 0x00u, 0x100u), "write CHCR STR should succeed");

            mem.processPendingTransfers();

            t.Equals(captured.size(), static_cast<size_t>(1u), "scratchpad GIF DMA should emit one packet");
            t.Equals(captured[0].size(), static_cast<size_t>(16u), "scratchpad GIF DMA packet should be 16 bytes");
            bool contentOk = true;
            for (uint32_t i = 0; i < 16u; ++i)
            {
                if (captured[0][i] != static_cast<uint8_t>((0xA0u + i) & 0xFFu))
                {
                    contentOk = false;
                    break;
                }
            }
            t.IsTrue(contentOk, "scratchpad GIF DMA packet bytes should match scratchpad source");
        });

        tc.Run("VIF1 DMA DIRECT forwards payload to GIF callback and clears channel", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            constexpr uint32_t kVif1Ch = 0x10009000u;
            constexpr uint32_t kSrc = 0x00024000u;
            constexpr uint32_t kQwc = 2u; // 32 bytes total transport

            uint8_t *rdram = mem.getRDRAM();
            std::memset(rdram + kSrc, 0, kQwc * 16u);

            // DIRECT 1 QW.
            const uint32_t cmd = makeVifCmd(0x50u, 0u, 1u);
            std::memcpy(rdram + kSrc, &cmd, sizeof(cmd));
            for (uint32_t i = 0; i < 16u; ++i)
            {
                rdram[kSrc + 4u + i] = static_cast<uint8_t>((0x11u + i) & 0xFFu);
            }

            std::vector<std::vector<uint8_t>> captured;
            mem.setGifPacketCallback([&](const uint8_t *data, uint32_t sizeBytes)
            {
                captured.emplace_back(data, data + sizeBytes);
            });

            t.IsTrue(mem.writeIORegister(kVif1Ch + 0x10u, kSrc), "write VIF1 MADR should succeed");
            t.IsTrue(mem.writeIORegister(kVif1Ch + 0x20u, kQwc), "write VIF1 QWC should succeed");
            t.IsTrue(mem.writeIORegister(kVif1Ch + 0x00u, 0x100u), "write VIF1 CHCR STR should succeed");

            mem.processPendingTransfers();

            t.Equals(captured.size(), static_cast<size_t>(1u), "VIF1 DIRECT should emit one GIF packet");
            t.Equals(captured[0].size(), static_cast<size_t>(16u), "VIF1 DIRECT packet should be 1 QW");
            bool contentOk = true;
            for (uint32_t i = 0; i < 16u; ++i)
            {
                if (captured[0][i] != static_cast<uint8_t>((0x11u + i) & 0xFFu))
                {
                    contentOk = false;
                    break;
                }
            }
            t.IsTrue(contentOk, "VIF1 DIRECT packet bytes should match payload");
            t.IsTrue((mem.readIORegister(kVif1Ch + 0x00u) & 0x100u) == 0u, "VIF1 CHCR STR bit should be cleared after drain");
            t.Equals(mem.readIORegister(kVif1Ch + 0x20u), 0u, "VIF1 QWC should be cleared after drain");
        });

        tc.Run("GIF DMA chain CALL sources payload from TADR+16", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            constexpr uint32_t kGifCh = 0x1000A000u;
            constexpr uint32_t kTag0 = 0x00026000u;
            constexpr uint32_t kTag1 = 0x00026100u;

            uint8_t *rdram = mem.getRDRAM();

            // CALL qwc=1 addr=kTag1
            writeDmaTag(rdram, kTag0, makeDmaTag(1u, 5u, kTag1, false));
            // END qwc=1
            writeDmaTag(rdram, kTag1, makeDmaTag(1u, 7u, 0u, false));

            for (uint32_t i = 0; i < 16u; ++i)
            {
                rdram[kTag0 + 16u + i] = static_cast<uint8_t>(0x40u + i); // CALL payload
                rdram[kTag1 + 16u + i] = static_cast<uint8_t>(0x80u + i); // END payload
            }

            std::vector<std::vector<uint8_t>> captured;
            mem.setGifPacketCallback([&](const uint8_t *data, uint32_t sizeBytes)
            {
                captured.emplace_back(data, data + sizeBytes);
            });

            t.IsTrue(mem.writeIORegister(kGifCh + 0x30u, kTag0), "write TADR should succeed");
            // STR + CHAIN mode (MOD=1)
            t.IsTrue(mem.writeIORegister(kGifCh + 0x00u, 0x104u), "write CHCR should succeed");

            mem.processPendingTransfers();

            t.Equals(captured.size(), static_cast<size_t>(1u), "chain CALL should emit one packet");
            t.Equals(captured[0].size(), static_cast<size_t>(32u), "CALL+END should emit two qwords");

            bool firstQwOk = true;
            bool secondQwOk = true;
            for (uint32_t i = 0; i < 16u; ++i)
            {
                if (captured[0][i] != static_cast<uint8_t>(0x40u + i))
                    firstQwOk = false;
                if (captured[0][16u + i] != static_cast<uint8_t>(0x80u + i))
                    secondQwOk = false;
            }
            t.IsTrue(firstQwOk, "CALL must transfer from TADR+16, not DMAtag ADDR");
            t.IsTrue(secondQwOk, "END payload should follow CALL payload");
        });

        tc.Run("GIF DMA chain RET transfers payload and resumes after CALL", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            constexpr uint32_t kGifCh = 0x1000A000u;
            constexpr uint32_t kTagCall = 0x00026200u;
            constexpr uint32_t kTagRet = 0x00026300u;
            constexpr uint32_t kTagEnd = 0x00026220u;

            uint8_t *rdram = mem.getRDRAM();

            // CALL qwc=1 -> jumps to RET tag
            writeDmaTag(rdram, kTagCall, makeDmaTag(1u, 5u, kTagRet, false));
            // RET qwc=1 -> should return to kTagEnd
            writeDmaTag(rdram, kTagRet, makeDmaTag(1u, 6u, 0u, false));
            // END qwc=1 after CALL payload
            writeDmaTag(rdram, kTagEnd, makeDmaTag(1u, 7u, 0u, false));

            for (uint32_t i = 0; i < 16u; ++i)
            {
                rdram[kTagCall + 16u + i] = static_cast<uint8_t>(0x11u + i); // CALL payload
                rdram[kTagRet + 16u + i] = static_cast<uint8_t>(0x22u + i);  // RET payload
                rdram[kTagEnd + 16u + i] = static_cast<uint8_t>(0x33u + i);  // END payload
            }

            std::vector<std::vector<uint8_t>> captured;
            mem.setGifPacketCallback([&](const uint8_t *data, uint32_t sizeBytes)
            {
                captured.emplace_back(data, data + sizeBytes);
            });

            t.IsTrue(mem.writeIORegister(kGifCh + 0x30u, kTagCall), "write TADR should succeed");
            t.IsTrue(mem.writeIORegister(kGifCh + 0x00u, 0x104u), "write CHCR should succeed");

            mem.processPendingTransfers();

            t.Equals(captured.size(), static_cast<size_t>(1u), "CALL/RET chain should emit one packet");
            t.Equals(captured[0].size(), static_cast<size_t>(48u), "CALL+RET+END should emit three qwords");

            bool q0 = true;
            bool q1 = true;
            bool q2 = true;
            for (uint32_t i = 0; i < 16u; ++i)
            {
                if (captured[0][i] != static_cast<uint8_t>(0x11u + i))
                    q0 = false;
                if (captured[0][16u + i] != static_cast<uint8_t>(0x22u + i))
                    q1 = false;
                if (captured[0][32u + i] != static_cast<uint8_t>(0x33u + i))
                    q2 = false;
            }
            t.IsTrue(q0, "CALL payload should be first");
            t.IsTrue(q1, "RET must still transfer its own payload");
            t.IsTrue(q2, "RET must resume after CALL payload and continue chain");
        });

        tc.Run("GIF DMA chain IRQ stops only when TIE is set", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            constexpr uint32_t kGifCh = 0x1000A000u;
            constexpr uint32_t kTag0 = 0x00026400u;
            constexpr uint32_t kTag1 = 0x00026410u;
            constexpr uint32_t kRefData = 0x00026500u;

            auto runChain = [&](uint32_t chcrValue, std::vector<uint8_t> &packetOut) -> bool
            {
                uint8_t *rdram = mem.getRDRAM();
                writeDmaTag(rdram, kTag0, makeDmaTag(1u, 3u, kRefData, true)); // REF + IRQ
                writeDmaTag(rdram, kTag1, makeDmaTag(1u, 7u, 0u, false));       // END
                for (uint32_t i = 0; i < 16u; ++i)
                {
                    rdram[kRefData + i] = static_cast<uint8_t>(0x55u + i);
                    rdram[kTag1 + 16u + i] = static_cast<uint8_t>(0x77u + i);
                }

                std::vector<std::vector<uint8_t>> captured;
                mem.setGifPacketCallback([&](const uint8_t *data, uint32_t sizeBytes)
                {
                    captured.emplace_back(data, data + sizeBytes);
                });

                if (!mem.writeIORegister(kGifCh + 0x30u, kTag0))
                    return false;
                if (!mem.writeIORegister(kGifCh + 0x00u, chcrValue))
                    return false;
                mem.processPendingTransfers();
                if (captured.empty())
                    return false;
                packetOut = captured[0];
                return true;
            };

            std::vector<uint8_t> packetNoTie;
            t.IsTrue(runChain(0x104u, packetNoTie), "chain run without TIE should succeed");
            t.Equals(packetNoTie.size(), static_cast<size_t>(32u), "IRQ tag should not stop chain when TIE is clear");

            std::vector<uint8_t> packetTie;
            // STR + CHAIN + TIE(bit7)
            t.IsTrue(runChain(0x184u, packetTie), "chain run with TIE should succeed");
            t.Equals(packetTie.size(), static_cast<size_t>(16u), "IRQ tag should stop chain when TIE is set");
        });

        tc.Run("DMAC D_STAT toggles masks and clears channel status on write-one", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            constexpr uint32_t kDStat = 0x1000E010u;
            constexpr uint32_t kGifMaskBit = (1u << 18); // channel 2 mask
            constexpr uint32_t kGifStatusBit = (1u << 2); // channel 2 status
            constexpr uint32_t kSummaryBit = (1u << 31);

            t.IsTrue(mem.writeIORegister(kDStat, kGifMaskBit), "D_STAT mask toggle write should succeed");
            t.IsTrue((mem.readIORegister(kDStat) & kGifMaskBit) != 0u, "first mask write should enable GIF mask bit");
            t.IsTrue(mem.writeIORegister(kDStat, kGifMaskBit), "D_STAT mask toggle write should succeed");
            t.IsTrue((mem.readIORegister(kDStat) & kGifMaskBit) == 0u, "second mask write should disable GIF mask bit");

            t.IsTrue(mem.writeIORegister(kDStat, kGifMaskBit), "re-enable GIF mask for summary test");

            constexpr uint32_t kGifCh = 0x1000A000u;
            constexpr uint32_t kSrc = 0x00027000u;
            uint8_t *rdram = mem.getRDRAM();
            for (uint32_t i = 0; i < 16u; ++i)
            {
                rdram[kSrc + i] = static_cast<uint8_t>(0x90u + i);
            }

            t.IsTrue(mem.writeIORegister(kGifCh + 0x10u, kSrc), "write MADR should succeed");
            t.IsTrue(mem.writeIORegister(kGifCh + 0x20u, 1u), "write QWC should succeed");
            t.IsTrue(mem.writeIORegister(kGifCh + 0x00u, 0x100u), "write CHCR STR should succeed");

            t.IsTrue((mem.readIORegister(kDStat) & kGifStatusBit) == 0u, "D_STAT status should not set before transfer drain");

            mem.processPendingTransfers();

            const uint32_t dstatAfter = mem.readIORegister(kDStat);
            t.IsTrue((dstatAfter & kGifStatusBit) != 0u, "GIF transfer completion should set D_STAT channel status bit");
            t.IsTrue((dstatAfter & kSummaryBit) != 0u, "status&mask should raise D_STAT summary bit");

            t.IsTrue(mem.writeIORegister(kDStat, kGifStatusBit), "D_STAT status clear write should succeed");
            const uint32_t dstatCleared = mem.readIORegister(kDStat);
            t.IsTrue((dstatCleared & kGifStatusBit) == 0u, "write-one should clear GIF channel status bit");
            t.IsTrue((dstatCleared & kSummaryBit) == 0u, "summary bit should clear after status clear");
        });

        tc.Run("DMAC D_CTRL DMAE gates GIF DMA start", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            constexpr uint32_t kDctrl = 0x1000E000u;
            constexpr uint32_t kGifCh = 0x1000A000u;
            constexpr uint32_t kSrc = 0x00027800u;

            uint8_t *rdram = mem.getRDRAM();
            for (uint32_t i = 0; i < 16u; ++i)
            {
                rdram[kSrc + i] = static_cast<uint8_t>(0xE0u + i);
            }

            std::vector<std::vector<uint8_t>> captured;
            mem.setGifPacketCallback([&](const uint8_t *data, uint32_t sizeBytes)
            {
                captured.emplace_back(data, data + sizeBytes);
            });

            t.IsTrue(mem.writeIORegister(kDctrl, 0u), "clearing D_CTRL.DMAE should succeed");
            t.IsTrue(mem.writeIORegister(kGifCh + 0x10u, kSrc), "write MADR should succeed");
            t.IsTrue(mem.writeIORegister(kGifCh + 0x20u, 1u), "write QWC should succeed");
            t.IsTrue(mem.writeIORegister(kGifCh + 0x00u, 0x100u), "write CHCR STR should succeed");
            mem.processPendingTransfers();

            t.Equals(captured.size(), static_cast<size_t>(0u), "DMAE=0 should prevent GIF DMA transfer");
            t.Equals(mem.dmaStartCount(), 0ull, "DMAE=0 should not increment dmaStartCount");

            t.IsTrue(mem.writeIORegister(kDctrl, 1u), "setting D_CTRL.DMAE should succeed");
            t.IsTrue(mem.writeIORegister(kGifCh + 0x00u, 0x100u), "restarting GIF DMA should succeed");
            mem.processPendingTransfers();

            t.Equals(captured.size(), static_cast<size_t>(1u), "DMAE=1 should allow GIF DMA transfer");
            if (!captured.empty())
            {
                t.Equals(captured[0].size(), static_cast<size_t>(16u), "GIF DMA transfer should emit one qword");
            }
        });

        tc.Run("VU1 XGKICK wraps packet payload across VU1 memory boundary", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            std::vector<std::vector<uint8_t>> captured;
            mem.setGifPacketCallback([&](const uint8_t *data, uint32_t sizeBytes)
            {
                captured.emplace_back(data, data + sizeBytes);
            });

            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            uint8_t *vuCode = mem.getVU1Code();
            uint8_t *vuData = mem.getVU1Data();
            std::memset(vuCode, 0, PS2_VU1_CODE_SIZE);
            std::memset(vuData, 0, PS2_VU1_DATA_SIZE);

            constexpr uint32_t kLastQw = (PS2_VU1_DATA_SIZE / 16u) - 1u;
            const uint32_t tagOffset = kLastQw * 16u;

            const uint64_t imageTag = makeGifTag(1u, GIF_FMT_IMAGE, 0u, true);
            std::memcpy(vuData + tagOffset, &imageTag, sizeof(imageTag));

            for (uint32_t i = 0; i < 16u; ++i)
            {
                vuData[i] = static_cast<uint8_t>(0xC0u + i);
            }

            const uint32_t lower = makeVuLowerSpecial(0x3Du, 1u);
            std::memcpy(vuCode + 0u, &lower, sizeof(lower));
            const uint32_t upper = 0u;
            std::memcpy(vuCode + 4u, &upper, sizeof(upper));

            VU1Interpreter vu1;
            vu1.state().vi[1] = static_cast<int32_t>(kLastQw);
            vu1.execute(vuCode,
                        PS2_VU1_CODE_SIZE,
                        vuData,
                        PS2_VU1_DATA_SIZE,
                        gs,
                        &mem,
                        0u,
                        0u,
                        1u);

            t.Equals(captured.size(), static_cast<size_t>(1u), "XGKICK should emit one wrapped GIF packet");
            if (!captured.empty())
            {
                t.Equals(captured[0].size(), static_cast<size_t>(32u), "wrapped packet should include tag plus one qword payload");
                bool payloadOk = true;
                for (uint32_t i = 0; i < 16u; ++i)
                {
                    if (captured[0].size() < 32u || captured[0][16u + i] != static_cast<uint8_t>(0xC0u + i))
                    {
                        payloadOk = false;
                        break;
                    }
                }
                t.IsTrue(payloadOk, "wrapped payload should be copied from start of VU1 memory");
            }
        });

        tc.Run("VIF1 DMA DIRECT image packet reaches GS through arbiter", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            GS gs;
            gs.init(mem.getGSVRAM(), static_cast<uint32_t>(PS2_GS_VRAM_SIZE), &mem.gs());
            GifArbiter arbiter([&](const uint8_t *data, uint32_t sizeBytes)
            {
                gs.processGIFPacket(data, sizeBytes);
            });
            mem.setGifArbiter(&arbiter);

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

            constexpr uint32_t kVif1Ch = 0x10009000u;
            constexpr uint32_t kSrc = 0x00027C00u;
            constexpr uint32_t kQwc = 3u;

            uint8_t *rdram = mem.getRDRAM();
            std::memset(rdram + kSrc, 0, kQwc * 16u);

            const uint32_t directCmd = makeVifCmd(0x50u, 0u, 2u); // DIRECT 2 QW payload.
            std::memcpy(rdram + kSrc, &directCmd, sizeof(directCmd));

            uint8_t *gifPayload = rdram + kSrc + 4u;
            const uint64_t gifTag = makeGifTag(1u, GIF_FMT_IMAGE, 0u, true);
            std::memcpy(gifPayload + 0u, &gifTag, sizeof(gifTag));
            const uint64_t tagHi = 0u;
            std::memcpy(gifPayload + 8u, &tagHi, sizeof(tagHi));
            for (uint32_t i = 0; i < 16u; ++i)
            {
                gifPayload[16u + i] = static_cast<uint8_t>(0x70u + i);
            }

            t.IsTrue(mem.writeIORegister(kVif1Ch + 0x10u, kSrc), "write VIF1 MADR should succeed");
            t.IsTrue(mem.writeIORegister(kVif1Ch + 0x20u, kQwc), "write VIF1 QWC should succeed");
            t.IsTrue(mem.writeIORegister(kVif1Ch + 0x00u, 0x100u), "write VIF1 CHCR STR should succeed");

            mem.processPendingTransfers();

            const uint8_t *vramOut = mem.getGSVRAM();
            bool imageOk = true;
            for (uint32_t i = 0; i < 16u; ++i)
            {
                if (vramOut[i] != static_cast<uint8_t>(0x70u + i))
                {
                    imageOk = false;
                    break;
                }
            }
            t.IsTrue(imageOk, "VIF1 DIRECT image should update GS VRAM through GIF path2");
        });

        tc.Run("VIF MSCAL callback can execute XGKICK and update GS VRAM", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            GS gs;
            gs.init(mem.getGSVRAM(), static_cast<uint32_t>(PS2_GS_VRAM_SIZE), &mem.gs());
            GifArbiter arbiter([&](const uint8_t *data, uint32_t sizeBytes)
            {
                gs.processGIFPacket(data, sizeBytes);
            });
            mem.setGifArbiter(&arbiter);

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

            uint8_t *vuCode = mem.getVU1Code();
            uint8_t *vuData = mem.getVU1Data();
            std::memset(vuCode, 0, PS2_VU1_CODE_SIZE);
            std::memset(vuData, 0, PS2_VU1_DATA_SIZE);

            const uint32_t lower = makeVuLowerSpecial(0x3Du, 0u);
            std::memcpy(vuCode + 0u, &lower, sizeof(lower));
            const uint32_t upper = 0u;
            std::memcpy(vuCode + 4u, &upper, sizeof(upper));

            const uint64_t gifTag = makeGifTag(1u, GIF_FMT_IMAGE, 0u, true);
            std::memcpy(vuData + 0u, &gifTag, sizeof(gifTag));
            const uint64_t tagHi = 0u;
            std::memcpy(vuData + 8u, &tagHi, sizeof(tagHi));
            for (uint32_t i = 0; i < 16u; ++i)
            {
                vuData[16u + i] = static_cast<uint8_t>(0x90u + i);
            }

            VU1Interpreter vu1;
            mem.setVu1MscalCallback([&](uint32_t startPC, uint32_t itop)
            {
                vu1.execute(vuCode,
                            PS2_VU1_CODE_SIZE,
                            vuData,
                            PS2_VU1_DATA_SIZE,
                            gs,
                            &mem,
                            startPC,
                            itop,
                            1u);
            });

            const uint32_t mscalCmd = makeVifCmd(0x14u, 0u, 0u);
            mem.processVIF1Data(reinterpret_cast<const uint8_t *>(&mscalCmd), sizeof(mscalCmd));

            const uint8_t *vramOut = mem.getGSVRAM();
            bool imageOk = true;
            for (uint32_t i = 0; i < 16u; ++i)
            {
                if (vramOut[i] != static_cast<uint8_t>(0x90u + i))
                {
                    imageOk = false;
                    break;
                }
            }
            t.IsTrue(imageOk, "MSCAL-triggered XGKICK should route PATH1 packet into GS VRAM");
        });

        tc.Run("unaligned accesses throw", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            bool threwRead32 = false;
            bool threwWrite64 = false;
            try
            {
                (void)mem.read32(0x00000002u);
            }
            catch (const std::exception &)
            {
                threwRead32 = true;
            }

            try
            {
                mem.write64(0x00000004u + 2u, 0x1122334455667788ull);
            }
            catch (const std::exception &)
            {
                threwWrite64 = true;
            }

            t.IsTrue(threwRead32, "unaligned read32 should throw");
            t.IsTrue(threwWrite64, "unaligned write64 should throw");
        });
    });
}
