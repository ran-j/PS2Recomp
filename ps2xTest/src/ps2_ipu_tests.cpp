#include "MiniTest.h"
#include "runtime/ps2_memory.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// The EE IPU driven only through PS2Memory, the way a guest drives it: the
// IPU registers, the FIFO ports and DMA ch.3/ch.4. Every bitstream below is
// hand-assembled from literal ISO/IEC 13818-2 codes (tables B.1, B.2, B.12,
// B.13, B.14, B.15) and every expected pixel is computed by hand in the
// comments, so a wrong table entry or formula in the decoder cannot also be
// baked into an expected value.
//
// Hand arithmetic used throughout:
// - An intra 8x8 block whose only coefficients are F(0,0) and F(4,4) (or
//   F(0,4), F(4,0)) has the exact IDCT
//     f(x,y) = F(0,0)/8 + F(4,4)/8 * s(x)s(y) [+ F(0,4)/8 * s(x) + F(4,0)/8 * s(y)]
//   with s = sign(cos((2n+1)pi/4)) = +,-,-,+,+,-,-,+ for n = 0..7.
// - IPU colour conversion (Cb = Cr = 128): R = G = B = ((0x95*(Y-16) >> 6) + 1) >> 1,
//   so Y 126 -> 128, 127 -> 129, 128 -> 130, 129 -> 132, 130 -> 133,
//   131 -> 134, 81 -> 76, 200 -> 214. Alpha is 0x80.
namespace
{
    constexpr uint32_t kIpuCmd = 0x10002000u;
    constexpr uint32_t kIpuCmdHi = 0x10002004u;
    constexpr uint32_t kIpuCtrl = 0x10002010u;
    constexpr uint32_t kIpuBp = 0x10002020u;
    constexpr uint32_t kIpuTop = 0x10002030u;
    constexpr uint32_t kIpuTopHi = 0x10002034u;
    constexpr uint32_t kIpuOutFifo = 0x10007000u;
    constexpr uint32_t kIpuInFifo = 0x10007010u;
    constexpr uint32_t kD3Chcr = 0x1000B000u;
    constexpr uint32_t kD3Madr = 0x1000B010u;
    constexpr uint32_t kD3Qwc = 0x1000B020u;
    constexpr uint32_t kD4Chcr = 0x1000B400u;
    constexpr uint32_t kD4Qwc = 0x1000B420u;
    constexpr uint32_t kD4Tadr = 0x1000B430u;
    constexpr uint32_t kDStat = 0x1000E010u;

    constexpr uint32_t kBusy = 1u << 31;
    constexpr uint32_t kCtrlRst = 1u << 30;
    constexpr uint32_t kCtrlScd = 1u << 15;
    constexpr uint32_t kCtrlEcd = 1u << 14;
    constexpr uint32_t kChcrStr = 0x100u;

    constexpr uint32_t kCmdBclr = 0x00000000u;
    constexpr uint32_t kCmdIdec = 0x10010000u; // IDEC, QSC 1, RGB32, no DTD/SGN, FB 0
    constexpr uint32_t kCmdFdec = 0x40000000u;
    constexpr uint32_t kCmdSetiqIntra = 0x50000000u;
    constexpr uint32_t kCmdSetiqNonIntra = 0x58000000u;
    constexpr uint32_t kCmdSetth = 0x90000000u;
    constexpr uint32_t kMacroblockQw = 64u; // 16x16 RGB32

    constexpr int kCos4Sign[8] = {1, -1, -1, 1, 1, -1, -1, 1};

    using Rgba = std::array<uint8_t, 4>;

    Rgba grey(uint8_t v)
    {
        return Rgba{v, v, v, 0x80};
    }

    // MSB-first writer for hand-assembled IPU streams.
    struct BitWriter
    {
        std::vector<uint8_t> bytes;
        uint32_t acc = 0;
        unsigned count = 0;

        void put(uint32_t value, unsigned bits)
        {
            for (int i = static_cast<int>(bits) - 1; i >= 0; --i)
            {
                acc = (acc << 1) | ((value >> i) & 1u);
                if (++count == 8)
                {
                    bytes.push_back(static_cast<uint8_t>(acc));
                    acc = 0;
                    count = 0;
                }
            }
        }

        // Literal bit string, e.g. "01 00001"; spaces are ignored.
        BitWriter &bits(const char *literal)
        {
            for (; *literal; ++literal)
                if (*literal == '0' || *literal == '1')
                    put(*literal == '1' ? 1u : 0u, 1);
            return *this;
        }

