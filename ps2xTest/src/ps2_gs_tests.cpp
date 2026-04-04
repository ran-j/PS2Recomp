#include "MiniTest.h"
#include "runtime/ps2_memory.h"
#include "ps2_runtime.h"
#include "ps2_stubs.h"
#include "ps2_syscalls.h"
#include "runtime/ps2_gs_gpu.h"
#include "runtime/ps2_gs_psmct32.h"
#include "runtime/ps2_gs_psmt4.h"
#include "runtime/ps2_gs_psmt8.h"
#include "Stubs/Helpers/Support.h"
#include "Stubs/GS.h"

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

    static_assert(sizeof(GsImageMem) == 12, "GsImageMem size mismatch");

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

    void writeGsImageTest(uint8_t *rdram, uint32_t addr, const GsImageMem &image)
    {
        std::memcpy(rdram + addr, &image, sizeof(image));
    }

    void writeGsImageTest(std::vector<uint8_t> &rdram, uint32_t addr, const GsImageMem &image)
    {
        writeGsImageTest(rdram.data(), addr, image);
    }

    void writePSMT4Texel(std::vector<uint8_t> &vram, uint32_t tbp, uint32_t tbw, uint32_t x, uint32_t y, uint8_t index)
    {
        const uint32_t nibbleAddr = GSPSMT4::addrPSMT4(tbp, tbw, x, y);
        const uint32_t byteOff = nibbleAddr >> 1;
        uint8_t &packed = vram[byteOff];
        if ((nibbleAddr & 1u) != 0u)
        {
            packed = static_cast<uint8_t>((packed & 0x0Fu) | ((index & 0x0Fu) << 4));
        }
        else
        {
            packed = static_cast<uint8_t>((packed & 0xF0u) | (index & 0x0Fu));
        }
    }

    uint32_t referenceAddrPSMT4(uint32_t block, uint32_t width, uint32_t x, uint32_t y)
    {
        static constexpr uint8_t kBlockTable4[8][4] = {
            {0, 2, 8, 10},
            {1, 3, 9, 11},
            {4, 6, 12, 14},
            {5, 7, 13, 15},
            {16, 18, 24, 26},
            {17, 19, 25, 27},
            {20, 22, 28, 30},
            {21, 23, 29, 31},
        };

        static constexpr uint16_t kColumnTable4[16][32] = {
            {0, 8, 32, 40, 64, 72, 96, 104, 2, 10, 34, 42, 66, 74, 98, 106, 4, 12, 36, 44, 68, 76, 100, 108, 6, 14, 38, 46, 70, 78, 102, 110},
            {16, 24, 48, 56, 80, 88, 112, 120, 18, 26, 50, 58, 82, 90, 114, 122, 20, 28, 52, 60, 84, 92, 116, 124, 22, 30, 54, 62, 86, 94, 118, 126},
            {65, 73, 97, 105, 1, 9, 33, 41, 67, 75, 99, 107, 3, 11, 35, 43, 69, 77, 101, 109, 5, 13, 37, 45, 71, 79, 103, 111, 7, 15, 39, 47},
            {81, 89, 113, 121, 17, 25, 49, 57, 83, 91, 115, 123, 19, 27, 51, 59, 85, 93, 117, 125, 21, 29, 53, 61, 87, 95, 119, 127, 23, 31, 55, 63},
            {192, 200, 224, 232, 128, 136, 160, 168, 194, 202, 226, 234, 130, 138, 162, 170, 196, 204, 228, 236, 132, 140, 164, 172, 198, 206, 230, 238, 134, 142, 166, 174},
            {208, 216, 240, 248, 144, 152, 176, 184, 210, 218, 242, 250, 146, 154, 178, 186, 212, 220, 244, 252, 148, 156, 180, 188, 214, 222, 246, 254, 150, 158, 182, 190},
            {129, 137, 161, 169, 193, 201, 225, 233, 131, 139, 163, 171, 195, 203, 227, 235, 133, 141, 165, 173, 197, 205, 229, 237, 135, 143, 167, 175, 199, 207, 231, 239},
            {145, 153, 177, 185, 209, 217, 241, 249, 147, 155, 179, 187, 211, 219, 243, 251, 149, 157, 181, 189, 213, 221, 245, 253, 151, 159, 183, 191, 215, 223, 247, 255},
            {256, 264, 288, 296, 320, 328, 352, 360, 258, 266, 290, 298, 322, 330, 354, 362, 260, 268, 292, 300, 324, 332, 356, 364, 262, 270, 294, 302, 326, 334, 358, 366},
            {272, 280, 304, 312, 336, 344, 368, 376, 274, 282, 306, 314, 338, 346, 370, 378, 276, 284, 308, 316, 340, 348, 372, 380, 278, 286, 310, 318, 342, 350, 374, 382},
            {321, 329, 353, 361, 257, 265, 289, 297, 323, 331, 355, 363, 259, 267, 291, 299, 325, 333, 357, 365, 261, 269, 293, 301, 327, 335, 359, 367, 263, 271, 295, 303},
            {337, 345, 369, 377, 273, 281, 305, 313, 339, 347, 371, 379, 275, 283, 307, 315, 341, 349, 373, 381, 277, 285, 309, 317, 343, 351, 375, 383, 279, 287, 311, 319},
            {448, 456, 480, 488, 384, 392, 416, 424, 450, 458, 482, 490, 386, 394, 418, 426, 452, 460, 484, 492, 388, 396, 420, 428, 454, 462, 486, 494, 390, 398, 422, 430},
            {464, 472, 496, 504, 400, 408, 432, 440, 466, 474, 498, 506, 402, 410, 434, 442, 468, 476, 500, 508, 404, 412, 436, 444, 470, 478, 502, 510, 406, 414, 438, 446},
            {385, 393, 417, 425, 449, 457, 481, 489, 387, 395, 419, 427, 451, 459, 483, 491, 389, 397, 421, 429, 453, 461, 485, 493, 391, 399, 423, 431, 455, 463, 487, 495},
            {401, 409, 433, 441, 465, 473, 497, 505, 403, 411, 435, 443, 467, 475, 499, 507, 405, 413, 437, 445, 469, 477, 501, 509, 407, 415, 439, 447, 471, 479, 503, 511},
        };

        const uint32_t pagesPerRow = ((width >> 1u) != 0u) ? (width >> 1u) : 1u;
        const uint32_t page = (block >> 5u) + (y >> 7u) * pagesPerRow + (x >> 7u);
        const uint32_t blockId = (block & 0x1Fu) + kBlockTable4[(y >> 4u) & 7u][(x >> 5u) & 3u];
        const uint32_t pageOffset = (blockId >> 5u) << 14u;
        const uint32_t localBlock = blockId & 0x1Fu;
        return (page << 14u) + pageOffset + localBlock * 512u + kColumnTable4[y & 0x0Fu][x & 0x1Fu];
    }

    void writeReferencePSMT4Texel(std::vector<uint8_t> &vram, uint32_t tbp, uint32_t tbw, uint32_t x, uint32_t y, uint8_t index)
    {
        const uint32_t nibbleAddr = referenceAddrPSMT4(tbp, tbw, x, y);
        const uint32_t byteOff = nibbleAddr >> 1;
        uint8_t &packed = vram[byteOff];
        if ((nibbleAddr & 1u) != 0u)
        {
            packed = static_cast<uint8_t>((packed & 0x0Fu) | ((index & 0x0Fu) << 4));
        }
        else
        {
            packed = static_cast<uint8_t>((packed & 0xF0u) | (index & 0x0Fu));
        }
    }

    uint32_t referenceAddrPSMT8(uint32_t block, uint32_t width, uint32_t x, uint32_t y)
    {
        static constexpr uint8_t kBlockTable8[4][8] = {
            {0, 1, 4, 5, 16, 17, 20, 21},
            {2, 3, 6, 7, 18, 19, 22, 23},
            {8, 9, 12, 13, 24, 25, 28, 29},
            {10, 11, 14, 15, 26, 27, 30, 31},
        };

        static constexpr uint8_t kColumnTable8[16][16] = {
            {0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54},
            {8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62},
            {33, 37, 49, 53, 1, 5, 17, 21, 35, 39, 51, 55, 3, 7, 19, 23},
            {41, 45, 57, 61, 9, 13, 25, 29, 43, 47, 59, 63, 11, 15, 27, 31},
            {96, 100, 112, 116, 64, 68, 80, 84, 98, 102, 114, 118, 66, 70, 82, 86},
            {104, 108, 120, 124, 72, 76, 88, 92, 106, 110, 122, 126, 74, 78, 90, 94},
            {65, 69, 81, 85, 97, 101, 113, 117, 67, 71, 83, 87, 99, 103, 115, 119},
            {73, 77, 89, 93, 105, 109, 121, 125, 75, 79, 91, 95, 107, 111, 123, 127},
            {128, 132, 144, 148, 160, 164, 176, 180, 130, 134, 146, 150, 162, 166, 178, 182},
            {136, 140, 152, 156, 168, 172, 184, 188, 138, 142, 154, 158, 170, 174, 186, 190},
            {161, 165, 177, 181, 129, 133, 145, 149, 163, 167, 179, 183, 131, 135, 147, 151},
            {169, 173, 185, 189, 137, 141, 153, 157, 171, 175, 187, 191, 139, 143, 155, 159},
            {224, 228, 240, 244, 192, 196, 208, 212, 226, 230, 242, 246, 194, 198, 210, 214},
            {232, 236, 248, 252, 200, 204, 216, 220, 234, 238, 250, 254, 202, 206, 218, 222},
            {193, 197, 209, 213, 225, 229, 241, 245, 195, 199, 211, 215, 227, 231, 243, 247},
            {201, 205, 217, 221, 233, 237, 249, 253, 203, 207, 219, 223, 235, 239, 251, 255},
        };

        const uint32_t pagesPerRow = ((width >> 1u) != 0u) ? (width >> 1u) : 1u;
        const uint32_t page = (block >> 5u) + (y >> 6u) * pagesPerRow + (x >> 7u);
        const uint32_t blockId = (block & 0x1Fu) + kBlockTable8[(y >> 4u) & 3u][(x >> 4u) & 7u];
        const uint32_t pageOffset = (blockId >> 5u) << 13u;
        const uint32_t localBlock = blockId & 0x1Fu;
        return (page << 13u) + pageOffset + localBlock * 256u + kColumnTable8[y & 0x0Fu][x & 0x0Fu];
    }

    uint32_t referenceAddrPSMCT32(uint32_t block, uint32_t width, uint32_t x, uint32_t y)
    {
        static constexpr uint8_t kBlockTable32[4][8] = {
            {0, 1, 4, 5, 16, 17, 20, 21},
            {2, 3, 6, 7, 18, 19, 22, 23},
            {8, 9, 12, 13, 24, 25, 28, 29},
            {10, 11, 14, 15, 26, 27, 30, 31},
        };

        static constexpr uint8_t kColumnTable32[8][8] = {
            {0, 1, 4, 5, 8, 9, 12, 13},
            {2, 3, 6, 7, 10, 11, 14, 15},
            {16, 17, 20, 21, 24, 25, 28, 29},
            {18, 19, 22, 23, 26, 27, 30, 31},
            {32, 33, 36, 37, 40, 41, 44, 45},
            {34, 35, 38, 39, 42, 43, 46, 47},
            {48, 49, 52, 53, 56, 57, 60, 61},
            {50, 51, 54, 55, 58, 59, 62, 63},
        };

        const uint32_t pagesPerRow = (width != 0u) ? width : 1u;
        const uint32_t page = (block >> 5u) + (y >> 5u) * pagesPerRow + (x >> 6u);
        const uint32_t blockId = (block & 0x1Fu) + kBlockTable32[(y >> 3u) & 3u][(x >> 3u) & 7u];
        const uint32_t pageOffset = (blockId >> 5u) << 13u;
        const uint32_t localBlock = blockId & 0x1Fu;
        return (page << 13u) + pageOffset + localBlock * 256u +
               static_cast<uint32_t>(kColumnTable32[y & 0x7u][x & 0x7u]) * 4u;
    }

    void writeReferencePSMCT32Pixel(std::vector<uint8_t> &vram,
                                    uint32_t fbp,
                                    uint32_t fbw,
                                    uint32_t x,
                                    uint32_t y,
                                    uint32_t pixel)
    {
        const uint32_t off = referenceAddrPSMCT32(fbp, (fbw != 0u) ? fbw : 1u, x, y);
        std::memcpy(vram.data() + off, &pixel, sizeof(pixel));
    }

    uint32_t readReferencePSMCT32Pixel(const std::vector<uint8_t> &vram,
                                       uint32_t fbp,
                                       uint32_t fbw,
                                       uint32_t x,
                                       uint32_t y)
    {
        const uint32_t off = referenceAddrPSMCT32(fbp, (fbw != 0u) ? fbw : 1u, x, y);
        uint32_t pixel = 0u;
        std::memcpy(&pixel, vram.data() + off, sizeof(pixel));
        return pixel;
    }

    uint32_t frameBaseToBlock(uint32_t fbp)
    {
        return fbp << 5u;
    }

    void writeReferenceFramePSMCT32Pixel(std::vector<uint8_t> &vram,
                                         uint32_t fbp,
                                         uint32_t fbw,
                                         uint32_t x,
                                         uint32_t y,
                                         uint32_t pixel)
    {
        writeReferencePSMCT32Pixel(vram, frameBaseToBlock(fbp), fbw, x, y, pixel);
    }

    uint32_t readReferenceFramePSMCT32Pixel(const std::vector<uint8_t> &vram,
                                            uint32_t fbp,
                                            uint32_t fbw,
                                            uint32_t x,
                                            uint32_t y)
    {
        return readReferencePSMCT32Pixel(vram, frameBaseToBlock(fbp), fbw, x, y);
    }

    void expectGuestHeapReusable(TestCase &t, PS2Runtime &runtime, const char *message)
    {
        const uint32_t expectedBase = runtime.guestHeapBase();
        const uint32_t probe = runtime.guestMalloc(16u, 16u);
        t.Equals(probe, expectedBase, message);
        runtime.guestFree(probe);
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

        tc.Run("GsSetCrt updates SMODE2 for host presentation mode", [](TestCase &t)
        {
            PS2Runtime runtime;
            t.IsTrue(runtime.memory().initialize(), "runtime memory initialize should succeed");

            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            R5900Context ctx{};
            setRegU32(ctx, 4, 1u); // interlaced
            setRegU32(ctx, 5, 0u); // NTSC
            setRegU32(ctx, 6, 0u); // field mode

            runtime.memory().gs().pmode = 0u;
            runtime.memory().gs().smode2 = 0u;
            GsSetCrt(rdram.data(), &ctx, &runtime);

            t.Equals(runtime.memory().gs().smode2, 0x1ull,
                     "GsSetCrt should publish interlaced field mode through SMODE2");
            t.Equals(runtime.memory().gs().pmode & 0x3ull, 0x1ull,
                     "GsSetCrt should leave CRT1 enabled for presentation");
            t.Equals(getRegU32Test(ctx, 2), 0u,
                     "GsSetCrt should return success");
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

        tc.Run("sceGsSetDefDBuffDc seeds a clear packet and swap clears the draw buffer", [](TestCase &t)
        {
            PS2Runtime runtime;
            t.IsTrue(runtime.memory().initialize(), "runtime memory initialize should succeed");

            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            constexpr uint32_t kEnvAddr = 0x5000u;
            constexpr uint32_t kDBuffSize = 0x330u;
            constexpr uint32_t kClear0Offset = 0x160u;
            constexpr uint32_t kTestAAddrOffset = kClear0Offset + 0x08u;
            constexpr uint32_t kPrimAddrOffset = kClear0Offset + 0x18u;
            constexpr uint32_t kRgbaqOffset = kClear0Offset + 0x20u;
            constexpr uint32_t kRgbaqAddrOffset = kClear0Offset + 0x28u;
            constexpr uint32_t kXyz2AAddrOffset = kClear0Offset + 0x38u;
            constexpr uint32_t kXyz2BAddrOffset = kClear0Offset + 0x48u;
            constexpr uint32_t kTestBAddrOffset = kClear0Offset + 0x58u;
            constexpr uint32_t kClearColor = 0x80402010u;
            constexpr uint32_t kStackAddr = 0x900u;
            const uint32_t kZTest = 2u;
            const uint32_t kEnableClear = 1u;

            R5900Context ctx{};
            setRegU32(ctx, 4, kEnvAddr);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 640u);
            setRegU32(ctx, 7, 448u);
            setRegU32(ctx, 29, kStackAddr);
            std::memset(rdram.data() + kEnvAddr, 0xCD, kDBuffSize);
            std::memcpy(rdram.data() + kStackAddr + 16u, &kZTest, sizeof(kZTest));
            std::memcpy(rdram.data() + kStackAddr + 24u, &kEnableClear, sizeof(kEnableClear));
            std::memset(runtime.memory().getGSVRAM(), 0xAB, 16u);
            ps2_stubs::sceGsSetDefDBuffDc(rdram.data(), &ctx, &runtime);

            uint64_t testAAddr = 0u;
            uint64_t primAddr = 0u;
            uint64_t rgbaqAddr = 0u;
            uint64_t xyz2AAddr = 0u;
            uint64_t xyz2BAddr = 0u;
            uint64_t testBAddr = 0u;
            std::memcpy(&testAAddr, rdram.data() + kEnvAddr + kTestAAddrOffset, sizeof(testAAddr));
            std::memcpy(&primAddr, rdram.data() + kEnvAddr + kPrimAddrOffset, sizeof(primAddr));
            std::memcpy(&rgbaqAddr, rdram.data() + kEnvAddr + kRgbaqAddrOffset, sizeof(rgbaqAddr));
            std::memcpy(&xyz2AAddr, rdram.data() + kEnvAddr + kXyz2AAddrOffset, sizeof(xyz2AAddr));
            std::memcpy(&xyz2BAddr, rdram.data() + kEnvAddr + kXyz2BAddrOffset, sizeof(xyz2BAddr));
            std::memcpy(&testBAddr, rdram.data() + kEnvAddr + kTestBAddrOffset, sizeof(testBAddr));

            t.Equals(testAAddr, 0x47ull, "dbuff clear packet should program TEST_1 before clearing");
            t.Equals(primAddr, 0x00ull, "dbuff clear packet should program PRIM before clearing");
            t.Equals(rgbaqAddr, 0x01ull, "dbuff clear packet should expose RGBAQ for runtime color updates");
            t.Equals(xyz2AAddr, 0x05ull, "dbuff clear packet should seed the first clear vertex as XYZ2");
            t.Equals(xyz2BAddr, 0x05ull, "dbuff clear packet should seed the second clear vertex as XYZ2");
            t.Equals(testBAddr, 0x47ull, "dbuff clear packet should restore TEST_1 after clearing");

            uint64_t rgbaq = static_cast<uint64_t>(kClearColor);
            std::memcpy(rdram.data() + kEnvAddr + kRgbaqOffset, &rgbaq, sizeof(rgbaq));

            std::memset(&ctx, 0, sizeof(ctx));
            setRegU32(ctx, 4, kEnvAddr);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::sceGsSwapDBuffDc(rdram.data(), &ctx, &runtime);

            uint32_t clearedPixel = 0u;
            std::memcpy(&clearedPixel, runtime.memory().getGSVRAM(), sizeof(clearedPixel));
            t.Equals(clearedPixel, kClearColor,
                     "sceGsSwapDBuffDc should execute the seeded clear packet against the active draw buffer");

            constexpr uint32_t kMidX = 320u;
            constexpr uint32_t kMidY = 200u;
            const uint32_t kMidOffset = ((kMidY * 640u) + kMidX) * 4u;
            uint32_t clearedMidPixel = 0u;
            std::memcpy(&clearedMidPixel, runtime.memory().getGSVRAM() + kMidOffset, sizeof(clearedMidPixel));
            t.Equals(clearedMidPixel, kClearColor,
                     "sceGsSwapDBuffDc should clear the interior of the active draw buffer, not just the first pixel");
        });

        tc.Run("sceGsSetDefDBuffDc accepts trailing args from the recompiler register ABI", [](TestCase &t)
        {
            PS2Runtime runtime;
            t.IsTrue(runtime.memory().initialize(), "runtime memory initialize should succeed");

            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            constexpr uint32_t kEnvAddr = 0x5400u;
            constexpr uint32_t kDBuffSize = 0x330u;
            constexpr uint32_t kClear0Offset = 0x160u;
            constexpr uint32_t kRgbaqOffset = kClear0Offset + 0x20u;
            constexpr uint32_t kRgbaqAddrOffset = kClear0Offset + 0x28u;
            constexpr uint32_t kStackAddr = 0xA00u;
            constexpr uint32_t kClearColor = 0x40201008u;

            R5900Context ctx{};
            setRegU32(ctx, 4, kEnvAddr);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 640u);
            setRegU32(ctx, 7, 448u);
            setRegU32(ctx, 8, 2u);
            setRegU32(ctx, 9, 58u);
            setRegU32(ctx, 10, 1u);
            setRegU32(ctx, 29, kStackAddr);
            std::memset(rdram.data() + kEnvAddr, 0xCD, kDBuffSize);
            std::memset(runtime.memory().getGSVRAM(), 0xAB, 16u);

            ps2_stubs::sceGsSetDefDBuffDc(rdram.data(), &ctx, &runtime);

            uint64_t rgbaqAddr = 0u;
            std::memcpy(&rgbaqAddr, rdram.data() + kEnvAddr + kRgbaqAddrOffset, sizeof(rgbaqAddr));
            t.Equals(rgbaqAddr, 0x01ull,
                     "sceGsSetDefDBuffDc should seed the clear packet when trailing args arrive in t0-t2");

            const uint64_t rgbaq = static_cast<uint64_t>(kClearColor);
            std::memcpy(rdram.data() + kEnvAddr + kRgbaqOffset, &rgbaq, sizeof(rgbaq));

            std::memset(&ctx, 0, sizeof(ctx));
            setRegU32(ctx, 4, kEnvAddr);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::sceGsSwapDBuffDc(rdram.data(), &ctx, &runtime);

            uint32_t clearedPixel = 0u;
            std::memcpy(&clearedPixel, runtime.memory().getGSVRAM(), sizeof(clearedPixel));
            t.Equals(clearedPixel, kClearColor,
                     "sceGsSwapDBuffDc should honor a clear packet seeded from register-based trailing args");
        });

        tc.Run("clearFramebufferContext clears the requested context even if another context is active", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            constexpr uint32_t kCtx0Color = 0x11223344u;
            constexpr uint32_t kCtx1Sentinel = 0xAABBCCDDu;

            gs.writeRegister(GS_REG_FRAME_1, (1ull << 16)); // FBP=0, FBW=1, PSMCT32
            gs.writeRegister(GS_REG_SCISSOR_1, (0ull << 0) | (0ull << 16) | (1ull << 32) | (1ull << 48));
            gs.writeRegister(GS_REG_FRAME_2, 150ull | (1ull << 16));
            gs.writeRegister(GS_REG_SCISSOR_2, (0ull << 0) | (0ull << 16) | (1ull << 32) | (1ull << 48));
            gs.writeRegister(GS_REG_PRIM, static_cast<uint64_t>(GS_PRIM_POINT) | (1ull << 9));

            writeReferenceFramePSMCT32Pixel(vram, 150u, 1u, 0u, 1u, kCtx1Sentinel);

            t.IsTrue(gs.clearFramebufferContext(0u, kCtx0Color),
                     "context-targeted clear should succeed for a configured CT32 framebuffer");

            const uint32_t ctx0Pixel = readReferenceFramePSMCT32Pixel(vram, 0u, 1u, 0u, 1u);
            t.Equals(ctx0Pixel, kCtx0Color,
                     "context-targeted clear should write the requested context even when PRIM.ctxt points elsewhere");

            const uint32_t ctx1Pixel = readReferenceFramePSMCT32Pixel(vram, 150u, 1u, 0u, 1u);
            t.Equals(ctx1Pixel, kCtx1Sentinel,
                     "context-targeted clear should leave the other context framebuffer untouched");
        });

        tc.Run("PABE bypasses alpha blend for low-alpha source pixels", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            gs.writeRegister(GS_REG_FRAME_1, (1ull << 16)); // FBW=1, PSMCT32, FBP=0
            gs.writeRegister(GS_REG_SCISSOR_1, 0ull);
            gs.writeRegister(GS_REG_ALPHA_1, 0x6000000064ull);
            gs.writeRegister(GS_REG_TEST_1, 0x30000ull);
            gs.writeRegister(GS_REG_PRIM, static_cast<uint64_t>(GS_PRIM_POINT) | (1ull << 6));

            const uint32_t dstWhite = 0xFFFFFFFFu;
            std::memcpy(vram.data(), &dstWhite, sizeof(dstWhite));

            gs.writeRegister(GS_REG_PABE, 0ull);
            gs.writeRegister(GS_REG_RGBAQ, 0x01000000ull);
            gs.writeRegister(GS_REG_XYZ2, 0ull);

            uint32_t blendedPixel = 0u;
            std::memcpy(&blendedPixel, vram.data(), sizeof(blendedPixel));
            t.Equals(blendedPixel, 0x013F3F3Fu,
                     "without PABE, low-alpha fullscreen copies should still apply ALPHA blending");

            std::memcpy(vram.data(), &dstWhite, sizeof(dstWhite));

            gs.writeRegister(GS_REG_PRIM, static_cast<uint64_t>(GS_PRIM_POINT) | (1ull << 6));
            gs.writeRegister(GS_REG_PABE, 1ull);
            gs.writeRegister(GS_REG_RGBAQ, 0x01000000ull);
            gs.writeRegister(GS_REG_XYZ2, 0ull);

            uint32_t pabeBypassedPixel = 0u;
            std::memcpy(&pabeBypassedPixel, vram.data(), sizeof(pabeBypassedPixel));
            t.Equals(pabeBypassedPixel, 0x01000000u,
                     "with PABE enabled, low-alpha source pixels should bypass ALPHA blending and overwrite the destination");

            std::memcpy(vram.data(), &dstWhite, sizeof(dstWhite));

            gs.writeRegister(GS_REG_PRIM, static_cast<uint64_t>(GS_PRIM_POINT) | (1ull << 6));
            gs.writeRegister(GS_REG_PABE, 1ull);
            gs.writeRegister(GS_REG_RGBAQ, 0x80000000ull);
            gs.writeRegister(GS_REG_XYZ2, 0ull);

            uint32_t highAlphaPixel = 0u;
            std::memcpy(&highAlphaPixel, vram.data(), sizeof(highAlphaPixel));
            t.Equals(highAlphaPixel, 0x803F3F3Fu,
                     "with PABE enabled, high-alpha source pixels should still use the configured ALPHA blend");
        });

        tc.Run("FBA forces the framebuffer alpha high bit on CT32 writes", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            gs.writeRegister(GS_REG_FRAME_1, (1ull << 16));
            gs.writeRegister(GS_REG_SCISSOR_1, 0ull);
            gs.writeRegister(GS_REG_TEST_1, 0ull);

            gs.writeRegister(GS_REG_PRIM, static_cast<uint64_t>(GS_PRIM_POINT));
            gs.writeRegister(GS_REG_FBA_1, 0ull);
            gs.writeRegister(GS_REG_RGBAQ, 0x01112233ull);
            gs.writeRegister(GS_REG_XYZ2, 0ull);

            uint32_t pixelWithoutFba = 0u;
            std::memcpy(&pixelWithoutFba, vram.data(), sizeof(pixelWithoutFba));
            t.Equals(pixelWithoutFba, 0x01112233u,
                     "without FBA, CT32 writes should preserve the source alpha byte");

            std::memset(vram.data(), 0, sizeof(uint32_t));

            gs.writeRegister(GS_REG_PRIM, static_cast<uint64_t>(GS_PRIM_POINT));
            gs.writeRegister(GS_REG_FBA_1, 1ull);
            gs.writeRegister(GS_REG_RGBAQ, 0x01112233ull);
            gs.writeRegister(GS_REG_XYZ2, 0ull);

            uint32_t pixelWithFba = 0u;
            std::memcpy(&pixelWithFba, vram.data(), sizeof(pixelWithFba));
            t.Equals(pixelWithFba, 0x81112233u,
                     "with FBA enabled, CT32 writes should force the framebuffer alpha high bit");
        });

        tc.Run("CT32 raster writes alias cleanly into later CT32 texture sampling", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            constexpr uint64_t kFrame1 =
                (0ull << 0) |
                (1ull << 16) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 24);
            constexpr uint64_t kScissor1 =
                (0ull << 0) |
                (1ull << 16) |
                (0ull << 32) |
                (1ull << 48);
            constexpr uint64_t kFrame2 =
                (150ull << 0) |
                (1ull << 16) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 24);
            constexpr uint64_t kScissor2 = 0ull;
            constexpr uint64_t kTex0_2 =
                (0ull << 0) |
                (1ull << 14) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 20) |
                (0ull << 26) |
                (1ull << 30) |
                (1ull << 34) |
                (1ull << 35);
            constexpr uint64_t kPointPrim = static_cast<uint64_t>(GS_PRIM_POINT);
            constexpr uint64_t kCopyPrim =
                static_cast<uint64_t>(GS_PRIM_SPRITE) |
                (1ull << 4) |
                (1ull << 8) |
                (1ull << 9);
            constexpr uint64_t kSourceColor = 0x80665544ull;
            constexpr uint64_t kPointXyz =
                (0ull << 0) |
                (16ull << 16);
            constexpr uint64_t kUvRow1 =
                (0ull << 0) |
                (16ull << 16);

            gs.writeRegister(GS_REG_FRAME_1, kFrame1);
            gs.writeRegister(GS_REG_SCISSOR_1, kScissor1);
            gs.writeRegister(GS_REG_XYOFFSET_1, 0ull);
            gs.writeRegister(GS_REG_TEST_1, 0ull);
            gs.writeRegister(GS_REG_PRIM, kPointPrim);
            gs.writeRegister(GS_REG_RGBAQ, kSourceColor);
            gs.writeRegister(GS_REG_XYZ2, kPointXyz);

            gs.writeRegister(GS_REG_FRAME_2, kFrame2);
            gs.writeRegister(GS_REG_SCISSOR_2, kScissor2);
            gs.writeRegister(GS_REG_XYOFFSET_2, 0ull);
            gs.writeRegister(GS_REG_TEST_2, 0ull);
            gs.writeRegister(GS_REG_TEX0_2, kTex0_2);
            gs.writeRegister(GS_REG_PRIM, kCopyPrim);
            gs.writeRegister(GS_REG_RGBAQ, 0x80808080ull);
            gs.writeRegister(GS_REG_UV, kUvRow1);
            gs.writeRegister(GS_REG_XYZ2, 0ull);
            gs.writeRegister(GS_REG_UV, kUvRow1);
            gs.writeRegister(GS_REG_XYZ2, 0ull);

            const uint32_t dstPixel = readReferenceFramePSMCT32Pixel(vram, 150u, 1u, 0u, 0u);
            t.Equals(dstPixel, static_cast<uint32_t>(kSourceColor),
                     "CT32 primitives should land in the same local-memory layout that later CT32 texture sampling expects");
        });

        tc.Run("FST sprite 1:1 CT32 copies preserve source texels at the right and bottom edges", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            constexpr uint32_t kTexTbp = 64u;
            constexpr uint64_t kFrame =
                (150ull << 0) |
                (1ull << 16) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 24);
            constexpr uint64_t kScissor =
                (0ull << 0) |
                (3ull << 16) |
                (0ull << 32) |
                (3ull << 48);
            constexpr uint64_t kTex0 =
                (static_cast<uint64_t>(kTexTbp) << 0) |
                (1ull << 14) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 20) |
                (2ull << 26) |
                (2ull << 30) |
                (1ull << 34);
            constexpr uint64_t kPrim =
                static_cast<uint64_t>(GS_PRIM_SPRITE) |
                (1ull << 4) |
                (1ull << 8);
            constexpr uint64_t kXyz0 = 0ull;
            constexpr uint64_t kXyz1 =
                (static_cast<uint64_t>(4u << 4) << 0) |
                (static_cast<uint64_t>(4u << 4) << 16);
            constexpr uint64_t kUv0 = 0ull;
            constexpr uint64_t kUv1 =
                ((4ull * 16ull) << 0) |
                ((4ull * 16ull) << 16);
            constexpr uint32_t kSourcePixels[4] = {
                0x800000FFu,
                0x8000FF00u,
                0x80FF0000u,
                0x80FFFFFFu,
            };

            for (uint32_t y = 0u; y < 4u; ++y)
            {
                for (uint32_t x = 0u; x < 4u; ++x)
                {
                    writeReferencePSMCT32Pixel(vram, kTexTbp, 1u, x, y, kSourcePixels[x]);
                }
            }

            gs.writeRegister(GS_REG_FRAME_1, kFrame);
            gs.writeRegister(GS_REG_SCISSOR_1, kScissor);
            gs.writeRegister(GS_REG_XYOFFSET_1, 0ull);
            gs.writeRegister(GS_REG_TEST_1, 0ull);
            gs.writeRegister(GS_REG_ALPHA_1, 0ull);
            gs.writeRegister(GS_REG_TEX0_1, kTex0);
            gs.writeRegister(GS_REG_TEX1_1, 0ull);
            gs.writeRegister(GS_REG_PRIM, kPrim);
            gs.writeRegister(GS_REG_RGBAQ, 0x80808080ull);
            gs.writeRegister(GS_REG_UV, kUv0);
            gs.writeRegister(GS_REG_XYZ2, kXyz0);
            gs.writeRegister(GS_REG_UV, kUv1);
            gs.writeRegister(GS_REG_XYZ2, kXyz1);

            for (uint32_t y = 0u; y < 4u; ++y)
            {
                for (uint32_t x = 0u; x < 4u; ++x)
                {
                    const uint32_t pixel = readReferenceFramePSMCT32Pixel(vram, 150u, 1u, x, y);
                    t.Equals(pixel, kSourcePixels[x],
                             "1:1 FST sprite copies should preserve each source texel without off-by-one edge skew");
                }
            }
        });

        tc.Run("fullscreen display copy tracks the preferred presentation source frame", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            constexpr uint64_t kFrame2 =
                150ull |
                (10ull << 16) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 24);
            constexpr uint64_t kScissor2 =
                (0ull << 0) |
                (639ull << 16) |
                (0ull << 32) |
                (479ull << 48);
            constexpr uint64_t kXYOffset2 =
                (static_cast<uint64_t>(1728u << 4) << 0) |
                (static_cast<uint64_t>(1808u << 4) << 32);
            constexpr uint64_t kAlpha2 = 0x6000000064ull;
            constexpr uint64_t kTest2 = 0x30000ull;
            constexpr uint64_t kTex0_2 =
                (0ull << 0) |
                (10ull << 14) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 20) |
                (10ull << 26) |
                (9ull << 30) |
                (1ull << 34);
            constexpr uint64_t kPrimCopy =
                static_cast<uint64_t>(GS_PRIM_SPRITE) |
                (1ull << 4) |
                (1ull << 6) |
                (1ull << 8) |
                (1ull << 9);
            constexpr uint64_t kXyz0 =
                (static_cast<uint64_t>(1728u << 4) << 0) |
                (static_cast<uint64_t>(1808u << 4) << 16) |
                (256ull << 32);
            constexpr uint64_t kXyz1 =
                (static_cast<uint64_t>(2368u << 4) << 0) |
                (static_cast<uint64_t>(2288u << 4) << 16) |
                (256ull << 32);
            constexpr uint64_t kUv0 =
                (8ull << 0) |
                (8ull << 16);
            constexpr uint64_t kUv1 =
                ((8ull + (640ull * 16ull)) << 0) |
                ((8ull + (480ull * 16ull)) << 16);

            gs.writeRegister(GS_REG_FRAME_2, kFrame2);
            gs.writeRegister(GS_REG_SCISSOR_2, kScissor2);
            gs.writeRegister(GS_REG_XYOFFSET_2, kXYOffset2);
            gs.writeRegister(GS_REG_ALPHA_2, kAlpha2);
            gs.writeRegister(GS_REG_TEST_2, kTest2);
            gs.writeRegister(GS_REG_TEX0_2, kTex0_2);
            gs.writeRegister(GS_REG_PRIM, kPrimCopy);
            gs.writeRegister(GS_REG_RGBAQ, 0x80808080ull);
            gs.writeRegister(GS_REG_UV, kUv0);
            gs.writeRegister(GS_REG_XYZ2, kXyz0);
            gs.writeRegister(GS_REG_UV, kUv1);
            gs.writeRegister(GS_REG_XYZ2, kXyz1);

            GSFrameReg preferredSource{};
            uint32_t preferredDestFbp = 0u;
            t.IsTrue(gs.getPreferredDisplaySource(preferredSource, preferredDestFbp),
                     "fullscreen display-copy sprites should record their source frame for host presentation");
            t.Equals(preferredDestFbp, 150u,
                     "preferred presentation tracking should target the copied display page");
            t.Equals(preferredSource.fbp, 0u,
                     "preferred presentation tracking should expose the copy source frame base");
            t.Equals(preferredSource.fbw, 10u,
                     "preferred presentation tracking should expose the copy source width");
            t.Equals(static_cast<uint32_t>(preferredSource.psm), static_cast<uint32_t>(GS_PSM_CT32),
                     "preferred presentation tracking should expose the copy source format");

            gs.writeRegister(GS_REG_PRIM, static_cast<uint64_t>(GS_PRIM_POINT) | (1ull << 9));
            gs.writeRegister(GS_REG_RGBAQ, 0xFFFFFFFFull);
            gs.writeRegister(GS_REG_XYZ2, kXyz0);

            t.IsFalse(gs.getPreferredDisplaySource(preferredSource, preferredDestFbp),
                      "non-copy primitives that touch the display target should invalidate the preferred presentation source");
        });

        tc.Run("latched host presentation frame stays stable until the next latch", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GSRegisters regs{};
            regs.pmode = 1ull;
            regs.dispfb1 =
                150ull |
                (10ull << 9) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 15);
            regs.display1 =
                (639ull << 32) |
                (447ull << 44);

            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), &regs);

            constexpr uint32_t kDisplayPixel = 0x00332211u;
            constexpr uint32_t kSourcePixel = 0x00665544u;
            constexpr uint32_t kUpdatedSourcePixel = 0x00998877u;
            writeReferenceFramePSMCT32Pixel(vram, 150u, 10u, 0u, 0u, kDisplayPixel);
            std::memcpy(vram.data() + 0u, &kSourcePixel, sizeof(kSourcePixel));

            constexpr uint64_t kFrame2 =
                150ull |
                (10ull << 16) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 24);
            constexpr uint64_t kScissor2 =
                (0ull << 0) |
                (639ull << 16) |
                (0ull << 32) |
                (479ull << 48);
            constexpr uint64_t kXYOffset2 =
                (static_cast<uint64_t>(1728u << 4) << 0) |
                (static_cast<uint64_t>(1808u << 4) << 32);
            constexpr uint64_t kAlpha2 = 0x6000000064ull;
            constexpr uint64_t kTest2 = 0x30000ull;
            constexpr uint64_t kTex0_2 =
                (0ull << 0) |
                (10ull << 14) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 20) |
                (10ull << 26) |
                (9ull << 30) |
                (1ull << 34);
            constexpr uint64_t kPrimCopy =
                static_cast<uint64_t>(GS_PRIM_SPRITE) |
                (1ull << 4) |
                (1ull << 6) |
                (1ull << 8) |
                (1ull << 9);
            constexpr uint64_t kXyz0 =
                (static_cast<uint64_t>(1728u << 4) << 0) |
                (static_cast<uint64_t>(1808u << 4) << 16) |
                (256ull << 32);
            constexpr uint64_t kXyz1 =
                (static_cast<uint64_t>(2368u << 4) << 0) |
                (static_cast<uint64_t>(2288u << 4) << 16) |
                (256ull << 32);
            constexpr uint64_t kUv0 =
                (8ull << 0) |
                (8ull << 16);
            constexpr uint64_t kUv1 =
                ((8ull + (640ull * 16ull)) << 0) |
                ((8ull + (480ull * 16ull)) << 16);

            gs.writeRegister(GS_REG_FRAME_2, kFrame2);
            gs.writeRegister(GS_REG_SCISSOR_2, kScissor2);
            gs.writeRegister(GS_REG_XYOFFSET_2, kXYOffset2);
            gs.writeRegister(GS_REG_ALPHA_2, kAlpha2);
            gs.writeRegister(GS_REG_TEST_2, kTest2);
            gs.writeRegister(GS_REG_TEX0_2, kTex0_2);
            gs.writeRegister(GS_REG_PRIM, kPrimCopy);
            gs.writeRegister(GS_REG_RGBAQ, 0x80808080ull);
            gs.writeRegister(GS_REG_UV, kUv0);
            gs.writeRegister(GS_REG_XYZ2, kXyz0);
            gs.writeRegister(GS_REG_UV, kUv1);
            gs.writeRegister(GS_REG_XYZ2, kXyz1);

            gs.latchHostPresentationFrame();

            std::vector<uint8_t> latchedFrame;
            uint32_t latchedWidth = 0u;
            uint32_t latchedHeight = 0u;
            uint32_t displayFbp = 0u;
            uint32_t sourceFbp = 0u;
            bool usedPreferred = false;
            t.IsTrue(gs.copyLatchedHostPresentationFrame(latchedFrame,
                                                         latchedWidth,
                                                         latchedHeight,
                                                         &displayFbp,
                                                         &sourceFbp,
                                                         &usedPreferred),
                     "host presentation latch should produce a readable frame");
            t.Equals(displayFbp, 150u,
                     "latched host presentation should remember the selected display page");
            t.Equals(sourceFbp, 0u,
                     "latched host presentation should switch to the fullscreen copy source");
            t.IsTrue(usedPreferred,
                     "latched host presentation should record when it used the preferred copy source");
            t.Equals(latchedWidth, 640u,
                     "latched host presentation should preserve display width");
            t.Equals(latchedHeight, 448u,
                     "latched host presentation should preserve display height");
            t.Equals(static_cast<uint32_t>(latchedFrame[0]), 0x44u,
                     "latched host presentation should expose the source frame RGB data");
            t.Equals(static_cast<uint32_t>(latchedFrame[1]), 0x55u,
                     "latched host presentation should preserve the source frame green channel");
            t.Equals(static_cast<uint32_t>(latchedFrame[2]), 0x66u,
                     "latched host presentation should preserve the source frame blue channel");
            t.Equals(static_cast<uint32_t>(latchedFrame[3]), 0xFFu,
                     "latched host presentation should normalize framebuffer alpha for host upload");

            std::memcpy(vram.data() + 0u, &kUpdatedSourcePixel, sizeof(kUpdatedSourcePixel));

            std::vector<uint8_t> staleFrame;
            uint32_t staleWidth = 0u;
            uint32_t staleHeight = 0u;
            t.IsTrue(gs.copyLatchedHostPresentationFrame(staleFrame, staleWidth, staleHeight),
                     "latched host presentation should remain readable without relatching");
            t.Equals(static_cast<uint32_t>(staleFrame[0]), 0x44u,
                     "latched host presentation should stay stable until the next latch");

            gs.latchHostPresentationFrame();

            std::vector<uint8_t> refreshedFrame;
            uint32_t refreshedWidth = 0u;
            uint32_t refreshedHeight = 0u;
            t.IsTrue(gs.copyLatchedHostPresentationFrame(refreshedFrame, refreshedWidth, refreshedHeight),
                     "latched host presentation should refresh after a new latch");
            t.Equals(static_cast<uint32_t>(refreshedFrame[0]), 0x77u,
                     "relatching should pick up the updated source frame contents");
        });

        tc.Run("latched host presentation frame is returned tightly packed for narrower display widths", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GSRegisters regs{};
            regs.pmode = 1ull;
            regs.dispfb1 =
                150ull |
                (1ull << 9) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 15);
            regs.display1 =
                (63ull << 32) |
                (63ull << 44);

            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), &regs);

            constexpr uint32_t kTopLeft = 0xFF332211u;
            constexpr uint32_t kTopRight = 0xFF665544u;
            constexpr uint32_t kBottomLeft = 0xFF998877u;
            constexpr uint32_t kBottomRight = 0xFFCCBBAAu;
            writeReferenceFramePSMCT32Pixel(vram, 150u, 1u, 0u, 0u, kTopLeft);
            writeReferenceFramePSMCT32Pixel(vram, 150u, 1u, 1u, 0u, kTopRight);
            writeReferenceFramePSMCT32Pixel(vram, 150u, 1u, 0u, 1u, kBottomLeft);
            writeReferenceFramePSMCT32Pixel(vram, 150u, 1u, 1u, 1u, kBottomRight);

            gs.latchHostPresentationFrame();

            std::vector<uint8_t> latchedFrame;
            uint32_t latchedWidth = 0u;
            uint32_t latchedHeight = 0u;
            t.IsTrue(gs.copyLatchedHostPresentationFrame(latchedFrame, latchedWidth, latchedHeight),
                     "latched host presentation should be readable for narrow display widths");
            t.Equals(latchedWidth, 64u,
                     "latched host presentation should preserve the decoded display width");
            t.Equals(latchedHeight, 64u,
                     "latched host presentation should preserve the decoded display height");
            t.Equals(static_cast<uint32_t>(latchedFrame.size()), 64u * 64u * 4u,
                     "latched host presentation should return a tightly packed RGBA buffer");

            uint32_t pixel = 0u;
            std::memcpy(&pixel, latchedFrame.data() + 0u, sizeof(pixel));
            t.Equals(pixel, kTopLeft,
                     "latched host presentation should keep the first row intact");
            std::memcpy(&pixel, latchedFrame.data() + 4u, sizeof(pixel));
            t.Equals(pixel, kTopRight,
                     "latched host presentation should pack the first row contiguously");
            std::memcpy(&pixel, latchedFrame.data() + (64u * 4u), sizeof(pixel));
            t.Equals(pixel, kBottomLeft,
                     "latched host presentation should start the second row immediately after the first");
            std::memcpy(&pixel, latchedFrame.data() + (64u * 4u) + 4u, sizeof(pixel));
            t.Equals(pixel, kBottomRight,
                     "latched host presentation should preserve subsequent rows without the internal 640-pixel stride");
        });

        tc.Run("latched host presentation reads preferred CT32 source with GS swizzle", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GSRegisters regs{};
            regs.pmode = 1ull;
            regs.dispfb1 =
                150ull |
                (10ull << 9) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 15);
            regs.display1 =
                (639ull << 32) |
                (447ull << 44);

            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), &regs);

            constexpr uint32_t kSourceTbp0 = 64u;
            constexpr uint32_t kSourcePixelRow1 = 0x00665544u;
            constexpr uint32_t kDisplayPixelRow1 = 0x00CCBBAAu;
            const uint32_t swizzledSourceOff = GSPSMCT32::addrPSMCT32(kSourceTbp0, 10u, 0u, 1u);
            std::memcpy(vram.data() + swizzledSourceOff, &kSourcePixelRow1, sizeof(kSourcePixelRow1));
            writeReferenceFramePSMCT32Pixel(vram, 150u, 10u, 0u, 1u, kDisplayPixelRow1);

            constexpr uint64_t kFrame2 =
                150ull |
                (10ull << 16) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 24);
            constexpr uint64_t kScissor2 =
                (0ull << 0) |
                (639ull << 16) |
                (0ull << 32) |
                (479ull << 48);
            constexpr uint64_t kXYOffset2 =
                (static_cast<uint64_t>(1728u << 4) << 0) |
                (static_cast<uint64_t>(1808u << 4) << 32);
            constexpr uint64_t kAlpha2 = 0x6000000064ull;
            constexpr uint64_t kTest2 = 0x30000ull;
            constexpr uint64_t kTex0_2 =
                (static_cast<uint64_t>(kSourceTbp0) << 0) |
                (10ull << 14) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 20) |
                (10ull << 26) |
                (9ull << 30) |
                (1ull << 34);
            constexpr uint64_t kPrimCopy =
                static_cast<uint64_t>(GS_PRIM_SPRITE) |
                (1ull << 4) |
                (1ull << 6) |
                (1ull << 8) |
                (1ull << 9);
            constexpr uint64_t kXyz0 =
                (static_cast<uint64_t>(1728u << 4) << 0) |
                (static_cast<uint64_t>(1808u << 4) << 16) |
                (256ull << 32);
            constexpr uint64_t kXyz1 =
                (static_cast<uint64_t>(2368u << 4) << 0) |
                (static_cast<uint64_t>(2288u << 4) << 16) |
                (256ull << 32);
            constexpr uint64_t kUv0 =
                (8ull << 0) |
                (8ull << 16);
            constexpr uint64_t kUv1 =
                ((8ull + (640ull * 16ull)) << 0) |
                ((8ull + (480ull * 16ull)) << 16);

            gs.writeRegister(GS_REG_FRAME_2, kFrame2);
            gs.writeRegister(GS_REG_SCISSOR_2, kScissor2);
            gs.writeRegister(GS_REG_XYOFFSET_2, kXYOffset2);
            gs.writeRegister(GS_REG_ALPHA_2, kAlpha2);
            gs.writeRegister(GS_REG_TEST_2, kTest2);
            gs.writeRegister(GS_REG_TEX0_2, kTex0_2);
            gs.writeRegister(GS_REG_PRIM, kPrimCopy);
            gs.writeRegister(GS_REG_RGBAQ, 0x80808080ull);
            gs.writeRegister(GS_REG_UV, kUv0);
            gs.writeRegister(GS_REG_XYZ2, kXyz0);
            gs.writeRegister(GS_REG_UV, kUv1);
            gs.writeRegister(GS_REG_XYZ2, kXyz1);

            gs.latchHostPresentationFrame();

            std::vector<uint8_t> latchedFrame;
            uint32_t latchedWidth = 0u;
            uint32_t latchedHeight = 0u;
            uint32_t displayFbp = 0u;
            uint32_t sourceFbp = 0u;
            bool usedPreferred = false;
            t.IsTrue(gs.copyLatchedHostPresentationFrame(latchedFrame,
                                                         latchedWidth,
                                                         latchedHeight,
                                                         &displayFbp,
                                                         &sourceFbp,
                                                         &usedPreferred),
                     "preferred-source presentation should produce a host frame");
            t.IsTrue(usedPreferred,
                     "preferred-source presentation should use the fullscreen copy source");
            t.Equals(displayFbp, 150u,
                     "preferred-source presentation should still target the display page");
            t.Equals(sourceFbp, kSourceTbp0,
                     "preferred-source presentation should report the CT32 source frame");

            const size_t row1Off = 640u * 4u;
            t.Equals(static_cast<uint32_t>(latchedFrame[row1Off + 0u]), 0x44u,
                     "preferred-source presentation should read row 1 red from the swizzled CT32 source");
            t.Equals(static_cast<uint32_t>(latchedFrame[row1Off + 1u]), 0x55u,
                     "preferred-source presentation should read row 1 green from the swizzled CT32 source");
            t.Equals(static_cast<uint32_t>(latchedFrame[row1Off + 2u]), 0x66u,
                     "preferred-source presentation should read row 1 blue from the swizzled CT32 source");
            t.Equals(static_cast<uint32_t>(latchedFrame[row1Off + 3u]), 0xFFu,
                     "preferred-source presentation should normalize row 1 alpha for the host frame");
        });

        tc.Run("latched host presentation reads direct CT32 display frames with GS swizzle", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GSRegisters regs{};
            regs.pmode = 0x0001ull;
            regs.dispfb1 =
                150ull |
                (10ull << 9) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 15);
            regs.display1 =
                (639ull << 32) |
                (447ull << 44);

            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), &regs);

            constexpr uint32_t kRow1Pixel =
                0x44u |
                (0x55u << 8) |
                (0x66u << 16) |
                (0x77u << 24);
            constexpr uint32_t kLinearGarbageRow1 =
                0xAAu |
                (0xBBu << 8) |
                (0xCCu << 16) |
                (0xDDu << 24);
            constexpr size_t kHostRow1Off = 640u * 4u;

            writeReferenceFramePSMCT32Pixel(vram, 150u, 10u, 0u, 1u, kRow1Pixel);
            std::memcpy(vram.data() + (150u * 8192u) + kHostRow1Off, &kLinearGarbageRow1, sizeof(kLinearGarbageRow1));

            gs.latchHostPresentationFrame();

            std::vector<uint8_t> latchedFrame;
            uint32_t latchedWidth = 0u;
            uint32_t latchedHeight = 0u;
            uint32_t displayFbp = 0u;
            bool usedPreferred = false;
            t.IsTrue(gs.copyLatchedHostPresentationFrame(latchedFrame,
                                                         latchedWidth,
                                                         latchedHeight,
                                                         &displayFbp,
                                                         nullptr,
                                                         &usedPreferred),
                     "direct CT32 display presentation should produce a host frame");
            t.Equals(displayFbp, 150u,
                     "direct CT32 presentation should report the active display page");
            t.IsFalse(usedPreferred,
                      "direct CT32 presentation should not claim it used a preferred copy source");
            t.Equals(static_cast<uint32_t>(latchedFrame[kHostRow1Off + 0u]), 0x44u,
                     "direct CT32 presentation should read row 1 red from the GS-swizzled display page");
            t.Equals(static_cast<uint32_t>(latchedFrame[kHostRow1Off + 1u]), 0x55u,
                     "direct CT32 presentation should read row 1 green from the GS-swizzled display page");
            t.Equals(static_cast<uint32_t>(latchedFrame[kHostRow1Off + 2u]), 0x66u,
                     "direct CT32 presentation should read row 1 blue from the GS-swizzled display page");
            t.Equals(static_cast<uint32_t>(latchedFrame[kHostRow1Off + 3u]), 0xFFu,
                     "direct CT32 presentation should normalize row 1 alpha for the host frame");
        });

        tc.Run("latched host presentation merges both enabled PMODE circuits", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GSRegisters regs{};
            regs.pmode = 0x8007ull;
            regs.dispfb1 =
                150ull |
                (10ull << 9) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 15);
            regs.display1 =
                (639ull << 32) |
                (447ull << 44);
            regs.dispfb2 =
                0ull |
                (10ull << 9) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 15);
            regs.display2 = regs.display1;

            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), &regs);

            constexpr uint32_t kCircuit1Pixel =
                200u |
                (0u << 8) |
                (0u << 16) |
                (64u << 24);
            constexpr uint32_t kCircuit2Pixel =
                0u |
                (0u << 8) |
                (200u << 16) |
                (255u << 24);

            writeReferenceFramePSMCT32Pixel(vram, 150u, 10u, 0u, 0u, kCircuit1Pixel);
            std::memcpy(vram.data(), &kCircuit2Pixel, sizeof(kCircuit2Pixel));

            gs.latchHostPresentationFrame();

            std::vector<uint8_t> latchedFrame;
            uint32_t latchedWidth = 0u;
            uint32_t latchedHeight = 0u;
            uint32_t displayFbp = 0u;
            bool usedPreferred = false;
            t.IsTrue(gs.copyLatchedHostPresentationFrame(latchedFrame,
                                                         latchedWidth,
                                                         latchedHeight,
                                                         &displayFbp,
                                                         nullptr,
                                                         &usedPreferred),
                     "dual-circuit PMODE presentation should produce a host frame");
            t.Equals(displayFbp, 150u,
                     "dual-circuit presentation should still report the primary display page");
            t.IsFalse(usedPreferred,
                      "dual-circuit PMODE presentation should not bypass the first circuit with the preferred-copy shortcut");
            t.Equals(latchedWidth, 640u,
                     "dual-circuit presentation should preserve the display width");
            t.Equals(latchedHeight, 448u,
                     "dual-circuit presentation should preserve the display height");
            t.Equals(static_cast<uint32_t>(latchedFrame[0]), 100u,
                     "dual-circuit presentation should blend the first circuit red channel over the second circuit");
            t.Equals(static_cast<uint32_t>(latchedFrame[1]), 0u,
                     "dual-circuit presentation should preserve a zero green channel");
            t.Equals(static_cast<uint32_t>(latchedFrame[2]), 100u,
                     "dual-circuit presentation should blend the second circuit blue channel under the first circuit");
            t.Equals(static_cast<uint32_t>(latchedFrame[3]), 0xFFu,
                     "dual-circuit presentation should normalize the final host alpha");
        });

        tc.Run("latched host presentation normalizes alpha for single-circuit display", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GSRegisters regs{};
            regs.pmode = 0x0001ull;
            regs.dispfb1 =
                150ull |
                (10ull << 9) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 15);
            regs.display1 =
                (639ull << 32) |
                (447ull << 44);

            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), &regs);

            constexpr uint32_t kPixel =
                0x22u |
                (0x44u << 8) |
                (0x66u << 16) |
                (0x01u << 24);
            writeReferenceFramePSMCT32Pixel(vram, 150u, 10u, 0u, 0u, kPixel);

            gs.latchHostPresentationFrame();

            std::vector<uint8_t> latchedFrame;
            uint32_t latchedWidth = 0u;
            uint32_t latchedHeight = 0u;
            t.IsTrue(gs.copyLatchedHostPresentationFrame(latchedFrame, latchedWidth, latchedHeight),
                     "single-circuit presentation should produce a host frame");
            t.Equals(static_cast<uint32_t>(latchedFrame[0]), 0x22u,
                     "single-circuit presentation should preserve the red channel");
            t.Equals(static_cast<uint32_t>(latchedFrame[1]), 0x44u,
                     "single-circuit presentation should preserve the green channel");
            t.Equals(static_cast<uint32_t>(latchedFrame[2]), 0x66u,
                     "single-circuit presentation should preserve the blue channel");
            t.Equals(static_cast<uint32_t>(latchedFrame[3]), 0xFFu,
                     "single-circuit presentation should upload an opaque host alpha");
        });

        tc.Run("latched host presentation preserves 480-line display height", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GSRegisters regs{};
            regs.pmode = 0x0001ull;
            regs.dispfb1 =
                150ull |
                (10ull << 9) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 15);
            regs.display1 =
                (639ull << 32) |
                (479ull << 44);

            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), &regs);

            constexpr uint32_t kLastRowPixel =
                0x12u |
                (0x34u << 8) |
                (0x56u << 16) |
                (0x78u << 24);
            writeReferenceFramePSMCT32Pixel(vram, 150u, 10u, 0u, 479u, kLastRowPixel);

            gs.latchHostPresentationFrame();

            std::vector<uint8_t> latchedFrame;
            uint32_t latchedWidth = 0u;
            uint32_t latchedHeight = 0u;
            t.IsTrue(gs.copyLatchedHostPresentationFrame(latchedFrame, latchedWidth, latchedHeight),
                     "480-line single-circuit presentation should produce a host frame");
            t.Equals(latchedWidth, 640u,
                     "480-line presentation should preserve the display width");
            t.Equals(latchedHeight, 480u,
                     "480-line presentation should preserve the full display height");

            const size_t lastRowOffset = static_cast<size_t>(479u) * 640u * 4u;
            t.Equals(static_cast<uint32_t>(latchedFrame[lastRowOffset + 0u]), 0x12u,
                     "480-line presentation should keep the last row red channel");
            t.Equals(static_cast<uint32_t>(latchedFrame[lastRowOffset + 1u]), 0x34u,
                     "480-line presentation should keep the last row green channel");
            t.Equals(static_cast<uint32_t>(latchedFrame[lastRowOffset + 2u]), 0x56u,
                     "480-line presentation should keep the last row blue channel");
            t.Equals(static_cast<uint32_t>(latchedFrame[lastRowOffset + 3u]), 0xFFu,
                     "single-circuit presentation should normalize the last row alpha");
        });

        tc.Run("latched host presentation line-doubles interlaced field output", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GSRegisters regs{};
            regs.pmode = 0x0001ull;
            regs.smode2 = 0x0001ull; // interlaced, field mode
            regs.dispfb1 =
                0ull |
                (10ull << 9) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 15);
            regs.display1 =
                (639ull << 32) |
                (447ull << 44);

            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), &regs);

            constexpr uint32_t kLine0 = 0x000000FFu;
            constexpr uint32_t kLine1 = 0x0000FF00u;
            constexpr uint32_t kLine2 = 0x00FF0000u;
            constexpr uint32_t kLine3 = 0x00FFFF00u;
            writeReferencePSMCT32Pixel(vram, 0u, 10u, 0u, 0u, kLine0);
            writeReferencePSMCT32Pixel(vram, 0u, 10u, 0u, 1u, kLine1);
            writeReferencePSMCT32Pixel(vram, 0u, 10u, 0u, 2u, kLine2);
            writeReferencePSMCT32Pixel(vram, 0u, 10u, 0u, 3u, kLine3);

            gs.latchHostPresentationFrame();

            std::vector<uint8_t> latchedFrame;
            uint32_t latchedWidth = 0u;
            uint32_t latchedHeight = 0u;
            t.IsTrue(gs.copyLatchedHostPresentationFrame(latchedFrame, latchedWidth, latchedHeight),
                     "interlaced field presentation should produce a host frame");
            t.Equals(latchedWidth, 640u,
                     "field presentation should preserve display width");
            t.Equals(latchedHeight, 448u,
                     "field presentation should preserve display height");

            auto pixelAtRow = [&](uint32_t row) -> uint32_t
            {
                const size_t off = static_cast<size_t>(row) * 640u * 4u;
                return static_cast<uint32_t>(latchedFrame[off + 0u]) |
                       (static_cast<uint32_t>(latchedFrame[off + 1u]) << 8) |
                       (static_cast<uint32_t>(latchedFrame[off + 2u]) << 16);
            };

            const uint32_t row0 = pixelAtRow(0u);
            const uint32_t row1 = pixelAtRow(1u);
            const uint32_t row2 = pixelAtRow(2u);
            const uint32_t row3 = pixelAtRow(3u);

            t.Equals(row0, row1,
                     "field presentation should duplicate the active field into the next scanline");
            t.Equals(row2, row3,
                     "field presentation should duplicate later field scanlines as well");
            t.IsTrue(row0 != row2,
                     "field presentation should still preserve different source content across field rows");
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

        tc.Run("GIF PACKED A+D writes DISPFB2 and DISPLAY2 privileged registers", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GSRegisters regs{};
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), &regs);

            std::vector<uint8_t> packet;
            appendU64(packet, makeGifTag(2u, GIF_FMT_PACKED, 1u, true));
            appendU64(packet, 0x0Eull); // REGS[0] = A+D

            const uint64_t dispfb2 = 0x2222333344445555ull;
            const uint64_t display2 = 0x6666777788889999ull;
            appendU64(packet, dispfb2);
            appendU64(packet, 0x5Bull); // DISPFB2
            appendU64(packet, display2);
            appendU64(packet, 0x5Cull); // DISPLAY2

            gs.processGIFPacket(packet.data(), static_cast<uint32_t>(packet.size()));

            t.Equals(regs.dispfb2, dispfb2, "A+D should write GS DISPFB2");
            t.Equals(regs.display2, display2, "A+D should write GS DISPLAY2");
        });

        tc.Run("PSMT4 address mapping matches GS manual layout", [](TestCase &t)
        {
            constexpr uint32_t kBaseBlock = 0u;
            constexpr uint32_t kWidth = 2u; // One 128x128 PSMT4 page.

            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 0u, 0u), 0u,
                     "PSMT4 origin should map to nibble offset 0");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 1u, 0u), 8u,
                     "PSMT4 x=1 should advance to the next packed nibble group");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 0u, 1u), 16u,
                     "PSMT4 second source row should follow the manual's row packing");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 0u, 2u), 65u,
                     "PSMT4 third source row should include the manual's odd-row permutation");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 0u, 3u), 81u,
                     "PSMT4 fourth source row should stay in the first block's manual column layout");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 31u, 15u), 511u,
                     "PSMT4 final texel in the first 32x16 block should land at the end of the block");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 32u, 0u), 1024u,
                     "PSMT4 x=32 should advance to the next swizzled block in the page");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 32u, 16u), 1536u,
                     "PSMT4 x=32,y=16 should follow the manual's second block-row permutation");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 64u, 0u), 4096u,
                     "PSMT4 x=64 should advance to the third swizzled block column in the page");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 96u, 112u), 15872u,
                     "PSMT4 bottom-right block origin should match the manual's page permutation");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 127u, 127u), 16383u,
                     "PSMT4 final texel in a 128x128 page should land at the end of the page");
            t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, 128u, 0u), 16384u,
                     "PSMT4 x=128 should advance to the next page of nibble addresses");
        });

        tc.Run("PSMT4 large atlases keep manual page layout across 512x512 textures", [](TestCase &t)
        {
            constexpr uint32_t kBaseBlock = 64u;
            constexpr uint32_t kWidth = 8u; // 512 pixel-wide T4 atlas, like Veronica font pages.
            constexpr uint32_t kCoords[][2] = {
                {0u, 0u},
                {31u, 15u},
                {32u, 0u},
                {95u, 31u},
                {127u, 127u},
                {128u, 0u},
                {255u, 127u},
                {256u, 0u},
                {383u, 127u},
                {384u, 128u},
                {511u, 511u},
            };

            for (const auto &coord : kCoords)
            {
                const uint32_t x = coord[0];
                const uint32_t y = coord[1];
                t.Equals(GSPSMT4::addrPSMT4(kBaseBlock, kWidth, x, y),
                         referenceAddrPSMT4(kBaseBlock, kWidth, x, y),
                         "PSMT4 512x512 atlas mapping should match the GS manual for every sampled page boundary");
            }
        });

        tc.Run("GS T4 triangle sampling reads manual-layout texels from a 512x512 atlas", [](TestCase &t)
        {
            constexpr uint32_t kTexTbp = 64u;
            constexpr uint32_t kClutCbp = 128u;
            constexpr uint64_t kFrame =
                (0ull << 0) |
                (1ull << 16) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 24);
            constexpr uint64_t kScissor =
                (0ull << 0) |
                (4ull << 16) |
                (0ull << 32) |
                (4ull << 48);
            constexpr uint64_t kTex0 =
                (static_cast<uint64_t>(kTexTbp) << 0) |
                (8ull << 14) |
                (static_cast<uint64_t>(GS_PSM_T4) << 20) |
                (9ull << 26) |
                (9ull << 30) |
                (1ull << 34) |
                (1ull << 35) |
                (static_cast<uint64_t>(kClutCbp) << 37) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 51) |
                (1ull << 55);
            constexpr uint64_t kPrim =
                static_cast<uint64_t>(GS_PRIM_TRIANGLE) |
                (1ull << 4);
            constexpr uint64_t kRgbaq = 0x3F80000080808080ull;

            auto packFloat = [](float value) -> uint32_t
            {
                uint32_t bits = 0u;
                std::memcpy(&bits, &value, sizeof(bits));
                return bits;
            };

            auto packSt = [&](float s, float tVal) -> uint64_t
            {
                return static_cast<uint64_t>(packFloat(s)) |
                       (static_cast<uint64_t>(packFloat(tVal)) << 32);
            };

            const struct SampleCase
            {
                uint32_t x;
                uint32_t y;
                uint8_t index;
                uint32_t color;
            } cases[] = {
                {5u, 5u, 1u, 0xFF0000FFu},
                {129u, 5u, 2u, 0xFF00FF00u},
                {257u, 5u, 3u, 0xFFFF0000u},
                {385u, 129u, 4u, 0xFFFFFFFFu},
            };

            for (const auto &sample : cases)
            {
                std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
                GS gs;
                gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

                writeReferencePSMT4Texel(vram, kTexTbp, 8u, sample.x, sample.y, sample.index);
                const uint32_t clutOff = referenceAddrPSMCT32(kClutCbp, 1u, sample.index, 0u);
                std::memcpy(vram.data() + clutOff, &sample.color, sizeof(sample.color));

                gs.writeRegister(GS_REG_FRAME_1, kFrame);
                gs.writeRegister(GS_REG_SCISSOR_1, kScissor);
                gs.writeRegister(GS_REG_XYOFFSET_1, 0ull);
                gs.writeRegister(GS_REG_TEST_1, 0ull);
                gs.writeRegister(GS_REG_ALPHA_1, 0ull);
                gs.writeRegister(GS_REG_TEX0_1, kTex0);
                gs.writeRegister(GS_REG_TEX1_1, 0ull);
                gs.writeRegister(GS_REG_PRIM, kPrim);
                gs.writeRegister(GS_REG_RGBAQ, kRgbaq);

                const float s = (static_cast<float>(sample.x) + 0.25f) / 512.0f;
                const float tVal = (static_cast<float>(sample.y) + 0.25f) / 512.0f;
                gs.writeRegister(GS_REG_ST, packSt(s, tVal));
                gs.writeRegister(GS_REG_XYZ2, 0ull);
                gs.writeRegister(GS_REG_ST, packSt(s, tVal));
                gs.writeRegister(GS_REG_XYZ2, (64ull << 0) | (0ull << 16));
                gs.writeRegister(GS_REG_ST, packSt(s, tVal));
                gs.writeRegister(GS_REG_XYZ2, (0ull << 0) | (64ull << 16));

                const uint32_t pixel = readReferencePSMCT32Pixel(vram, 0u, 1u, 1u, 1u);
                t.Equals(pixel, sample.color,
                         "T4 triangle sampling should fetch the manual-layout atlas texel from the correct 128x128 page");
            }
        });

        tc.Run("PSMT8 address mapping matches Veronica Conv8to32 layout", [](TestCase &t)
        {
            constexpr uint32_t kBaseBlock = 0u;
            constexpr uint32_t kWidth = 2u; // One 128x64 PSMT8 page.

            t.Equals(GSPSMT8::addrPSMT8(kBaseBlock, kWidth, 0u, 0u), 0u,
                     "PSMT8 origin should map to byte offset 0");
            t.Equals(GSPSMT8::addrPSMT8(kBaseBlock, kWidth, 1u, 0u), 4u,
                     "PSMT8 x=1 should follow Veronica's Conv8to32 byte interleave");
            t.Equals(GSPSMT8::addrPSMT8(kBaseBlock, kWidth, 0u, 1u), 8u,
                     "PSMT8 second source row should land on the next Conv8to32 row stride");
            t.Equals(GSPSMT8::addrPSMT8(kBaseBlock, kWidth, 0u, 2u), 33u,
                     "PSMT8 third source row should preserve Veronica's odd-row shuffle");
            t.Equals(GSPSMT8::addrPSMT8(kBaseBlock, kWidth, 0u, 3u), 41u,
                     "PSMT8 fourth source row should preserve Veronica's alternating block rows");
            t.Equals(GSPSMT8::addrPSMT8(kBaseBlock, kWidth, 15u, 15u), 255u,
                     "PSMT8 final texel in the first 16x16 block should end at byte 255");
            t.Equals(GSPSMT8::addrPSMT8(kBaseBlock, kWidth, 16u, 0u), 256u,
                     "PSMT8 x=16 should advance to the next 16x16 block");
            t.Equals(GSPSMT8::addrPSMT8(kBaseBlock, kWidth, 16u, 16u), 768u,
                     "PSMT8 x=16,y=16 should include both block-column and block-row offsets");
            t.Equals(GSPSMT8::addrPSMT8(kBaseBlock, kWidth, 32u, 0u), 1024u,
                     "PSMT8 x=32 should advance to the third block column in the page");
            t.Equals(GSPSMT8::addrPSMT8(kBaseBlock, kWidth, 64u, 0u), 4096u,
                     "PSMT8 x=64 should advance to the second page half");
            t.Equals(GSPSMT8::addrPSMT8(kBaseBlock, kWidth, 96u, 48u), 7680u,
                     "PSMT8 lower-right interior block should follow Veronica's page permutation");
            t.Equals(GSPSMT8::addrPSMT8(kBaseBlock, kWidth, 127u, 63u), 8191u,
                     "PSMT8 final texel in a 128x64 page should land at the final byte");
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
            for (uint32_t x = 0; x < 4u && imageOk; ++x)
            {
                const uint32_t off = referenceAddrPSMCT32(0u, 1u, x, 0u);
                for (uint32_t c = 0; c < 4u; ++c)
                {
                    if (vram[off + c] != payload[x * 4u + c])
                    {
                        imageOk = false;
                        break;
                    }
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
            for (uint32_t x = 0; x < 4u && imageOk; ++x)
            {
                const uint32_t off = referenceAddrPSMCT32(0u, 1u, x, 0u);
                for (uint32_t c = 0; c < 4u; ++c)
                {
                    if (vram[off + c] != payload[x * 4u + c])
                    {
                        imageOk = false;
                        break;
                    }
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
            for (uint32_t y = 0; y < 2u && same; ++y)
            {
                for (uint32_t x = 0; x < 2u; ++x)
                {
                    const uint32_t pixelIndex = y * 2u + x;
                    const uint32_t off = referenceAddrPSMCT32(0u, 1u, x, y);
                    for (uint32_t c = 0; c < 4u; ++c)
                    {
                        if (vram[off + c] != payload[pixelIndex * 4u + c])
                        {
                            same = false;
                            break;
                        }
                    }
                    if (!same)
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

            for (uint32_t x = 0; x < 4u; ++x)
            {
                const uint32_t off = referenceAddrPSMCT32(0u, 1u, x, 0u);
                for (uint32_t c = 0; c < 4u; ++c)
                {
                    vram[off + c] = static_cast<uint8_t>(0xA0u + x * 4u + c);
                }
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

        tc.Run("GS CT32 upload aliases cleanly into later PSMT8 sampling", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            constexpr uint32_t kTexWidth = 128u;
            constexpr uint32_t kTexHeight = 64u;
            constexpr uint32_t kUploadWidth = 64u;
            constexpr uint32_t kUploadHeight = 32u;
            constexpr uint32_t kTexTbp = 0u;
            constexpr uint32_t kTexTbw = 2u;

            std::vector<uint8_t> source(kTexWidth * kTexHeight, 0u);
            for (uint32_t i = 0; i < source.size(); ++i)
            {
                source[i] = static_cast<uint8_t>((i * 37u + 11u) & 0xFFu);
            }

            std::vector<uint16_t> rawToUpload(8192u, 0xFFFFu);
            for (uint32_t y = 0; y < kUploadHeight; ++y)
            {
                for (uint32_t x = 0; x < kUploadWidth; ++x)
                {
                    const uint32_t rawBase = referenceAddrPSMCT32(kTexTbp, 1u, x, y);
                    const uint32_t uploadBase = ((y * kUploadWidth) + x) * 4u;
                    for (uint32_t c = 0; c < 4u; ++c)
                    {
                        rawToUpload[rawBase + c] = static_cast<uint16_t>(uploadBase + c);
                    }
                }
            }

            bool inverseComplete = true;
            for (uint16_t byteOff : rawToUpload)
            {
                if (byteOff == 0xFFFFu)
                {
                    inverseComplete = false;
                    break;
                }
            }
            t.IsTrue(inverseComplete,
                     "reference CT32 raw-to-upload map should cover every byte in a 64x32 CT32 page");

            std::vector<uint8_t> upload(kUploadWidth * kUploadHeight * 4u, 0u);
            for (uint32_t y = 0; y < kTexHeight; ++y)
            {
                for (uint32_t x = 0; x < kTexWidth; ++x)
                {
                    const uint32_t texelIndex = y * kTexWidth + x;
                    const uint32_t rawOff = referenceAddrPSMT8(kTexTbp, kTexTbw, x, y);
                    const uint32_t uploadOff = rawToUpload[rawOff];
                    upload[uploadOff] = source[texelIndex];
                }
            }

            const uint64_t bitblt =
                (static_cast<uint64_t>(0u) << 0) |
                (static_cast<uint64_t>(1u) << 16) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 24) |
                (static_cast<uint64_t>(kTexTbp) << 32) |
                (static_cast<uint64_t>(1u) << 48) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 56);
            gs.writeRegister(GS_REG_BITBLTBUF, bitblt);
            gs.writeRegister(GS_REG_TRXPOS, 0ull);
            gs.writeRegister(GS_REG_TRXREG, (static_cast<uint64_t>(kUploadWidth) << 0) |
                                            (static_cast<uint64_t>(kUploadHeight) << 32));
            gs.writeRegister(GS_REG_TRXDIR, 0ull);

            std::vector<uint8_t> packet;
            appendU64(packet, makeGifTag(static_cast<uint16_t>(upload.size() / 16u), GIF_FMT_IMAGE, 0u, true));
            appendU64(packet, 0ull);
            packet.insert(packet.end(), upload.begin(), upload.end());
            gs.processGIFPacket(packet.data(), static_cast<uint32_t>(packet.size()));

            bool aliasOk = true;
            uint32_t badX = 0u;
            uint32_t badY = 0u;
            uint32_t got = 0u;
            uint32_t expected = 0u;
            for (uint32_t y = 0; y < kTexHeight && aliasOk; ++y)
            {
                for (uint32_t x = 0; x < kTexWidth; ++x)
                {
                    const uint32_t texelOff = GSPSMT8::addrPSMT8(kTexTbp, kTexTbw, x, y);
                    got = vram[texelOff];
                    expected = source[y * kTexWidth + x];
                    if (got != expected)
                    {
                        aliasOk = false;
                        badX = x;
                        badY = y;
                        break;
                    }
                }
            }

            if (!aliasOk)
            {
                t.Fail("CT32 image upload should preserve Veronica's later PSMT8 sampling layout "
                       "(first mismatch at x=" + std::to_string(badX) +
                       ", y=" + std::to_string(badY) +
                       ", got " + std::to_string(got) +
                       ", expected " + std::to_string(expected) + ")");
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

            // Veronica uploads CSM1 CLUT rows with a 64-pixel GS stride, so logical entry 8
            // resolves to row 1, column 0 after the CSM1 swizzle.
            const uint32_t wrongClutOff = GSPSMCT32::addrPSMCT32(kClutCbp, 1u, 8u, 0u);
            const uint32_t expectedClutOff = GSPSMCT32::addrPSMCT32(kClutCbp, 1u, 0u, 1u);
            std::memcpy(vram.data() + wrongClutOff, &kWrongColor, sizeof(kWrongColor));
            std::memcpy(vram.data() + expectedClutOff, &kExpectedColor, sizeof(kExpectedColor));

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
                     "T4 CSM1 lookup should follow Veronica's swizzled CLUT row layout for logical index 8");
        });

        tc.Run("GS T8 CT32-uploaded CSM1 CLUT follows swizzled palette layout", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            constexpr uint32_t kTexTbp = 64u;
            constexpr uint32_t kClutCbp = 128u;
            constexpr uint64_t kFrameReg =
                (0ull << 0) |
                (1ull << 16) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 24);
            constexpr uint64_t kTex0 =
                (static_cast<uint64_t>(kTexTbp) << 0) |
                (1ull << 14) |
                (static_cast<uint64_t>(GS_PSM_T8) << 20) |
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
            constexpr uint64_t kClutBitblt =
                (0ull << 0) |
                (1ull << 16) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 24) |
                (static_cast<uint64_t>(kClutCbp) << 32) |
                (1ull << 48) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 56);
            constexpr uint64_t kClutRect =
                (16ull << 0) |
                (2ull << 32);
            constexpr uint32_t kExpectedColor = 0x80FFFFFFu;

            const uint32_t texOff = GSPSMT8::addrPSMT8(kTexTbp, 1u, 0u, 0u);
            vram[texOff] = 8u;

            std::vector<uint32_t> clut(32u, 0u);
            clut[16] = kExpectedColor;

            gs.writeRegister(GS_REG_BITBLTBUF, kClutBitblt);
            gs.writeRegister(GS_REG_TRXPOS, 0ull);
            gs.writeRegister(GS_REG_TRXREG, kClutRect);
            gs.writeRegister(GS_REG_TRXDIR, 0ull);

            std::vector<uint8_t> packet;
            appendU64(packet,
                      makeGifTag(static_cast<uint16_t>((clut.size() * sizeof(uint32_t)) / 16u),
                                 GIF_FMT_IMAGE,
                                 0u,
                                 true));
            appendU64(packet, 0ull);
            const size_t payloadOffset = packet.size();
            packet.resize(payloadOffset + clut.size() * sizeof(uint32_t));
            std::memcpy(packet.data() + payloadOffset, clut.data(), clut.size() * sizeof(uint32_t));
            gs.processGIFPacket(packet.data(), static_cast<uint32_t>(packet.size()));

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
                     "T8 CSM1 CLUT sampling should read CT32-uploaded palette entries through GS swizzled addressing");
        });

        tc.Run("GS TEX2 updates CLUT state independently from TEX0", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            constexpr uint32_t kTexTbp = 64u;
            constexpr uint32_t kWrongClutCbp = 128u;
            constexpr uint32_t kExpectedClutCbp = 192u;
            constexpr uint64_t kFrameReg =
                (0ull << 0) |
                (1ull << 16) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 24);
            constexpr uint64_t kTex0 =
                (static_cast<uint64_t>(kTexTbp) << 0) |
                (1ull << 14) |
                (static_cast<uint64_t>(GS_PSM_T8) << 20) |
                (0ull << 26) |
                (0ull << 30) |
                (1ull << 34) |
                (1ull << 35) |
                (static_cast<uint64_t>(kWrongClutCbp) << 37) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 51) |
                (1ull << 55);
            constexpr uint64_t kTex2 =
                (static_cast<uint64_t>(GS_PSM_T8) << 20) |
                (static_cast<uint64_t>(kExpectedClutCbp) << 37) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 51) |
                (1ull << 55);
            constexpr uint64_t kPrim =
                static_cast<uint64_t>(GS_PRIM_SPRITE) |
                (1ull << 4) |
                (1ull << 8);
            constexpr uint32_t kWrongColor = 0xFF00FF00u;
            constexpr uint32_t kExpectedColor = 0xFF0000FFu;

            const uint32_t texOff = GSPSMT8::addrPSMT8(kTexTbp, 1u, 0u, 0u);
            vram[texOff] = 8u;

            const uint32_t wrongClutOff = GSPSMCT32::addrPSMCT32(kWrongClutCbp, 1u, 8u, 0u);
            const uint32_t expectedClutOff = GSPSMCT32::addrPSMCT32(kExpectedClutCbp, 1u, 8u, 0u);
            std::memcpy(vram.data() + wrongClutOff, &kWrongColor, sizeof(kWrongColor));
            std::memcpy(vram.data() + expectedClutOff, &kExpectedColor, sizeof(kExpectedColor));

            gs.writeRegister(GS_REG_FRAME_1, kFrameReg);
            gs.writeRegister(GS_REG_SCISSOR_1, 0ull);
            gs.writeRegister(GS_REG_XYOFFSET_1, 0ull);
            gs.writeRegister(GS_REG_TEST_1, 0ull);
            gs.writeRegister(GS_REG_ALPHA_1, 0ull);
            gs.writeRegister(GS_REG_TEX0_1, kTex0);
            gs.writeRegister(GS_REG_TEX2_1, kTex2);
            gs.writeRegister(GS_REG_PRIM, kPrim);
            gs.writeRegister(GS_REG_RGBAQ, 0x80808080ull);
            gs.writeRegister(GS_REG_UV, 0ull);
            gs.writeRegister(GS_REG_XYZ2, 0ull);
            gs.writeRegister(GS_REG_UV, 0ull);
            gs.writeRegister(GS_REG_XYZ2, (16ull << 0) | (16ull << 16));

            uint32_t pixel = 0u;
            std::memcpy(&pixel, vram.data(), sizeof(pixel));
            t.Equals(pixel, kExpectedColor,
                     "TEX2 should override the active CLUT base and format state without requiring a new TEX0 write");
        });

        tc.Run("GS TEXCLUT offsets T8 CLUT fetch coordinates", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            constexpr uint32_t kTexTbp = 64u;
            constexpr uint32_t kClutCbp = 128u;
            constexpr uint64_t kFrameReg =
                (0ull << 0) |
                (1ull << 16) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 24);
            constexpr uint64_t kTex0 =
                (static_cast<uint64_t>(kTexTbp) << 0) |
                (1ull << 14) |
                (static_cast<uint64_t>(GS_PSM_T8) << 20) |
                (0ull << 26) |
                (0ull << 30) |
                (1ull << 34) |
                (1ull << 35) |
                (static_cast<uint64_t>(kClutCbp) << 37) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 51) |
                (1ull << 55);
            constexpr uint64_t kTexClut =
                (1ull << 0) |
                (3ull << 6) |
                (2ull << 12);
            constexpr uint64_t kPrim =
                static_cast<uint64_t>(GS_PRIM_SPRITE) |
                (1ull << 4) |
                (1ull << 8);
            constexpr uint32_t kWrongColor = 0xFF00FF00u;
            constexpr uint32_t kExpectedColor = 0xFF3366CCu;

            const uint32_t texOff = GSPSMT8::addrPSMT8(kTexTbp, 1u, 0u, 0u);
            vram[texOff] = 0u;

            const uint32_t wrongClutOff = GSPSMCT32::addrPSMCT32(kClutCbp, 1u, 0u, 0u);
            const uint32_t expectedClutOff = GSPSMCT32::addrPSMCT32(kClutCbp, 1u, 3u, 2u);
            std::memcpy(vram.data() + wrongClutOff, &kWrongColor, sizeof(kWrongColor));
            std::memcpy(vram.data() + expectedClutOff, &kExpectedColor, sizeof(kExpectedColor));

            gs.writeRegister(GS_REG_FRAME_1, kFrameReg);
            gs.writeRegister(GS_REG_SCISSOR_1, 0ull);
            gs.writeRegister(GS_REG_XYOFFSET_1, 0ull);
            gs.writeRegister(GS_REG_TEST_1, 0ull);
            gs.writeRegister(GS_REG_ALPHA_1, 0ull);
            gs.writeRegister(GS_REG_TEX0_1, kTex0);
            gs.writeRegister(GS_REG_TEXCLUT, kTexClut);
            gs.writeRegister(GS_REG_PRIM, kPrim);
            gs.writeRegister(GS_REG_RGBAQ, 0x80808080ull);
            gs.writeRegister(GS_REG_UV, 0ull);
            gs.writeRegister(GS_REG_XYZ2, 0ull);
            gs.writeRegister(GS_REG_UV, 0ull);
            gs.writeRegister(GS_REG_XYZ2, (16ull << 0) | (16ull << 16));

            uint32_t pixel = 0u;
            std::memcpy(&pixel, vram.data(), sizeof(pixel));
            t.Equals(pixel, kExpectedColor,
                     "TEXCLUT should offset the CLUT lookup coordinates instead of always starting from the CLUT base");
        });

        tc.Run("GS TEXA expands CT24 alpha and honors AEM for black texels", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            constexpr uint32_t kTexTbp = 64u;
            constexpr uint64_t kFrameReg =
                (0ull << 0) |
                (1ull << 16) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 24);
            constexpr uint64_t kScissor =
                (0ull << 0) |
                (1ull << 16) |
                (0ull << 32) |
                (0ull << 48);
            constexpr uint64_t kTex0 =
                (static_cast<uint64_t>(kTexTbp) << 0) |
                (1ull << 14) |
                (static_cast<uint64_t>(GS_PSM_CT24) << 20) |
                (1ull << 26) |
                (0ull << 30) |
                (1ull << 34) |
                (1ull << 35);
            constexpr uint64_t kTexa =
                (0x55ull << 0) |
                (1ull << 15) |
                (0xAAull << 32);
            constexpr uint64_t kPrim =
                static_cast<uint64_t>(GS_PRIM_SPRITE) |
                (1ull << 4) |
                (1ull << 8);
            constexpr uint32_t kExpectedRed = 0x550000FFu;
            constexpr uint32_t kExpectedBlack = 0x00000000u;

            const uint32_t redOff = GSPSMCT32::addrPSMCT32(kTexTbp, 1u, 0u, 0u);
            vram[redOff + 0u] = 0xFFu;
            vram[redOff + 1u] = 0x00u;
            vram[redOff + 2u] = 0x00u;
            vram[redOff + 3u] = 0x00u;

            const uint32_t blackOff = GSPSMCT32::addrPSMCT32(kTexTbp, 1u, 1u, 0u);
            vram[blackOff + 0u] = 0x00u;
            vram[blackOff + 1u] = 0x00u;
            vram[blackOff + 2u] = 0x00u;
            vram[blackOff + 3u] = 0x00u;

            gs.writeRegister(GS_REG_FRAME_1, kFrameReg);
            gs.writeRegister(GS_REG_SCISSOR_1, kScissor);
            gs.writeRegister(GS_REG_XYOFFSET_1, 0ull);
            gs.writeRegister(GS_REG_TEST_1, 0ull);
            gs.writeRegister(GS_REG_ALPHA_1, 0ull);
            gs.writeRegister(GS_REG_TEX0_1, kTex0);
            gs.writeRegister(GS_REG_TEXA, kTexa);
            gs.writeRegister(GS_REG_PRIM, kPrim);
            gs.writeRegister(GS_REG_RGBAQ, 0x80808080ull);

            gs.writeRegister(GS_REG_UV, 0ull);
            gs.writeRegister(GS_REG_XYZ2, 0ull);
            gs.writeRegister(GS_REG_UV, 0ull);
            gs.writeRegister(GS_REG_XYZ2, (16ull << 0) | (16ull << 16));

            gs.writeRegister(GS_REG_UV, (16ull << 0));
            gs.writeRegister(GS_REG_XYZ2, (16ull << 0));
            gs.writeRegister(GS_REG_UV, (16ull << 0));
            gs.writeRegister(GS_REG_XYZ2, (32ull << 0) | (16ull << 16));

            const uint32_t redPixel = readReferencePSMCT32Pixel(vram, 0u, 1u, 0u, 0u);
            const uint32_t blackPixel = readReferencePSMCT32Pixel(vram, 0u, 1u, 1u, 0u);
            t.Equals(redPixel, kExpectedRed,
                     "TEXA should supply TA0 as the alpha for non-alpha CT24 texels");
            t.Equals(blackPixel, kExpectedBlack,
                     "TEXA AEM should force zero alpha when a CT24 texel is RGB=0");
        });

        tc.Run("GS TCC=0 MODULATE uses texture RGB and keeps vertex alpha", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            constexpr uint32_t kTexTbp = 64u;
            constexpr uint64_t kFrameReg =
                (0ull << 0) |
                (1ull << 16) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 24);
            constexpr uint64_t kScissor =
                (0ull << 0) |
                (0ull << 16) |
                (0ull << 32) |
                (0ull << 48);
            constexpr uint64_t kTex0 =
                (static_cast<uint64_t>(kTexTbp) << 0) |
                (1ull << 14) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 20) |
                (0ull << 26) |
                (0ull << 30) |
                (0ull << 34) |
                (0ull << 35);
            constexpr uint64_t kPrim =
                static_cast<uint64_t>(GS_PRIM_SPRITE) |
                (1ull << 4) |
                (1ull << 8);
            constexpr uint32_t kTexturePixel =
                0x12u |
                (0x34u << 8) |
                (0x56u << 16) |
                (0x78u << 24);
            constexpr uint32_t kExpectedPixel =
                0x12u |
                (0x34u << 8) |
                (0x56u << 16) |
                (0x44u << 24);

            const uint32_t texOff = GSPSMCT32::addrPSMCT32(kTexTbp, 1u, 0u, 0u);
            std::memcpy(vram.data() + texOff, &kTexturePixel, sizeof(kTexturePixel));

            gs.writeRegister(GS_REG_FRAME_1, kFrameReg);
            gs.writeRegister(GS_REG_SCISSOR_1, kScissor);
            gs.writeRegister(GS_REG_XYOFFSET_1, 0ull);
            gs.writeRegister(GS_REG_TEST_1, 0ull);
            gs.writeRegister(GS_REG_ALPHA_1, 0ull);
            gs.writeRegister(GS_REG_TEX0_1, kTex0);
            gs.writeRegister(GS_REG_PRIM, kPrim);
            gs.writeRegister(GS_REG_RGBAQ, 0x44808080ull);
            gs.writeRegister(GS_REG_UV, 0ull);
            gs.writeRegister(GS_REG_XYZ2, 0ull);
            gs.writeRegister(GS_REG_UV, 0ull);
            gs.writeRegister(GS_REG_XYZ2, (16ull << 0) | (16ull << 16));

            const uint32_t pixel = readReferencePSMCT32Pixel(vram, 0u, 1u, 0u, 0u);
            t.Equals(pixel, kExpectedPixel,
                     "TCC=0 MODULATE should still use texture RGB while sourcing alpha from the shaded vertex");
        });

        tc.Run("GS HIGHLIGHT adds vertex alpha into RGB and texture alpha into A", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            constexpr uint32_t kTexTbp = 64u;
            constexpr uint64_t kFrameReg =
                (0ull << 0) |
                (1ull << 16) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 24);
            constexpr uint64_t kScissor =
                (0ull << 0) |
                (0ull << 16) |
                (0ull << 32) |
                (0ull << 48);
            constexpr uint64_t kTex0 =
                (static_cast<uint64_t>(kTexTbp) << 0) |
                (1ull << 14) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 20) |
                (0ull << 26) |
                (0ull << 30) |
                (1ull << 34) |
                (2ull << 35);
            constexpr uint64_t kPrim =
                static_cast<uint64_t>(GS_PRIM_SPRITE) |
                (1ull << 4) |
                (1ull << 8);
            constexpr uint32_t kTexturePixel =
                0x20u |
                (0x40u << 8) |
                (0x60u << 16) |
                (0x10u << 24);
            constexpr uint32_t kExpectedPixel =
                0x40u |
                (0x60u << 8) |
                (0x80u << 16) |
                (0x30u << 24);

            const uint32_t texOff = GSPSMCT32::addrPSMCT32(kTexTbp, 1u, 0u, 0u);
            std::memcpy(vram.data() + texOff, &kTexturePixel, sizeof(kTexturePixel));

            gs.writeRegister(GS_REG_FRAME_1, kFrameReg);
            gs.writeRegister(GS_REG_SCISSOR_1, kScissor);
            gs.writeRegister(GS_REG_XYOFFSET_1, 0ull);
            gs.writeRegister(GS_REG_TEST_1, 0ull);
            gs.writeRegister(GS_REG_ALPHA_1, 0ull);
            gs.writeRegister(GS_REG_TEX0_1, kTex0);
            gs.writeRegister(GS_REG_PRIM, kPrim);
            gs.writeRegister(GS_REG_RGBAQ, 0x20808080ull);
            gs.writeRegister(GS_REG_UV, 0ull);
            gs.writeRegister(GS_REG_XYZ2, 0ull);
            gs.writeRegister(GS_REG_UV, 0ull);
            gs.writeRegister(GS_REG_XYZ2, (16ull << 0) | (16ull << 16));

            const uint32_t pixel = readReferencePSMCT32Pixel(vram, 0u, 1u, 0u, 0u);
            t.Equals(pixel, kExpectedPixel,
                     "HIGHLIGHT should add the shaded vertex alpha into RGB and accumulate texture plus vertex alpha");
        });

        tc.Run("GS HIGHLIGHT2 keeps texture alpha while adding vertex alpha into RGB", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            constexpr uint32_t kTexTbp = 64u;
            constexpr uint64_t kFrameReg =
                (0ull << 0) |
                (1ull << 16) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 24);
            constexpr uint64_t kScissor =
                (0ull << 0) |
                (0ull << 16) |
                (0ull << 32) |
                (0ull << 48);
            constexpr uint64_t kTex0 =
                (static_cast<uint64_t>(kTexTbp) << 0) |
                (1ull << 14) |
                (static_cast<uint64_t>(GS_PSM_CT32) << 20) |
                (0ull << 26) |
                (0ull << 30) |
                (1ull << 34) |
                (3ull << 35);
            constexpr uint64_t kPrim =
                static_cast<uint64_t>(GS_PRIM_SPRITE) |
                (1ull << 4) |
                (1ull << 8);
            constexpr uint32_t kTexturePixel =
                0x20u |
                (0x40u << 8) |
                (0x60u << 16) |
                (0x10u << 24);
            constexpr uint32_t kExpectedPixel =
                0x40u |
                (0x60u << 8) |
                (0x80u << 16) |
                (0x10u << 24);

            const uint32_t texOff = GSPSMCT32::addrPSMCT32(kTexTbp, 1u, 0u, 0u);
            std::memcpy(vram.data() + texOff, &kTexturePixel, sizeof(kTexturePixel));

            gs.writeRegister(GS_REG_FRAME_1, kFrameReg);
            gs.writeRegister(GS_REG_SCISSOR_1, kScissor);
            gs.writeRegister(GS_REG_XYOFFSET_1, 0ull);
            gs.writeRegister(GS_REG_TEST_1, 0ull);
            gs.writeRegister(GS_REG_ALPHA_1, 0ull);
            gs.writeRegister(GS_REG_TEX0_1, kTex0);
            gs.writeRegister(GS_REG_PRIM, kPrim);
            gs.writeRegister(GS_REG_RGBAQ, 0x20808080ull);
            gs.writeRegister(GS_REG_UV, 0ull);
            gs.writeRegister(GS_REG_XYZ2, 0ull);
            gs.writeRegister(GS_REG_UV, 0ull);
            gs.writeRegister(GS_REG_XYZ2, (16ull << 0) | (16ull << 16));

            const uint32_t pixel = readReferencePSMCT32Pixel(vram, 0u, 1u, 0u, 0u);
            t.Equals(pixel, kExpectedPixel,
                     "HIGHLIGHT2 should add the shaded vertex alpha into RGB while preserving the texture alpha");
        });

        tc.Run("GS TEX1 linear filter blends T4 STQ triangle samples", [](TestCase &t)
        {
            auto renderSamplePixel = [](uint64_t tex1Reg) -> uint32_t
            {
                std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
                GS gs;
                gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

                constexpr uint32_t kTexTbp = 64u;
                constexpr uint32_t kClutCbp = 128u;
                constexpr uint64_t kFrame =
                    (0ull << 0) |
                    (1ull << 16) |
                    (static_cast<uint64_t>(GS_PSM_CT32) << 24);
                constexpr uint64_t kScissor =
                    (0ull << 0) |
                    (4ull << 16) |
                    (0ull << 32) |
                    (4ull << 48);
                constexpr uint64_t kTex0 =
                    (static_cast<uint64_t>(kTexTbp) << 0) |
                    (1ull << 14) |
                    (static_cast<uint64_t>(GS_PSM_T4) << 20) |
                    (1ull << 26) |
                    (0ull << 30) |
                    (1ull << 34) |
                    (1ull << 35) |
                    (static_cast<uint64_t>(kClutCbp) << 37) |
                    (static_cast<uint64_t>(GS_PSM_CT32) << 51);
                constexpr uint64_t kPrim =
                    static_cast<uint64_t>(GS_PRIM_TRIANGLE) |
                    (1ull << 4) |
                    (0ull << 8);
                constexpr uint64_t kRgbaq = 0x3F80000080808080ull;
                constexpr uint32_t kBlack = 0x80000000u;
                constexpr uint32_t kWhite = 0x80FFFFFFu;

                writePSMT4Texel(vram, kTexTbp, 1u, 0u, 0u, 0u);
                writePSMT4Texel(vram, kTexTbp, 1u, 1u, 0u, 1u);
                std::memcpy(vram.data() + kClutCbp * 256u + 0u * 4u, &kBlack, sizeof(kBlack));
                std::memcpy(vram.data() + kClutCbp * 256u + 1u * 4u, &kWhite, sizeof(kWhite));

                auto packFloat = [](float value) -> uint32_t
                {
                    uint32_t bits = 0u;
                    std::memcpy(&bits, &value, sizeof(bits));
                    return bits;
                };

                auto packSt = [&](float s, float tVal) -> uint64_t
                {
                    return static_cast<uint64_t>(packFloat(s)) |
                           (static_cast<uint64_t>(packFloat(tVal)) << 32);
                };

                gs.writeRegister(GS_REG_FRAME_1, kFrame);
                gs.writeRegister(GS_REG_SCISSOR_1, kScissor);
                gs.writeRegister(GS_REG_XYOFFSET_1, 0ull);
                gs.writeRegister(GS_REG_TEST_1, 0ull);
                gs.writeRegister(GS_REG_ALPHA_1, 0ull);
                gs.writeRegister(GS_REG_TEX0_1, kTex0);
                gs.writeRegister(GS_REG_TEX1_1, tex1Reg);
                gs.writeRegister(GS_REG_PRIM, kPrim);
                gs.writeRegister(GS_REG_RGBAQ, kRgbaq);
                gs.writeRegister(GS_REG_ST, packSt(0.0f, 0.0f));
                gs.writeRegister(GS_REG_XYZ2, 0ull);
                gs.writeRegister(GS_REG_ST, packSt(1.0f, 0.0f));
                gs.writeRegister(GS_REG_XYZ2, (64ull << 0) | (0ull << 16));
                gs.writeRegister(GS_REG_ST, packSt(0.0f, 0.0f));
                gs.writeRegister(GS_REG_XYZ2, (0ull << 0) | (64ull << 16));

                return readReferencePSMCT32Pixel(vram, 0u, 1u, 1u, 1u);
            };

            constexpr uint64_t kTex1Linear =
                (1ull << 5) |
                (1ull << 6);

            const uint32_t nearestPixel = renderSamplePixel(0ull);
            const uint32_t linearPixel = renderSamplePixel(kTex1Linear);

            t.Equals(nearestPixel, 0x80000000u,
                     "point sampling should keep the sampled STQ triangle pixel on texel 0");

            const uint8_t linearR = static_cast<uint8_t>(linearPixel & 0xFFu);
            const uint8_t linearA = static_cast<uint8_t>((linearPixel >> 24) & 0xFFu);
            t.IsTrue(linearR > 0x10u && linearR < 0x70u,
                     "linear filtering should blend the STQ triangle sample between black and white T4 texels");
            t.Equals(linearA, static_cast<uint8_t>(0x80u),
                     "linear filtering should preserve the shared opaque alpha from the CLUT entries");
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

        tc.Run("GS triangle fan subpixel quad fills rows without interior holes", [](TestCase &t)
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
                (31ull << 16) |
                (0ull << 32) |
                (31ull << 48);
            constexpr uint64_t kPrim =
                static_cast<uint64_t>(GS_PRIM_TRIFAN);
            constexpr uint64_t kRgbaq =
                0xFFull |
                (0xFFull << 8) |
                (0xFFull << 16) |
                (0x80ull << 24) |
                (0x3F800000ull << 32); // q = 1.0f
            auto makeXyzf = [](uint16_t x, uint16_t y) -> uint64_t
            {
                return static_cast<uint64_t>(x) |
                       (static_cast<uint64_t>(y) << 16);
            };

            gs.writeRegister(GS_REG_FRAME_1, kFrame);
            gs.writeRegister(GS_REG_SCISSOR_1, kScissor);
            gs.writeRegister(GS_REG_XYOFFSET_1, 0ull);
            gs.writeRegister(GS_REG_TEST_1, 0ull);
            gs.writeRegister(GS_REG_PRIM, kPrim);
            gs.writeRegister(GS_REG_RGBAQ, kRgbaq);
            gs.writeRegister(GS_REG_XYZF2, makeXyzf(102u, 102u));
            gs.writeRegister(GS_REG_XYZF2, makeXyzf(420u, 102u));
            gs.writeRegister(GS_REG_XYZF2, makeXyzf(420u, 420u));
            gs.writeRegister(GS_REG_XYZF2, makeXyzf(102u, 420u));

            bool sawFilledRow = false;
            for (uint32_t y = 6u; y <= 26u; ++y)
            {
                int first = -1;
                int last = -1;
                for (uint32_t x = 6u; x <= 26u; ++x)
                {
                    const size_t offset = (static_cast<size_t>(y) * 64u + static_cast<size_t>(x)) * 4u;
                    uint32_t pixel = 0u;
                    std::memcpy(&pixel, vram.data() + offset, sizeof(pixel));
                    if ((pixel & 0x00FFFFFFu) != 0u)
                    {
                        if (first < 0)
                        {
                            first = static_cast<int>(x);
                        }
                        last = static_cast<int>(x);
                    }
                }

                if (first < 0 || last < 0)
                {
                    continue;
                }

                sawFilledRow = true;
                for (int x = first; x <= last; ++x)
                {
                    const size_t offset = (static_cast<size_t>(y) * 64u + static_cast<size_t>(x)) * 4u;
                    uint32_t pixel = 0u;
                    std::memcpy(&pixel, vram.data() + offset, sizeof(pixel));
                    if ((pixel & 0x00FFFFFFu) == 0u)
                    {
                        t.Fail("triangle fan quad should not leave interior holes within a covered row");
                        break;
                    }
                }
            }

            t.IsTrue(sawFilledRow,
                     "triangle fan quad should light at least one framebuffer row");
        });

        tc.Run("sceGsExecLoadImage and sceGsExecStoreImage roundtrip and free guest packets", [](TestCase &t)
        {
            PS2Runtime runtime;
            t.IsTrue(runtime.memory().initialize(), "runtime memory initialize should succeed");
            uint8_t *const rdram = runtime.memory().getRDRAM();
            constexpr uint32_t kImageAddr = 0x4000u;
            constexpr uint32_t kSrcAddr = 0x5000u;
            constexpr uint32_t kDstAddr = 0x6000u;

            const GsImageMem image{0u, 0u, 2u, 2u, 0u, 1u, 0u};
            const uint8_t pixels[16] = {
                0x10u, 0x20u, 0x30u, 0x40u,
                0x50u, 0x60u, 0x70u, 0x80u,
                0x90u, 0xA0u, 0xB0u, 0xC0u,
                0xD0u, 0xE0u, 0xF0u, 0xFFu,
            };

            writeGsImageTest(rdram, kImageAddr, image);
            std::memcpy(rdram + kSrcAddr, pixels, sizeof(pixels));

            R5900Context loadCtx{};
            setRegU32(loadCtx, 4, kImageAddr);
            setRegU32(loadCtx, 5, kSrcAddr);
            ps2_stubs::sceGsExecLoadImage(rdram, &loadCtx, &runtime);
            t.Equals(static_cast<int32_t>(getRegU32Test(loadCtx, 2)), 0,
                     "sceGsExecLoadImage should succeed for a simple CT32 upload");
            uint64_t loadTag = 0u;
            std::memcpy(&loadTag, rdram + runtime.guestHeapBase(), sizeof(loadTag));
            t.Equals(loadTag, 0x1000000000008004ull,
                     "sceGsExecLoadImage should populate the packed A+D GIF tag in guest RAM");
            uint64_t loadReg1 = 0u;
            uint64_t loadReg2 = 0u;
            uint64_t loadReg3 = 0u;
            uint64_t loadReg4 = 0u;
            std::memcpy(&loadReg1, rdram + runtime.guestHeapBase() + 24u, sizeof(loadReg1));
            std::memcpy(&loadReg2, rdram + runtime.guestHeapBase() + 40u, sizeof(loadReg2));
            std::memcpy(&loadReg3, rdram + runtime.guestHeapBase() + 56u, sizeof(loadReg3));
            std::memcpy(&loadReg4, rdram + runtime.guestHeapBase() + 72u, sizeof(loadReg4));
            t.Equals(loadReg1, 0x50ull, "sceGsExecLoadImage should encode BITBLTBUF as A+D register 0x50");
            t.Equals(loadReg2, 0x51ull, "sceGsExecLoadImage should encode TRXPOS as A+D register 0x51");
            t.Equals(loadReg3, 0x52ull, "sceGsExecLoadImage should encode TRXREG as A+D register 0x52");
            t.Equals(loadReg4, 0x53ull, "sceGsExecLoadImage should encode TRXDIR as A+D register 0x53");
            expectGuestHeapReusable(t, runtime,
                                    "sceGsExecLoadImage should free its temporary GIF packet");

            R5900Context storeCtx{};
            setRegU32(storeCtx, 4, kImageAddr);
            setRegU32(storeCtx, 5, kDstAddr);
            ps2_stubs::sceGsExecStoreImage(rdram, &storeCtx, &runtime);
            t.Equals(static_cast<int32_t>(getRegU32Test(storeCtx, 2)), 0,
                     "sceGsExecStoreImage should succeed for a matching CT32 readback");
            uint64_t storeTag = 0u;
            std::memcpy(&storeTag, rdram + runtime.guestHeapBase(), sizeof(storeTag));
            t.Equals(storeTag, 0x1000000000008004ull,
                     "sceGsExecStoreImage should populate the packed A+D GIF tag in guest RAM");
            uint64_t storeReg1 = 0u;
            uint64_t storeReg2 = 0u;
            uint64_t storeReg3 = 0u;
            uint64_t storeReg4 = 0u;
            std::memcpy(&storeReg1, rdram + runtime.guestHeapBase() + 24u, sizeof(storeReg1));
            std::memcpy(&storeReg2, rdram + runtime.guestHeapBase() + 40u, sizeof(storeReg2));
            std::memcpy(&storeReg3, rdram + runtime.guestHeapBase() + 56u, sizeof(storeReg3));
            std::memcpy(&storeReg4, rdram + runtime.guestHeapBase() + 72u, sizeof(storeReg4));
            t.Equals(storeReg1, 0x50ull, "sceGsExecStoreImage should encode BITBLTBUF as A+D register 0x50");
            t.Equals(storeReg2, 0x51ull, "sceGsExecStoreImage should encode TRXPOS as A+D register 0x51");
            t.Equals(storeReg3, 0x52ull, "sceGsExecStoreImage should encode TRXREG as A+D register 0x52");
            t.Equals(storeReg4, 0x53ull, "sceGsExecStoreImage should encode TRXDIR as A+D register 0x53");
            expectGuestHeapReusable(t, runtime,
                                    "sceGsExecStoreImage should free its temporary GIF packet");

            bool roundtripOk = true;
            size_t mismatchIndex = 0u;
            for (size_t i = 0; i < sizeof(pixels); ++i)
            {
                if (rdram[kDstAddr + i] != pixels[i])
                {
                    roundtripOk = false;
                    mismatchIndex = i;
                    break;
                }
            }
            if (!roundtripOk)
            {
                t.Fail("sceGsExecLoadImage/sceGsExecStoreImage should roundtrip CT32 pixel data "
                       "(first mismatch at byte " + std::to_string(mismatchIndex) +
                       ", got " + std::to_string(rdram[kDstAddr + mismatchIndex]) +
                       ", expected " + std::to_string(pixels[mismatchIndex]) + ")");
            }
        });

        tc.Run("sceGsResetGraph frees its temporary GIF packet", [](TestCase &t)
        {
            PS2Runtime runtime;
            t.IsTrue(runtime.memory().initialize(), "runtime memory initialize should succeed");

            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            R5900Context ctx{};
            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 1u);
            setRegU32(ctx, 6, 2u);
            setRegU32(ctx, 7, 1u);
            ps2_stubs::sceGsResetGraph(rdram.data(), &ctx, &runtime);

            t.Equals(static_cast<int32_t>(getRegU32Test(ctx, 2)), 0,
                     "sceGsResetGraph should succeed in reset mode");
            expectGuestHeapReusable(t, runtime,
                                    "sceGsResetGraph should free its temporary GIF packet");
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