        // Zero-pad to a byte, append the 0x000001B0 end code, zero-pad to a
        // quadword: the layout a picture has in the IPU input FIFO.
        std::vector<uint8_t> finishPicture()
        {
            while (count != 0)
                put(0, 1);
            put(0x000001B0u, 32);
            while (bytes.size() % 16u != 0u)
                bytes.push_back(0);
            return bytes;
        }
    };

    // Two DC-only macroblocks, MPEG-2 defaults (8-bit DC, linear quantiser,
    // B.14, zigzag, frame DCT):
    //   MB0: Y 200, Cb = Cr = 128 -> (214,214,214,0x80)
    //   MB1: Y 81, Cb 90, Cr 240  -> lum = 0x95*65>>6 = 151,
    //        R = (151 + (0xCC*112>>6 = 357) + 1)>>1 = 254,
    //        G = (151 + (-0x68*112>>6 = -182) + (-0x32*-38>>6 = 29) + 1)>>1 = -1 -> 0,
    //        B = (151 + (0x102*-38>>6 = -154) + 1)>>1 = -1 -> 0  -> (254,0,0,0x80)
    std::vector<uint8_t> twoDcOnlyMacroblocks(bool withFlagsByte)
    {
        BitWriter w;
        if (withFlagsByte)
            w.bits("00000000"); // the per-picture flags byte a movie player reads with FDEC
        w.bits("01 00001");               // MB0: B.2 intra+quant, quantiser_scale_code 1
        w.bits("111110 1001000 10");      // Y0: B.12 size 7, +72 (128 -> 200), B.14 EOB
        w.bits("100 10 100 10 100 10");   // Y1-Y3: B.12 size 0, EOB
        w.bits("00 10 00 10");            // Cb, Cr: B.13 size 0, EOB
        w.bits("1");                      // B.1 macroblock_address_increment 1
        w.bits("1");                      // MB1: B.2 intra
        w.bits("111110 0001000 10");      // Y0: size 7, 8 - 127 = -119 (200 -> 81)
        w.bits("100 10 100 10 100 10");   // Y1-Y3
        w.bits("111110 011001 10");       // Cb: B.13 size 6, 25 - 63 = -38 (128 -> 90)
        w.bits("1111110 1110000 10");     // Cr: B.13 size 7, +112 (128 -> 240)
        return w.finishPicture();
    }

    const Rgba kMb0Grey = grey(214);
    const Rgba kMb1Red = {254, 0, 0, 0x80};

    // Empty when every pixel of the 16x16 RGB32 macroblock equals
    // expected(x, y); otherwise describes the first mismatch.
    template <typename Expected>
    std::string macroblockMismatch(const uint8_t *mb, Expected expected)
    {
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
            {
                const Rgba want = expected(x, y);
                const uint8_t *got = mb + (y * 16 + x) * 4;
                if (std::memcmp(got, want.data(), 4) != 0)
                {
                    char text[128];
                    std::snprintf(text, sizeof(text), " (pixel %d,%d is %d,%d,%d,%d; expected %d,%d,%d,%d)", x, y,
                                  got[0], got[1], got[2], got[3], want[0], want[1], want[2], want[3]);
                    return text;
                }
            }
        return {};
    }

    struct IpuFixture
    {
        PS2Memory mem;
        bool ok = mem.initialize();

        uint32_t r32(uint32_t address) { return mem.read32(address); }
        void w32(uint32_t address, uint32_t value) { mem.write32(address, value); }

        // Through the 0x10007010 input FIFO port, one quadword per store.
        void feed(const uint8_t *data, size_t bytes)
        {
            for (size_t off = 0; off < bytes; off += 16u)
                mem.write128(kIpuInFifo, _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + off)));
        }
        void feed(const std::vector<uint8_t> &data) { feed(data.data(), data.size()); }

        // Through the 0x10007000 output FIFO port.
        std::vector<uint8_t> drain(size_t quadwords)
        {
            std::vector<uint8_t> out(quadwords * 16u);
            for (size_t q = 0; q < quadwords; ++q)
                _mm_storeu_si128(reinterpret_cast<__m128i *>(out.data() + q * 16u), mem.read128(kIpuOutFifo));
            return out;
        }

        void resetIpu()
        {
            w32(kIpuCtrl, kCtrlRst);
            w32(kIpuCmd, kCmdBclr);
        }

        void setIntraMatrix(const std::array<uint8_t, 64> &zigzagOrder)
        {
            feed(zigzagOrder.data(), zigzagOrder.size());
            w32(kIpuCmd, kCmdSetiqIntra);
        }
    };

    std::array<uint8_t, 64> flatMatrix(uint8_t value)
    {
        std::array<uint8_t, 64> m{};
        m.fill(value);
        return m;
    }

    uint32_t bigEndian32(const std::vector<uint8_t> &bytes, size_t offset)
    {
        return (static_cast<uint32_t>(bytes[offset]) << 24) | (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
               (static_cast<uint32_t>(bytes[offset + 2]) << 8) | bytes[offset + 3];
    }
}

void register_ps2_ipu_tests()
{
    MiniTest::Case("PS2Ipu", [](TestCase &tc)
    {
        tc.Run("IPU_CTRL RST self-clears and keeps the writable config bits", [](TestCase &t)
        {
            IpuFixture f;
            t.IsTrue(f.ok, "PS2Memory initialize should succeed");

            // RST plus IDP 2, AS, IVF, QST, MP1 and PCT 1 (I-picture).
            f.w32(kIpuCtrl, kCtrlRst | 0x01F20000u);
            t.Equals(f.r32(kIpuCtrl), 0x01F20000u, "RST must read back clear; IDP/AS/IVF/QST/MP1/PCT are kept");

            f.w32(kIpuCtrl, kCtrlRst);
            t.Equals(f.r32(kIpuCtrl), 0u, "a plain RST leaves IPU_CTRL idle");
            t.Equals(f.r32(kIpuBp), 0u, "RST leaves BP, IFC and FP at 0");
            t.Equals(f.r32(kIpuTopHi), kBusy, "with an empty FIFO IPU_TOP reports no valid bits");
        });

        tc.Run("BCLR, FDEC, IPU_BP and IPU_TOP track the bitstream position", [](TestCase &t)
        {
            IpuFixture f;
            t.IsTrue(f.ok, "PS2Memory initialize should succeed");
            f.resetIpu();

            std::vector<uint8_t> data(48);
            for (size_t i = 0; i < data.size(); ++i)
                data[i] = static_cast<uint8_t>(i * 0x11u);
            f.feed(data);
            // Three quadwords queued: two in the IPU's internal buffer (FP),
            // one left in the FIFO (IFC).
            t.Equals(f.r32(kIpuBp), 0x00020100u, "3 QW queued: BP 0, IFC 1, FP 2");
            t.Equals(f.r32(kIpuCtrl) & 0xFu, 1u, "IPU_CTRL.IFC mirrors IPU_BP.IFC");
            t.Equals(f.r32(kIpuTop), 0x00112233u, "IPU_TOP shows the next 32 bits");
            t.Equals(f.r32(kIpuTopHi), 0u, "IPU_TOP is valid with 32+ bits queued");
            t.Equals(f.r32(kIpuCmd), 0x00112233u, "after BCLR an IPU_CMD read returns the next 32 bits");

            // BCLR empties the FIFO and sets BP to its option bits.
            f.w32(kIpuCmd, kCmdBclr | 16u);
            t.Equals(f.r32(kIpuBp), 16u, "BCLR 16: BP 16, FIFO empty");
            t.Equals(f.r32(kIpuTopHi), kBusy, "no bits queued after BCLR");

            f.feed(data.data(), 16u);
            t.Equals(f.r32(kIpuTop), 0x22334455u, "decoding starts at bit 16 of the first quadword");

            // FDEC skips FB bits, then DATA = TOP = the next 32 bits, unconsumed.
            f.w32(kIpuCmd, kCmdFdec | 4u);
            t.Equals(f.r32(kIpuCmd), 0x23344556u, "FDEC 4: bits 20..51");
            t.Equals(f.r32(kIpuCmdHi), 0u, "FDEC with enough input is not busy");
            t.Equals(f.r32(kIpuTop), 0x23344556u, "IPU_TOP matches the FDEC result");
            t.Equals(f.r32(kIpuBp), 0x00010014u, "BP 20, FP 1: FDEC does not consume the 32 bits it returns");
            f.w32(kIpuCmd, kCmdFdec);
            t.Equals(f.r32(kIpuCmd), 0x23344556u, "FDEC 0 returns the same bits again");
        });

        tc.Run("FDEC on an empty FIFO stays busy until input arrives", [](TestCase &t)
        {
            IpuFixture f;
            t.IsTrue(f.ok, "PS2Memory initialize should succeed");
            f.resetIpu();

            f.w32(kIpuCmd, kCmdFdec);
            t.Equals(f.mem.read64(kIpuCmd) >> 63, 1ull, "64-bit IPU_CMD read: BUSY while FDEC waits for input");
            t.Equals(f.r32(kIpuCtrl) & kBusy, kBusy, "IPU_CTRL.BUSY while FDEC waits for input");
            t.Equals(f.mem.read64(kIpuTop) >> 63, 1ull, "64-bit IPU_TOP read: not valid while FDEC waits");

            const uint8_t qw[16] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
            f.feed(qw, sizeof(qw));
            t.Equals(f.mem.read64(kIpuCmd), 0x12345678ull, "the input completes FDEC: DATA set, BUSY clear");
            t.Equals(f.r32(kIpuCtrl) & kBusy, 0u, "IPU_CTRL.BUSY clears");
            t.Equals(f.mem.read64(kIpuTop), 0x12345678ull, "64-bit IPU_TOP read: valid next 32 bits");
        });

        tc.Run("IDEC decodes DC-only macroblocks to flat RGB32 blocks", [](TestCase &t)
        {
            IpuFixture f;
            t.IsTrue(f.ok, "PS2Memory initialize should succeed");
            f.resetIpu();
            f.feed(twoDcOnlyMacroblocks(true));
            f.w32(kIpuCmd, kCmdIdec | 8u); // FB 8 skips the flags byte

            const uint32_t ctrl = f.r32(kIpuCtrl);
            t.Equals(ctrl & kBusy, kBusy, "BUSY while decoded macroblocks wait in the output FIFO");
            t.Equals((ctrl >> 4) & 0xFu, 8u, "OFC reports a full 8-quadword output FIFO");
            t.Equals((ctrl >> 8) & 0x3Fu, 0x3Fu, "CBP of an intra macroblock is 0x3F");
            t.Equals(ctrl & (kCtrlScd | kCtrlEcd), kCtrlScd, "the run ends on a start code: SCD, no ECD");

            const std::vector<uint8_t> out = f.drain(2u * kMacroblockQw);
            t.IsTrue(macroblockMismatch(out.data(), [](int, int) { return kMb0Grey; }).empty(),
                     "MB0 is Y 200 -> (214,214,214,0x80)" + macroblockMismatch(out.data(), [](int, int) { return kMb0Grey; }));
            t.IsTrue(macroblockMismatch(out.data() + 1024, [](int, int) { return kMb1Red; }).empty(),
                     "MB1 is Y 81 Cb 90 Cr 240 -> (254,0,0,0x80)" +
                         macroblockMismatch(out.data() + 1024, [](int, int) { return kMb1Red; }));
            t.Equals(f.r32(kIpuCtrl) & (kBusy | 0xF0u), 0u, "BUSY and OFC clear once the output is drained");
            t.Equals(f.r32(kIpuTop), 0x000001B0u, "IPU_TOP holds the start code that ended the run");
        });

        tc.Run("IDEC issued before its input stays busy and decodes once the stream is complete", [](TestCase &t)
        {
            IpuFixture f;
            t.IsTrue(f.ok, "PS2Memory initialize should succeed");
            f.resetIpu();
            const std::vector<uint8_t> stream = twoDcOnlyMacroblocks(false);
            t.Equals(stream.size(), size_t{32}, "the stream is two quadwords");

            f.w32(kIpuCmd, kCmdIdec);
            t.Equals(f.r32(kIpuCtrl) & kBusy, kBusy, "IDEC with no input is busy");
            f.feed(stream.data(), 16u);
            // Both macroblocks fit in the first quadword, but the end-of-run
            // start code does not: IDEC has not finished.
            t.Equals(f.r32(kIpuCtrl) & (kBusy | kCtrlScd), kBusy, "still busy after one quadword, no start code yet");
            f.feed(stream.data() + 16, 16u);
            t.Equals(f.r32(kIpuCtrl) & (kBusy | kCtrlScd | kCtrlEcd), kBusy | kCtrlScd,
                     "the second quadword completes IDEC on the start code; BUSY until the drain");

            const std::vector<uint8_t> out = f.drain(2u * kMacroblockQw);
            t.IsTrue(macroblockMismatch(out.data(), [](int, int) { return kMb0Grey; }).empty() &&
                         macroblockMismatch(out.data() + 1024, [](int, int) { return kMb1Red; }).empty(),
                     "both macroblocks decode as when the input came first");
            t.Equals(f.r32(kIpuCtrl) & kBusy, 0u, "BUSY clears after the drain");
        });

        tc.Run("SETIQ loads the intra matrix in zigzag order and IDEC weights AC coefficients with it", [](TestCase &t)
        {
            IpuFixture f;
            t.IsTrue(f.ok, "PS2Memory initialize should succeed");
            f.resetIpu();

            // One macroblock whose Y0 has DC 128 and a single AC coefficient
            // at raster (4,4) = zigzag index 39; every other block is flat 128.
            BitWriter w;
            w.bits("01 00001");                    // intra+quant, quantiser_scale_code 1 (quantiser_scale 2)
            w.bits("100");                         // Y0 DC: B.12 size 0 (128, F(0,0) = 1024)
            w.bits("000001 100110 000000000001");  // escape, run 38, level +1 -> zigzag index 39
            w.bits("10");                          // EOB
            w.bits("100 10 100 10 100 10");        // Y1-Y3: 128
            w.bits("00 10 00 10");                 // Cb, Cr: 128
            const std::vector<uint8_t> picture = w.finishPicture();

            // Intra matrix: 16 everywhere but index 39. SETIQ must take exactly
            // 64 bytes: the quadword behind it stays queued.
            std::array<uint8_t, 64> intra = flatMatrix(16);
            intra[39] = 137;
            f.feed(intra.data(), intra.size());
            const uint8_t marker[16] = {0xDE, 0xAD, 0xBE, 0xEF};
            f.feed(marker, sizeof(marker));
            f.w32(kIpuCmd, kCmdSetiqIntra);
            t.Equals(f.r32(kIpuCtrl) & kBusy, 0u, "SETIQ with 64 bytes queued completes");
            t.Equals(f.r32(kIpuTop), 0xDEADBEEFu, "SETIQ consumed exactly 64 bytes");
            f.w32(kIpuCmd, kCmdBclr);

            // A non-intra matrix must not touch the intra one.
            const std::array<uint8_t, 64> nonIntra = flatMatrix(255);
            f.feed(nonIntra.data(), nonIntra.size());
            f.w32(kIpuCmd, kCmdSetiqNonIntra);
            t.Equals(f.r32(kIpuBp), 0u, "the non-intra SETIQ consumed its 64 bytes");

            // F(4,4) = (1 * 2 * 137) >> 4 = 17; 1024 + 17 is odd, so no
            // mismatch control. f = 128 +- 17/8 = 130.125 / 125.875 -> 130 / 126
            // -> RGB 133 / 128; the other blocks are 128 -> 130.
            f.feed(picture);
            f.w32(kIpuCmd, kCmdIdec);
            std::vector<uint8_t> out = f.drain(kMacroblockQw);
            auto weighted = [](uint8_t high, uint8_t low)
            {
                return [high, low](int x, int y)
                {
                    if (x >= 8 || y >= 8)
                        return grey(130);
                    return grey(kCos4Sign[x] * kCos4Sign[y] > 0 ? high : low);
                };
            };
            t.IsTrue(macroblockMismatch(out.data(), weighted(133, 128)).empty(),
                     "W(4,4) = 137 gives the 130/126 checkerboard in Y0" + macroblockMismatch(out.data(), weighted(133, 128)));

            // Same picture with W(4,4) = 41: F(4,4) = 82 >> 4 = 5, odd sum;
            // f = 128 +- 0.625 -> 129 / 127 -> RGB 132 / 129. BCLR drops the
            // end code the first IDEC stopped at.
            intra[39] = 41;
            f.w32(kIpuCmd, kCmdBclr);
            f.setIntraMatrix(intra);
            f.feed(picture);
            f.w32(kIpuCmd, kCmdIdec);
            out = f.drain(kMacroblockQw);
            t.IsTrue(macroblockMismatch(out.data(), weighted(132, 129)).empty(),
                     "W(4,4) = 41 gives the 129/127 checkerboard in Y0" + macroblockMismatch(out.data(), weighted(132, 129)));
        });

        tc.Run("IDEC honours IPU_CTRL IDP, AS, IVF and QST", [](TestCase &t)
        {
            IpuFixture f;
            t.IsTrue(f.ok, "PS2Memory initialize should succeed");
            f.resetIpu();
            f.setIntraMatrix(flatMatrix(16));
            // IDP 1 (9-bit DC), AS (alternate scan), IVF (table B.15), QST (non-linear quantiser).
            f.w32(kIpuCtrl, 0x00710000u);

            BitWriter w;
            w.bits("01 00111");                    // intra+quant, quantiser_scale_code 7 -> non-linear 7
            w.bits("100");                         // Y0 DC: size 0 (9-bit predictor 256, F(0,0) = 1024)
            w.bits("000001 101001 000000000001");  // escape, run 41, level +1 -> alternate index 42 = raster (4,4)
            w.bits("0110");                        // B.15 EOB
            w.bits("1111110 10010000 0110");       // Y1: B.12 size 8, +144 (256 -> 400, i.e. 200)
            w.bits("100 0110 100 0110");           // Y2, Y3: 200
            w.bits("00 0110 00 0110");             // Cb, Cr: 256 -> 128
            f.feed(w.finishPicture());
            f.w32(kIpuCmd, kCmdIdec);
            t.Equals(f.r32(kIpuCtrl) & kCtrlEcd, 0u, "the B.15 stream parses without error");

            // F(4,4) = (1 * 7 * 16) >> 4 = 7 (linear would be 14); odd sum.
            // Y0: 128 +- 7/8 -> 129 / 127 -> RGB 132 / 129. Y1-Y3: 200 -> 214.
            const std::vector<uint8_t> out = f.drain(kMacroblockQw);
            auto expected = [](int x, int y)
            {
                if (x >= 8 || y >= 8)
                    return grey(214);
                return grey(kCos4Sign[x] * kCos4Sign[y] > 0 ? 132 : 129);
            };
            t.IsTrue(macroblockMismatch(out.data(), expected).empty(),
                     "alternate scan, non-linear quantiser, 9-bit DC and B.15 decode as computed" +
                         macroblockMismatch(out.data(), expected));
        });

        tc.Run("IDEC in MPEG-1 mode uses MPEG-1 escapes and oddification without mismatch control", [](TestCase &t)
        {
            IpuFixture f;
            t.IsTrue(f.ok, "PS2Memory initialize should succeed");
            f.resetIpu();
            f.setIntraMatrix(flatMatrix(16));
            f.w32(kIpuCtrl, 0x00800000u); // MP1

            BitWriter w;
            w.bits("01 00101");                // intra+quant, quantiser_scale_code 5
            w.bits("100");                     // Y0 DC: 128
            w.bits("0000101 0");               // B.14 run 9 level +1 -> zigzag index 10 = raster (4,0)
            w.bits("00111 0");                 // B.14 run 3 level +1 -> zigzag index 14 = raster (0,4)
            w.bits("000001 011000 00000001");  // MPEG-1 escape: run 24, 8-bit level +1 -> zigzag 39 = (4,4)
            w.bits("10");                      // EOB
            w.bits("100 10 100 10 100 10");    // Y1-Y3: 128
            w.bits("00 10 00 10");             // Cb, Cr: 128
            f.feed(w.finishPicture());
            f.w32(kIpuCmd, kCmdIdec);
            t.Equals(f.r32(kIpuCtrl) & kCtrlEcd, 0u, "the MPEG-1 stream parses without error");

            // Each AC: (2 * 1 * 5 * 16) / 16 = 10, even -> oddified to 9.
            // Y0: 128 + 9/8 * (s(x) + s(y) + s(x)s(y)) = 131.375 where both
            // signs are + (-> 131 -> RGB 134), else 126.875 (-> 127 -> RGB 129).
            // Without oddification the + pixels would be 131.75 -> 132.
            const std::vector<uint8_t> out = f.drain(kMacroblockQw);
            auto expected = [](int x, int y)
            {
                if (x >= 8 || y >= 8)
                    return grey(130);
                return grey(kCos4Sign[x] > 0 && kCos4Sign[y] > 0 ? 134 : 129);
            };
            t.IsTrue(macroblockMismatch(out.data(), expected).empty(),
                     "MPEG-1 intra block decodes as computed" + macroblockMismatch(out.data(), expected));
        });

        tc.Run("IDEC with DTD reads dct_type and places field-DCT luma blocks on alternate lines", [](TestCase &t)
        {
            IpuFixture f;
            t.IsTrue(f.ok, "PS2Memory initialize should succeed");
            f.resetIpu();

            BitWriter w;
            w.bits("01");                   // intra+quant
            w.bits("1");                    // dct_type: field
            w.bits("00001");                // quantiser_scale_code 1
            w.bits("111110 1001000 10");    // Y0: 200
            w.bits("100 10");               // Y1: 200
            w.bits("111110 0001000 10");    // Y2: 81
            w.bits("100 10");               // Y3: 81
            w.bits("00 10 00 10");          // Cb, Cr: 128
            f.feed(w.finishPicture());
            f.w32(kIpuCmd, kCmdIdec | (1u << 24)); // DTD

            // Field DCT: Y0/Y1 are the even lines, Y2/Y3 the odd ones.
            const std::vector<uint8_t> out = f.drain(kMacroblockQw);
            auto expected = [](int, int y) { return grey((y & 1) == 0 ? 214 : 76); };
            t.IsTrue(macroblockMismatch(out.data(), expected).empty(),
                     "even lines Y 200 (214), odd lines Y 81 (76)" + macroblockMismatch(out.data(), expected));
        });

        tc.Run("SETTH alpha thresholds and IDEC SGN apply to the RGB32 output", [](TestCase &t)
        {
            IpuFixture f;
            t.IsTrue(f.ok, "PS2Memory initialize should succeed");
            f.resetIpu();
            const std::vector<uint8_t> stream = twoDcOnlyMacroblocks(false);

            // TH0 215, TH1 300: (214,214,214) is below TH0 -> transparent black;
            // (254,0,0) is not, but is below TH1 -> alpha 0x40.
            f.w32(kIpuCmd, kCmdSetth | 215u | (300u << 16));
            f.feed(stream);
            f.w32(kIpuCmd, kCmdIdec);
            std::vector<uint8_t> out = f.drain(2u * kMacroblockQw);
            t.IsTrue(macroblockMismatch(out.data(), [](int, int) { return Rgba{0, 0, 0, 0}; }).empty(),
                     "below TH0: (0,0,0,0)");
            t.IsTrue(macroblockMismatch(out.data() + 1024, [](int, int) { return Rgba{254, 0, 0, 0x40}; }).empty(),
                     "between TH0 and TH1: alpha 0x40");

            // SETTH 0 turns the thresholds off; SGN flips the colour MSBs.
            f.w32(kIpuCmd, kCmdBclr);
            f.w32(kIpuCmd, kCmdSetth);
            f.feed(stream);
            f.w32(kIpuCmd, kCmdIdec | (1u << 25)); // SGN
            out = f.drain(2u * kMacroblockQw);
            t.IsTrue(macroblockMismatch(out.data(), [](int, int) { return Rgba{86, 86, 86, 0x80}; }).empty(),
                     "SGN: 214 ^ 0x80 = 86");
            t.IsTrue(macroblockMismatch(out.data() + 1024, [](int, int) { return Rgba{126, 128, 128, 0x80}; }).empty(),
                     "SGN: (254,0,0) -> (126,128,128)");
        });

        tc.Run("DMA ch.4 source chain feeds the IPU and ch.3 holds STR until the macroblock arrives", [](TestCase &t)
        {
            IpuFixture f;
            t.IsTrue(f.ok, "PS2Memory initialize should succeed");
            f.resetIpu();
            uint8_t *rdram = f.mem.getRDRAM();
            uint8_t *spr = f.mem.getScratchpad();

            // The movie-player sequence: a REF tag for the first quadword in
            // RDRAM, a REFE tag for the second in the scratchpad (ADDR bit 31).
            const std::vector<uint8_t> stream = twoDcOnlyMacroblocks(true);
            t.Equals(stream.size(), size_t{32}, "the stream is two quadwords");
            std::memcpy(rdram + 0x00100000u, stream.data(), 16);
            std::memcpy(spr + 0x100u, stream.data() + 16, 16);
            const uint64_t refTag = 1ull | (3ull << 28) | (0x00100000ull << 32);
            const uint64_t refeTag = 1ull | (0ull << 28) | (0x80000100ull << 32);
            std::memcpy(rdram + 0x00080000u, &refTag, sizeof(refTag));
            std::memcpy(rdram + 0x00080010u, &refeTag, sizeof(refeTag));

            f.w32(kD4Tadr, 0x00080000u);
            f.w32(kD4Qwc, 0u);
            f.w32(kD4Chcr, 0x105u); // STR, source chain, from memory
            t.Equals(f.r32(kD4Chcr) & kChcrStr, 0u, "ch.4 completes");
            t.Equals(f.r32(kD4Tadr), 0x00080020u, "TADR is past the REFE tag");
            t.Equals(f.r32(kDStat) & (1u << 4), 1u << 4, "D_STAT.CIS4 is set");
            const uint32_t bp = f.r32(kIpuBp);
            t.Equals(((bp >> 8) & 0xFu) + ((bp >> 16) & 0x3u), 2u, "both quadwords reached the IPU (IFC + FP)");
            t.Equals(bp & 0x7Fu, 0u, "decoding starts at bit 0");
            t.Equals(f.r32(kIpuTop), bigEndian32(stream, 0), "IPU_TOP shows the start of the stream");

            // FDEC 0 reads the flags byte, FDEC 8 skips it, then IDEC.
            f.w32(kIpuCmd, kCmdFdec);
            t.Equals(f.r32(kIpuCmd) >> 24, 0u, "FDEC 0 returns the flags byte in DATA[31:24]");
            f.w32(kIpuCmd, kCmdFdec | 8u);
            f.w32(kIpuCtrl, 0u);

            // ch.3 started before there is any output must stay busy.
            f.w32(kD3Madr, 0x00200000u);
            f.w32(kD3Qwc, kMacroblockQw);
            f.w32(kD3Chcr, kChcrStr);
            t.Equals(f.r32(kD3Chcr) & kChcrStr, kChcrStr, "ch.3 STR stays set while the IPU has no output");
            t.Equals(f.r32(kD3Chcr) & kChcrStr, kChcrStr, "reading CHCR does not clear STR");

            f.w32(kIpuCmd, kCmdIdec);
            t.Equals(f.r32(kD3Chcr) & kChcrStr, 0u, "ch.3 completes once IDEC has produced 64 quadwords");
            t.Equals(f.r32(kD3Madr), 0x00200400u, "MADR advanced by one macroblock");
            t.Equals(f.r32(kD3Qwc), 0u, "QWC counted down to 0");
            t.Equals(f.r32(kDStat) & (1u << 3), 1u << 3, "D_STAT.CIS3 is set");
            t.IsTrue(macroblockMismatch(rdram + 0x00200000u, [](int, int) { return kMb0Grey; }).empty(),
                     "MB0 landed at MADR" + macroblockMismatch(rdram + 0x00200000u, [](int, int) { return kMb0Grey; }));
            t.Equals(f.r32(kIpuCtrl) & kBusy, kBusy, "the IPU stays busy with MB1 still queued");

            f.w32(kD3Qwc, kMacroblockQw);
            f.w32(kD3Chcr, kChcrStr);
            t.Equals(f.r32(kD3Chcr) & kChcrStr, 0u, "the second ch.3 transfer completes at once");
            t.IsTrue(macroblockMismatch(rdram + 0x00200400u, [](int, int) { return kMb1Red; }).empty(),
                     "MB1 landed at the advanced MADR" +
                         macroblockMismatch(rdram + 0x00200400u, [](int, int) { return kMb1Red; }));
            t.Equals(f.r32(kIpuCtrl) & kBusy, 0u, "the IPU is idle once its output is drained");
        });

        tc.Run("Unimplemented commands and bitstream errors set ECD", [](TestCase &t)
        {
            IpuFixture f;
            t.IsTrue(f.ok, "PS2Memory initialize should succeed");
            f.resetIpu();

            f.w32(kIpuCmd, 0x20000000u); // BDEC
            t.Equals(f.r32(kIpuCtrl) & (kBusy | kCtrlEcd), kCtrlEcd, "BDEC: ECD, not busy");
            f.w32(kIpuCmd, kCmdBclr);
            t.Equals(f.r32(kIpuCtrl) & kCtrlEcd, 0u, "the next command clears ECD");
            f.w32(kIpuCmd, 0x80000000u); // PACK
            t.Equals(f.r32(kIpuCtrl) & (kBusy | kCtrlEcd), kCtrlEcd, "PACK: ECD, not busy");
            f.w32(kIpuCmd, kCmdIdec | (1u << 27)); // OFM: RGB16 output
            t.Equals(f.r32(kIpuCtrl) & (kBusy | kCtrlEcd), kCtrlEcd, "IDEC RGB16: ECD, not busy");

            // '00' is not an I-picture macroblock_type.
            f.w32(kIpuCmd, kCmdBclr);
            const uint8_t zeros[16] = {};
            f.feed(zeros, sizeof(zeros));
            f.w32(kIpuCmd, kCmdIdec);
            const uint32_t ctrl = f.r32(kIpuCtrl);
            t.Equals(ctrl & (kBusy | kCtrlEcd), kCtrlEcd, "invalid macroblock_type: ECD, not busy");
            t.Equals((ctrl >> 4) & 0xFu, 0u, "no output for a macroblock that failed to parse");
        });
    });
}
