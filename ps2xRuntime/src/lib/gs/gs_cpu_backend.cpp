#include "runtime/gs/gs_cpu_backend.h"
#include "ThreadNaming.h"
#include "runtime/gs/ps2_gs_common.h"
#include "runtime/gs/ps2_gs_psmct16.h"
#include "runtime/gs/ps2_gs_psmct32.h"
#include "runtime/gs/ps2_gs_psmt4.h"
#include "runtime/gs/ps2_gs_psmt8.h"
#include "runtime/gs/ps2_gs_memory.h"
#include "runtime/ps2_memory.h"
#include "ps2_log.h"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <string>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#include <condition_variable>
#include <functional>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace GSInternal;

namespace
{
    // PS2X_GS_STATS=1 prints where GS time goes every 5 s. Rasterizer bands add their counters once per
    // pass, so leaving this compiled in costs a predictable branch per primitive.
    bool gsStatsEnabled()
    {
        static const bool enabled = std::getenv("PS2X_GS_STATS") != nullptr;
        return enabled;
    }

    enum GSTimeCategory : uint32_t
    {
        kTimeTracked,
        kTimeUntracked,
        kTimeSerialBatch,
        kTimeAlone,
        kTimeClut,
        kTimeTransfer,
        kTimeUpload,
        kTimeCapture,
        kTimeClear,
        kTimeSubmitOther,
        kTimeOther,
        kTimeIdle,
        kTimeCount,
    };
    const char *const kTimeNames[kTimeCount] = {"tracked", "untracked", "serialBatch", "alone", "clut", "transfer",
                                                "upload", "capture", "clear", "submitOther", "other", "idle"};
    const char *const kFlushNames[] = {"full", "targetChange", "targetOverlap", "readsPendingWrites", "writesPendingReads",
                                       "trackMismatch", "drawAlone", "clut", "transfer", "textureFlush", "capture", "idle",
                                       "readback", "other"};

    struct GSStatCounters
    {
        std::atomic<uint64_t> prims[8]{};
        std::atomic<uint64_t> texturedPrims{};
        std::atomic<uint64_t> batches[3]{};      // tracked, untracked parallel, serial
        std::atomic<uint64_t> batchPrims[3]{};
        std::atomic<uint64_t> batchPixels[3]{};
        std::atomic<uint64_t> alonePrims{};
        std::atomic<uint64_t> trackStart[2]{}; // a primitive sampling its own target, or a page queued ones draw
        std::atomic<uint32_t> trackSample[7]{};
        std::atomic<uint64_t> flushes[16]{};
        std::atomic<uint64_t> undoEntries{};
        std::atomic<uint64_t> boxPixels{};
        std::atomic<uint64_t> insidePixels{};
        std::atomic<uint64_t> writtenPixels{};
        std::atomic<uint64_t> texSamples{};
        std::atomic<uint64_t> bigBoxPixels{};
        std::atomic<uint64_t> bigInsidePixels{};
        std::atomic<uint64_t> timeNs[kTimeCount]{};
        std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    };

    GSStatCounters &gsStats()
    {
        static GSStatCounters counters;
        return counters;
    }

    uint64_t take(std::atomic<uint64_t> &counter)
    {
        return counter.exchange(0u, std::memory_order_relaxed);
    }

    void printGsStatsIfDue()
    {
        GSStatCounters &s = gsStats();
        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now - s.last).count();
        if (seconds < 5.0)
            return;
        s.last = now;
        const double perSec = 1.0 / seconds;
        std::fprintf(stderr, "[gsstats] prims/s point=%.0f line=%.0f tri=%.0f tristrip=%.0f trifan=%.0f sprite=%.0f textured=%.0f alone=%.0f\n",
                     take(s.prims[0]) * perSec, (take(s.prims[1]) + take(s.prims[2])) * perSec, take(s.prims[3]) * perSec,
                     take(s.prims[4]) * perSec, take(s.prims[5]) * perSec, take(s.prims[6]) * perSec, take(s.texturedPrims) * perSec,
                     take(s.alonePrims) * perSec);
        const char *batchNames[3] = {"tracked", "untracked", "serial"};
        for (int i = 0; i < 3; ++i)
        {
            const uint64_t batches = take(s.batches[i]);
            const uint64_t prims = take(s.batchPrims[i]);
            const uint64_t pixels = take(s.batchPixels[i]);
            std::fprintf(stderr, "[gsstats] batches %s/s=%.1f prims/batch=%.1f boxpx/batch=%.0f\n", batchNames[i], batches * perSec,
                         batches ? double(prims) / batches : 0.0, batches ? double(pixels) / batches : 0.0);
        }
        std::string flushes;
        for (size_t i = 0; i < sizeof(kFlushNames) / sizeof(kFlushNames[0]); ++i)
        {
            const uint64_t count = take(s.flushes[i]);
            if (count)
                flushes += " " + std::string(kFlushNames[i]) + "=" + std::to_string(static_cast<uint64_t>(count * perSec));
        }
        std::fprintf(stderr, "[gsstats] flushes/s%s\n", flushes.c_str());
        std::fprintf(stderr, "[gsstats] tracked starts/s self=%.1f cross=%.1f; last: fbp=%u fpsm=0x%x zbp=%u zpsm=0x%x tbp0=%u tpsm=0x%x prim=%u\n",
                     take(s.trackStart[0]) * perSec, take(s.trackStart[1]) * perSec, s.trackSample[0].load(), s.trackSample[1].load(),
                     s.trackSample[2].load(), s.trackSample[3].load(), s.trackSample[4].load(), s.trackSample[5].load(), s.trackSample[6].load());
        std::fprintf(stderr, "[gsstats] Mpx/s box=%.2f inside=%.2f written=%.2f texsamples=%.2f bigbox=%.2f biginside=%.2f undo=%.2f\n",
                     take(s.boxPixels) * perSec * 1e-6, take(s.insidePixels) * perSec * 1e-6, take(s.writtenPixels) * perSec * 1e-6,
                     take(s.texSamples) * perSec * 1e-6, take(s.bigBoxPixels) * perSec * 1e-6, take(s.bigInsidePixels) * perSec * 1e-6,
                     take(s.undoEntries) * perSec * 1e-6);
        std::string times;
        for (uint32_t i = 0; i < kTimeCount; ++i)
        {
            char item[64];
            std::snprintf(item, sizeof(item), " %s=%.1f%%", kTimeNames[i], take(s.timeNs[i]) * 1e-9 * perSec * 100.0);
            times += item;
        }
        std::fprintf(stderr, "[gsstats] gs-thread time%s\n", times.c_str());
    }

    // Adds wall time to one category while in scope, excluding nested timers so categories sum to the total.
    thread_local uint64_t t_nestedNs = 0u;
    class GSStatTimer
    {
    public:
        explicit GSStatTimer(uint32_t category)
            : m_category(category), m_enabled(gsStatsEnabled())
        {
            if (!m_enabled)
                return;
            m_outerNested = t_nestedNs;
            t_nestedNs = 0u;
            m_start = std::chrono::steady_clock::now();
        }
        ~GSStatTimer()
        {
            if (!m_enabled)
                return;
            const uint64_t elapsed = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - m_start).count());
            gsStats().timeNs[m_category].fetch_add(elapsed - std::min(elapsed, t_nestedNs), std::memory_order_relaxed);
            t_nestedNs = m_outerNested + elapsed;
        }

    private:
        uint32_t m_category;
        bool m_enabled;
        uint64_t m_outerNested = 0u;
        std::chrono::steady_clock::time_point m_start;
    };

    float fabsQ(float q)
    {
        return (std::fabs(q) > 1.0e-8f) ? q : 1.0f;
    }

    u16 Rgba8888ToRgba5551(u32 c)
    {
        uint32_t r = ((c >> 0) & 0xFF) >> 3;
        uint32_t g = ((c >> 8) & 0xFF) >> 3;
        uint32_t b = ((c >> 16) & 0xFF) >> 3;
        uint32_t a = ((c >> 24) & 0xFF) >> 7;

        return (r | (g << 5) | (b << 10) | (a << 15));
    }

    u32 Rgba5551ToRgba8888(u16 c)
    {
        u32 r = ((c >> 0) & 0x1F) << 3;
        u32 g = ((c >> 5) & 0x1F) << 3;
        u32 b = ((c >> 10) & 0x1F) << 3;
        u32 a = ((c >> 15) & 0x01) << 7;

        return (r | (g << 8) | (b << 16) | (a << 24));
    }

    u32 pack32(u8 r, u8 g, u8 b, u8 a)
    {
        return static_cast<u32>(r) | (g << 8) | (b << 16) | (a << 24);
    }

    uint32_t applyTexa(const GSTexaReg &texa, uint8_t psm, uint32_t texel)
    {
        if (psm == GS_PSM_CT32)
            return texel;

        const uint8_t r = static_cast<uint8_t>(texel & 0xFFu);
        const uint8_t g = static_cast<uint8_t>((texel >> 8) & 0xFFu);
        const uint8_t b = static_cast<uint8_t>((texel >> 16) & 0xFFu);
        const bool rgbZero = r == 0u && g == 0u && b == 0u;
        uint8_t a = static_cast<uint8_t>((texel >> 24) & 0xFFu);

        switch (psm)
        {
        case GS_PSM_CT24:
            a = (texa.aem && rgbZero) ? 0u : texa.ta0;
            break;
        case GS_PSM_CT16:
        case GS_PSM_CT16S:
            if ((a & 0x80u) != 0u)
                a = texa.ta1;
            else
                a = (texa.aem && rgbZero) ? 0u : texa.ta0;
            break;
        default:
            break;
        }

        return (texel & 0x00FFFFFFu) | (static_cast<uint32_t>(a) << 24);
    }

    uint32_t addrPSMCT16Family(uint32_t basePtr, uint32_t width, uint8_t psm, uint32_t x, uint32_t y)
    {
        switch (psm)
        {
        case GS_PSM_CT16:
            return GSPSMCT16::addrPSMCT16(basePtr, width, x, y);
        case GS_PSM_CT16S:
            return GSPSMCT16::addrPSMCT16S(basePtr, width, x, y);
        case GS_PSM_Z16:
            return GSPSMCT16::addrPSMZ16(basePtr, width, x, y);
        case GS_PSM_Z16S:
            return GSPSMCT16::addrPSMZ16S(basePtr, width, x, y);
        default:
            return 0u;
        }
    }


    // Conservative VRAM page sets, used to decide when the texture page cache must snapshot a page.
    constexpr uint32_t kVramPageCount = GSMem::MEMORY_SIZE / GSMem::TexturePageCache::kPageSize;

    struct PageSpan
    {
        uint32_t first = 0;
        uint32_t count = 0;
    };

    void psmPageShift(uint32_t psm, uint32_t &widthShift, uint32_t &heightShift)
    {
        switch (psm)
        {
        case GS_PSM_CT16:
        case GS_PSM_CT16S:
        case GS_PSM_Z16:
        case GS_PSM_Z16S:
            widthShift = 6u;
            heightShift = 6u;
            break;
        case GS_PSM_T8:
            widthShift = 7u;
            heightShift = 6u;
            break;
        case GS_PSM_T4:
            widthShift = 7u;
            heightShift = 7u;
            break;
        default:
            widthShift = 6u;
            heightShift = 5u;
            break;
        }
    }

    // Pages a buffer can touch for pixels in [x, maxX] x [y, maxY]. A base block that is not page aligned
    // spills its swizzled blocks into the following page, so that page is included too.
    PageSpan pageSpan(uint32_t baseBlock, uint32_t bw, uint32_t psm, uint32_t x, uint32_t y, uint32_t maxX, uint32_t maxY)
    {
        uint32_t widthShift = 6u;
        uint32_t heightShift = 5u;
        psmPageShift(psm, widthShift, heightShift);
        const uint64_t stride = (bw * 64u) >> widthShift;
        const uint64_t first = baseBlock / 32u + (y >> heightShift) * stride + (x >> widthShift);
        const uint64_t last = baseBlock / 32u + (maxY >> heightShift) * stride + (maxX >> widthShift) + ((baseBlock % 32u) != 0u ? 1u : 0u);
        PageSpan span{};
        span.first = static_cast<uint32_t>(first % kVramPageCount);
        span.count = static_cast<uint32_t>(std::min<uint64_t>(last - first + 1u, kVramPageCount));
        return span;
    }

    bool spanHasPage(const PageSpan &span, uint32_t page)
    {
        return ((page + kVramPageCount - span.first) % kVramPageCount) < span.count;
    }

    bool spansOverlap(const PageSpan &a, const PageSpan &b)
    {
        if (a.count == 0u || b.count == 0u)
            return false;
        return a.count + b.count > kVramPageCount || spanHasPage(a, b.first) || spanHasPage(b, a.first);
    }


    // Palette versions are unique across backends: expanded palettes are cached per thread, and a new
    // backend can reuse a destroyed one's address.
    uint64_t nextClutGeneration()
    {
        static std::atomic<uint64_t> generation{0};
        return generation.fetch_add(1u, std::memory_order_relaxed) + 1u;
    }

    std::atomic<uint32_t> s_debugPrimitiveCount{0};
    std::atomic<uint32_t> s_debugPixelCount{0};
    std::atomic<uint32_t> s_debugContext1PrimitiveCount{0};
    std::atomic<uint32_t> s_debugFbp150PixelCount{0};

    int wrapTextureCoordinate(int coordinate,
                              int textureSize,
                              uint8_t mode,
                              uint16_t regionMin,
                              uint16_t regionMax)
    {
        switch (mode & 0x3u)
        {
        case 0: // REPEAT
            return static_cast<int>(static_cast<uint32_t>(coordinate) & static_cast<uint32_t>(textureSize - 1));
        case 1: // CLAMP
            return clampInt(coordinate, 0, textureSize - 1);
        case 2: // REGION_CLAMP
            return std::min(std::max(coordinate, static_cast<int>(regionMin)), static_cast<int>(regionMax));
        case 3: // REGION_REPEAT
            return static_cast<int>((static_cast<uint32_t>(coordinate) & static_cast<uint32_t>(regionMin)) | static_cast<uint32_t>(regionMax));
        default:
            return coordinate;
        }
    }

    bool passesAlphaTest(uint64_t testReg, uint8_t alpha)
    {
        if ((testReg & 0x1u) == 0u)
            return true;

        const uint8_t atst = static_cast<uint8_t>((testReg >> 1) & 0x7u);
        const uint8_t aref = static_cast<uint8_t>((testReg >> 4) & 0xFFu);

        switch (atst)
        {
        case 0:
            return false;
        case 1:
            return true;
        case 2:
            return alpha < aref;
        case 3:
            return alpha <= aref;
        case 4:
            return alpha == aref;
        case 5:
            return alpha >= aref;
        case 6:
            return alpha > aref;
        case 7:
            return alpha != aref;
        default:
            return true;
        }
    }

    struct PixelWriteMask
    {
        bool writeRgb = true;
        bool writeAlpha = true;
        bool writeDepth = true;

        bool writesFramebuffer() const
        {
            return writeRgb || writeAlpha;
        }

        bool writesAnything() const
        {
            return writesFramebuffer() || writeDepth;
        }
    };

    PixelWriteMask classifyAlphaTest(uint64_t testReg, uint8_t alpha, uint8_t framePsm)
    {
        const bool pass = passesAlphaTest(testReg, alpha);
        if (pass)
            return {};

        // TEST.AFAIL controls what happens when the alpha comparison fails.
        switch (static_cast<uint8_t>((testReg >> 12) & 0x3u))
        {
        case 1: // FB_ONLY
            return {true, true, false};
        case 2: // ZB_ONLY
            return {false, false, true};
        case 3: // RGB_ONLY
            // RGB_ONLY is only distinct for RGBA32. The GS treats it as
            // FB_ONLY for RGB24 and RGBA16 framebuffers.
            if (framePsm == GS_PSM_CT32)
                return {true, false, false};
            return {true, true, false};
        case 0: // KEEP
        default:
            return {false, false, false};
        }
    }

    bool passesDestinationAlphaTest(uint64_t testReg, uint8_t framePsm, uint32_t rawFramebufferPixel)
    {
        const bool date = ((testReg >> 14) & 0x1u) != 0u;
        if (!date)
            return true;

        const bool datm = ((testReg >> 15) & 0x1u) != 0u;
        switch (framePsm)
        {
        case GS_PSM_CT32:
            return (((rawFramebufferPixel >> 31) & 0x1u) != 0u) == datm;
        case GS_PSM_CT16:
        case GS_PSM_CT16S:
            return (((rawFramebufferPixel >> 15) & 0x1u) != 0u) == datm;
        case GS_PSM_CT24:
            // RGB24 has no destination alpha, so DATE always passes.
            return true;
        default:
            return true;
        }
    }

    struct TextureCombineResult
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
    };

    TextureCombineResult combineTexture(const GSTex0Reg &tex,
                                        uint8_t vr,
                                        uint8_t vg,
                                        uint8_t vb,
                                        uint8_t va,
                                        uint8_t tr,
                                        uint8_t tg,
                                        uint8_t tb,
                                        uint8_t ta)
    {
        const bool textureHasAlpha = tex.tcc != 0u;
        TextureCombineResult out{tr, tg, tb, textureHasAlpha ? ta : va};

        switch (tex.tfx)
        {
        case 0: // MODULATE
            out.r = clampU8((tr * vr) >> 7);
            out.g = clampU8((tg * vg) >> 7);
            out.b = clampU8((tb * vb) >> 7);
            out.a = textureHasAlpha ? clampU8((ta * va) >> 7) : va;
            break;
        case 1: // DECAL
            out.r = tr;
            out.g = tg;
            out.b = tb;
            out.a = textureHasAlpha ? ta : va;
            break;
        case 2: // HIGHLIGHT
            out.r = clampU8(((tr * vr) >> 7) + va);
            out.g = clampU8(((tg * vg) >> 7) + va);
            out.b = clampU8(((tb * vb) >> 7) + va);
            out.a = textureHasAlpha ? clampU8(ta + va) : va;
            break;
        case 3: // HIGHLIGHT2
            out.r = clampU8(((tr * vr) >> 7) + va);
            out.g = clampU8(((tg * vg) >> 7) + va);
            out.b = clampU8(((tb * vb) >> 7) + va);
            out.a = textureHasAlpha ? ta : va;
            break;
        default:
            out.r = tr;
            out.g = tg;
            out.b = tb;
            out.a = textureHasAlpha ? ta : va;
            break;
        }

        return out;
    }

    uint32_t swizzleClutIndexCSM1(uint32_t index)
    {
        // CSM1 swaps address bits 3 and 4. Preserve the remaining bits:
        // 16-bit CLUTs expose a ninth address bit through CSA[4].
        return (index & ~0x18u) | ((index & 0x08u) << 1u) | ((index & 0x10u) >> 1u);
    }

    bool isFourBitIndexedPsm(uint8_t psm)
    {
        return psm == GS_PSM_T4 || psm == GS_PSM_T4HL || psm == GS_PSM_T4HH;
    }

    bool isEightBitIndexedPsm(uint8_t psm)
    {
        return psm == GS_PSM_T8 || psm == GS_PSM_T8H;
    }

    uint8_t lerpChannel(uint8_t c00, uint8_t c10, uint8_t c01, uint8_t c11, float fx, float fy)
    {
        const float top = static_cast<float>(c00) + (static_cast<float>(c10) - static_cast<float>(c00)) * fx;
        const float bottom = static_cast<float>(c01) + (static_cast<float>(c11) - static_cast<float>(c01)) * fx;
        return clampU8(static_cast<int>(std::lround(top + (bottom - top) * fy)));
    }

    // lerpChannel for all four channels at once. Only for weights in [0, 1], where every intermediate stays
    // within the channels' range and the vector rounding (fused multiply-adds, then round half away from
    // zero) gives what the scalar code computes.
    uint32_t lerpTexels(uint32_t c00, uint32_t c10, uint32_t c01, uint32_t c11, float fx, float fy)
    {
#if defined(__ARM_NEON)
        auto channels = [](uint32_t texel)
        {
            const uint8x8_t bytes = vreinterpret_u8_u32(vdup_n_u32(texel));
            return vcvtq_f32_u32(vmovl_u16(vget_low_u16(vmovl_u8(bytes))));
        };
        const float32x4_t f00 = channels(c00);
        const float32x4_t f10 = channels(c10);
        const float32x4_t f01 = channels(c01);
        const float32x4_t f11 = channels(c11);
        const float32x4_t top = vfmaq_n_f32(f00, vsubq_f32(f10, f00), fx);
        const float32x4_t bottom = vfmaq_n_f32(f01, vsubq_f32(f11, f01), fx);
        const uint32x4_t rounded = vreinterpretq_u32_s32(vcvtaq_s32_f32(vfmaq_n_f32(top, vsubq_f32(bottom, top), fy)));
        const uint8x8_t packed = vmovn_u16(vcombine_u16(vmovn_u32(rounded), vdup_n_u16(0)));
        return vget_lane_u32(vreinterpret_u32_u8(packed), 0);
#else
        uint32_t out = 0u;
        for (uint32_t shift = 0; shift < 32u; shift += 8u)
            out |= static_cast<uint32_t>(lerpChannel(static_cast<uint8_t>(c00 >> shift), static_cast<uint8_t>(c10 >> shift),
                                                     static_cast<uint8_t>(c01 >> shift), static_cast<uint8_t>(c11 >> shift), fx, fy))
                   << shift;
        return out;
#endif
    }
}

namespace
{
    static constexpr uint32_t kDefaultDisplayWidth = 640u;
    static constexpr uint32_t kDefaultDisplayHeight = 448u;
    static constexpr uint32_t kHostFrameWidth = 640u;
    static constexpr uint32_t kHostFrameHeight = 512u;

    uint16_t encodeFramePixelPSMCT16(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        return static_cast<uint16_t>(((r >> 3) & 0x1Fu) |
                                     (((g >> 3) & 0x1Fu) << 5) |
                                     (((b >> 3) & 0x1Fu) << 10) |
                                     ((a >= 0x40u) ? 0x8000u : 0u));
    }

    void decodeDisplaySize(uint64_t display64, uint32_t &outWidth, uint32_t &outHeight)
    {
        const uint32_t dw = static_cast<uint32_t>((display64 >> 32) & 0x0FFFu);
        const uint32_t dh = static_cast<uint32_t>((display64 >> 44) & 0x07FFu);
        const uint32_t magh = static_cast<uint32_t>((display64 >> 23) & 0x0Fu);

        outWidth = (dw + 1u) / (magh + 1u);
        outHeight = dh + 1u;
        if (outWidth < 64u || outHeight < 64u)
        {
            outWidth = kDefaultDisplayWidth;
            outHeight = kDefaultDisplayHeight;
        }
        outWidth = std::min<uint32_t>(outWidth, kHostFrameWidth);
        outHeight = std::min<uint32_t>(outHeight, kHostFrameHeight);
    }

    GSFrameReg decodeDisplayFrame(uint64_t dispfb64)
    {
        GSFrameReg frame{};
        frame.fbp = static_cast<uint32_t>(dispfb64 & 0x1FFu);
        frame.fbw = static_cast<uint32_t>((dispfb64 >> 9) & 0x3Fu);
        frame.psm = static_cast<uint8_t>((dispfb64 >> 15) & 0x1Fu);
        return frame;
    }

    struct GSDisplayReadOrigin
    {
        uint32_t x = 0u;
        uint32_t y = 0u;
    };

    GSDisplayReadOrigin decodeDisplayReadOrigin(uint64_t dispfb64)
    {
        return {
            static_cast<uint32_t>((dispfb64 >> 32) & 0x7FFu),
            static_cast<uint32_t>((dispfb64 >> 43) & 0x7FFu)};
    }

    bool hasDisplaySetup(uint64_t display64, const GSFrameReg &frame)
    {
        const uint32_t dw = static_cast<uint32_t>((display64 >> 32) & 0x0FFFu);
        const uint32_t dh = static_cast<uint32_t>((display64 >> 44) & 0x07FFu);
        const uint32_t magh = static_cast<uint32_t>((display64 >> 23) & 0x0Fu);
        return frame.fbw != 0u || dw != 0u || dh != 0u || magh != 0u;
    }

    struct GSPmodeState
    {
        bool enableCrt1 = false;
        bool enableCrt2 = false;
        bool mmod = false;
        bool amod = false;
        bool slbg = false;
        uint8_t alp = 0u;
    };

    GSPmodeState decodePmode(uint64_t pmode64)
    {
        return {
            (pmode64 & 0x1ull) != 0ull,
            (pmode64 & 0x2ull) != 0ull,
            ((pmode64 >> 5) & 0x1ull) != 0ull,
            ((pmode64 >> 6) & 0x1ull) != 0ull,
            ((pmode64 >> 7) & 0x1ull) != 0ull,
            static_cast<uint8_t>((pmode64 >> 8) & 0xFFu)};
    }

    struct GSSmode2State
    {
        bool interlaced = false;
        bool frameMode = true;
    };

    GSSmode2State decodeSMode2(uint64_t smode2)
    {
        return {(smode2 & 0x1ull) != 0ull, ((smode2 >> 1) & 0x1ull) != 0ull};
    }

    void applyFieldPresentation(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height, bool oddField)
    {
        if (pixels.empty() || width == 0u || height < 2u)
            return;
        const std::vector<uint8_t> source = pixels;
        for (uint32_t y = 0; y < height; ++y)
        {
            uint32_t sourceY = ((y >> 1u) << 1u) + (oddField ? 1u : 0u);
            if (sourceY >= height)
                sourceY = height - 1u;
            std::memcpy(pixels.data() + y * kHostFrameWidth * 4u,
                        source.data() + sourceY * kHostFrameWidth * 4u,
                        width * 4u);
        }
    }

    void normalizePresentationAlpha(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height)
    {
        for (uint32_t y = 0; y < height; ++y)
        {
            uint8_t *row = pixels.data() + y * kHostFrameWidth * 4u;
            for (uint32_t x = 0; x < width; ++x)
                row[x * 4u + 3u] = 255u;
        }
    }

    uint8_t blendPresentationChannel(uint8_t src, uint8_t dst, uint32_t factor)
    {
        const int delta = static_cast<int>(src) - static_cast<int>(dst);
        return GSInternal::clampU8(static_cast<int>(dst) + ((delta * static_cast<int>(factor)) / 255));
    }

    uint32_t countNonBlackPixels(const std::vector<uint8_t> &pixels, uint32_t width, uint32_t height)
    {
        uint32_t count = 0u;
        for (uint32_t y = 0; y < height; ++y)
        {
            const uint8_t *row = pixels.data() + y * kHostFrameWidth * 4u;
            for (uint32_t x = 0; x < width; ++x)
            {
                if (row[x * 4u] != 0u || row[x * 4u + 1u] != 0u || row[x * 4u + 2u] != 0u)
                    ++count;
            }
        }
        return count;
    }
}

GSCpuBackend::GSCpuBackend()
{
    using namespace GSMem;
    static std::once_flag lookupTablesOnce;
    std::call_once(lookupTablesOnce, []()
                   { InitLookupTables(); });
    for (size_t i = 0; i < kPsmHandlerCount; ++i)
    {
        switch (i)
        {
        case GS_PSM_CT32:
            m_readVramFuncs[i] = ReadCT32;
            m_writeVramFuncs[i] = WriteCT32;
            break;
        case GS_PSM_CT24:
            m_readVramFuncs[i] = ReadCT24;
            m_writeVramFuncs[i] = WriteCT24;
            break;
        case GS_PSM_CT16:
            m_readVramFuncs[i] = ReadCT16;
            m_writeVramFuncs[i] = WriteCT16;
            break;
        case GS_PSM_CT16S:
            m_readVramFuncs[i] = ReadCT16S;
            m_writeVramFuncs[i] = WriteCT16S;
            break;
        case GS_PSM_T8:
            m_readVramFuncs[i] = ReadP8;
            m_writeVramFuncs[i] = WriteP8;
            break;
        case GS_PSM_T8H:
            m_readVramFuncs[i] = ReadP8H;
            m_writeVramFuncs[i] = WriteP8H;
            break;
        case GS_PSM_T4:
            m_readVramFuncs[i] = ReadP4;
            m_writeVramFuncs[i] = WriteP4;
            break;
        case GS_PSM_T4HH:
            m_readVramFuncs[i] = ReadP4HH;
            m_writeVramFuncs[i] = WriteP4HH;
            break;
        case GS_PSM_T4HL:
            m_readVramFuncs[i] = ReadP4HL;
            m_writeVramFuncs[i] = WriteP4HL;
            break;
        case GS_PSM_Z32:
            m_readVramFuncs[i] = ReadZ32;
            m_writeVramFuncs[i] = WriteZ32;
            break;
        case GS_PSM_Z24:
            m_readVramFuncs[i] = ReadZ24;
            m_writeVramFuncs[i] = WriteZ24;
            break;
        case GS_PSM_Z16:
            m_readVramFuncs[i] = ReadZ16;
            m_writeVramFuncs[i] = WriteZ16;
            break;
        case GS_PSM_Z16S:
            m_readVramFuncs[i] = ReadZ16S;
            m_writeVramFuncs[i] = WriteZ16S;
            break;
        default:
            m_readVramFuncs[i] = ReadNull;
            m_writeVramFuncs[i] = WriteNull;
            break;
        }
    }
    Reset();
}



class GSCpuBackend::RasterWorkers
{
public:
    explicit RasterWorkers(uint32_t participants)
        : m_participants(participants)
    {
        for (uint32_t index = 1; index < participants; ++index)
            m_threads.emplace_back([this, index]() { loop(index); });
    }

    ~RasterWorkers()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_start.notify_all();
        for (std::thread &thread : m_threads)
            thread.join();
    }

    uint32_t participants() const { return m_participants; }

    // Runs band(i) for i in [0, bands), each once, with every participant taking the next band as it
    // finishes one, so a band that was estimated low does not leave the others waiting.
    void runBands(uint32_t bands, const std::function<void(uint32_t)> &band)
    {
        std::atomic<uint32_t> next{0u};
        const std::function<void(uint32_t)> job = [&](uint32_t)
        {
            for (uint32_t i = next.fetch_add(1u, std::memory_order_relaxed); i < bands; i = next.fetch_add(1u, std::memory_order_relaxed))
                band(i);
        };
        run(job);
    }

    // Runs job(i) for every participant; the caller is participant 0.
    void run(const std::function<void(uint32_t)> &job)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_job = &job;
            m_remaining = m_participants - 1u;
            ++m_generation;
        }
        m_start.notify_all();
        job(0u);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_done.wait(lock, [this]() { return m_remaining == 0u; });
        m_job = nullptr;
    }

private:
    void loop(uint32_t index)
    {
        uint64_t seen = 0;
        for (;;)
        {
            const std::function<void(uint32_t)> *job = nullptr;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_start.wait(lock, [&]() { return m_stop || m_generation != seen; });
                if (m_stop)
                    return;
                seen = m_generation;
                job = m_job;
            }
            (*job)(index);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (--m_remaining == 0u)
                    m_done.notify_one();
            }
        }
    }

    uint32_t m_participants = 1;
    std::vector<std::thread> m_threads;
    std::mutex m_mutex;
    std::condition_variable m_start;
    std::condition_variable m_done;
    const std::function<void(uint32_t)> *m_job = nullptr;
    uint64_t m_generation = 0;
    uint32_t m_remaining = 0;
    bool m_stop = false;
};

namespace
{
    uint32_t defaultRasterParticipants()
    {
        static const uint32_t participants = []()
        {
            if (const char *value = std::getenv("PS2X_GS_THREADS"))
                return static_cast<uint32_t>(std::clamp(std::atoi(value), 1, 16));
            // The EE game thread and the host window each need a core; 3D scenes are fill-rate bound and
            // keep scaling with every raster thread up to that (20 -> 27 vblanks/s on 8 cores going 4 -> 6).
            const uint32_t cores = std::max(1u, std::thread::hardware_concurrency());
            return std::clamp(cores > 2u ? cores - 2u : 1u, 1u, 8u);
        }();
        return participants;
    }

    constexpr size_t kMaxPendingPrimitives = 8192u;
    // Row ranges per participant: pixel estimates are only bounding boxes, and a participant can run on a
    // slow core or be preempted, so ranges down to single 8-row groups taken on demand keep every participant
    // busy until the batch ends (4 per participant left workers 16% idle in 3D scenes).
    constexpr uint32_t kBandsPerWorker = 32u;
    constexpr uint64_t kMaxPendingPixels = 1u << 22u;
    // Below this, waking the workers costs more than drawing the batch on the calling thread.
    constexpr uint64_t kMinParallelPixels = 16384u;

    // T8H/T4HL/T4HH texels are the top byte of 32-bit words, which RGB24/Z24 writes never change, so such a
    // texture can share pages with a 24-bit colour or depth buffer without depending on what is drawn.
    bool readsTopByteOnly(uint32_t psm)
    {
        return psm == GS_PSM_T8H || psm == GS_PSM_T4HL || psm == GS_PSM_T4HH;
    }

    bool keepsTopByte(uint32_t psm)
    {
        return psm == GS_PSM_CT24 || psm == GS_PSM_Z24;
    }

    // The pages of a span as a mask. Queueing tests and marks several spans of a whole frame per primitive,
    // which bit by bit cost the GS thread more than 5% of its time in 3D scenes.
    std::bitset<512> spanMask(const PageSpan &span)
    {
        std::bitset<512> mask;
        if (span.count == 0u)
            return mask;
        if (span.count >= 512u)
            return mask.set();
        mask.set();
        mask >>= 512u - span.count;
        if (span.first + span.count <= 512u)
            return mask << span.first;
        return (mask << span.first) | (mask >> (512u - span.first));
    }

    void markPages(std::bitset<512> &pages, const PageSpan &span)
    {
        pages |= spanMask(span);
    }

    bool anyPage(const std::bitset<512> &pages, const PageSpan &span)
    {
        return (pages & spanMask(span)).any();
    }

    struct PixelBounds
    {
        int x0 = 0;
        int y0 = 0;
        int x1 = -1;
        int y1 = -1;

        bool empty() const { return x1 < x0 || y1 < y0; }
        uint64_t area() const { return empty() ? 0u : static_cast<uint64_t>(x1 - x0 + 1) * static_cast<uint64_t>(y1 - y0 + 1); }
    };

    // Pixels a primitive can draw: its vertex box (rounded outwards, as every rasterizer stays inside it)
    // within the scissor.
    PixelBounds primitiveBounds(const GSPrimitiveBatch &batch)
    {
        const auto &ctx = batch.state.context;
        float minX = batch.vertices[0].x, maxX = minX, minY = batch.vertices[0].y, maxY = minY;
        for (uint32_t i = 1; i < std::min<uint32_t>(batch.vertexCount, 3u); ++i)
        {
            minX = std::min(minX, batch.vertices[i].x);
            maxX = std::max(maxX, batch.vertices[i].x);
            minY = std::min(minY, batch.vertices[i].y);
            maxY = std::max(maxY, batch.vertices[i].y);
        }
        auto toPixel = [](double value, int offset, int lo, int hi)
        {
            const double pixel = value - static_cast<double>(offset);
            if (!(pixel >= static_cast<double>(lo)))
                return lo;
            if (pixel > static_cast<double>(hi))
                return hi;
            return static_cast<int>(pixel);
        };
        const int ofx = static_cast<int>(ctx.xyoffset.ofx >> 4);
        const int ofy = static_cast<int>(ctx.xyoffset.ofy >> 4);
        const int sx0 = static_cast<int>(ctx.scissor.x0), sx1 = static_cast<int>(ctx.scissor.x1);
        const int sy0 = static_cast<int>(ctx.scissor.y0), sy1 = static_cast<int>(ctx.scissor.y1);
        PixelBounds bounds;
        if (std::isnan(minX) || std::isnan(maxX) || std::isnan(minY) || std::isnan(maxY))
            return {sx0, sy0, sx1, sy1};
        if (maxX - ofx < sx0 || minX - ofx > sx1 + 1 || maxY - ofy < sy0 || minY - ofy > sy1 + 1)
            return bounds;
        bounds.x0 = toPixel(std::floor(static_cast<double>(minX)), ofx, sx0, sx1);
        bounds.x1 = toPixel(std::ceil(static_cast<double>(maxX)), ofx, sx0, sx1);
        bounds.y0 = toPixel(std::floor(static_cast<double>(minY)), ofy, sy0, sy1);
        bounds.y1 = toPixel(std::ceil(static_cast<double>(maxY)), ofy, sy0, sy1);
        return bounds;
    }

    // Each row also costs some setup besides its pixels.
    constexpr uint64_t kRowCost = 8u;

    void addRowWeights(std::array<uint64_t, 256> &weights, const PixelBounds &bounds)
    {
        if (bounds.empty())
            return;
        const uint64_t width = static_cast<uint64_t>(bounds.x1 - bounds.x0 + 1) + kRowCost;
        for (int group = bounds.y0 >> 3; group <= (bounds.y1 >> 3) && group < 256; ++group)
        {
            const int rows = std::min(bounds.y1, group * 8 + 7) - std::max(bounds.y0, group * 8) + 1;
            weights[static_cast<size_t>(group)] += width * static_cast<uint64_t>(rows);
        }
    }
}

void GSCpuBackend::BalanceRows(const RowWeights &weights, uint32_t participants, std::vector<RowRange> &ranges)
{
    // Contiguous ranges keep each band's writes together; cuts fall between 8-row groups, which no
    // swizzled block straddles.
    ranges.assign(participants, RowRange{});
    uint64_t total = 0u;
    for (uint64_t weight : weights)
        total += weight;
    uint64_t sum = 0u;
    uint32_t band = 0u;
    int begin = std::numeric_limits<int>::min();
    for (uint32_t group = 0; group < kRowGroups && band + 1u < participants; ++group)
    {
        sum += weights[group];
        while (band + 1u < participants && sum * participants >= total * (band + 1u))
        {
            const int end = static_cast<int>((group + 1u) * 8u);
            ranges[band] = {begin, end};
            begin = end;
            ++band;
        }
    }
    ranges[band] = {begin, std::numeric_limits<int>::max()};
}

GSCpuBackend::~GSCpuBackend()
{
    SetAsync(false);
}

void GSCpuBackend::SetRasterThreads(uint32_t participants)
{
    WaitForQueue();
    std::lock_guard<std::mutex> lock(m_mutex);
    FlushPendingUnlocked(FlushReason::Other);
    m_rasterParticipants = participants;
}

uint32_t GSCpuBackend::RasterParticipants() const
{
    return m_rasterParticipants != 0u ? m_rasterParticipants : defaultRasterParticipants();
}

void GSCpuBackend::QueuePrimitive(const GSPrimitiveBatch &batch)
{
    const GSDrawState &state = batch.state;
    const auto &ctx = state.context;
    const uint32_t maxX = ctx.scissor.x1;
    const uint32_t maxY = ctx.scissor.y1;
    const uint32_t fbw = std::max<u32>(ctx.frame.fbw, 1u);
    const PageSpan frame = pageSpan(GSInternal::framePageBaseToBlock(ctx.frame.fbp), fbw, ctx.frame.psm, 0u, 0u, maxX, maxY);
    const PageSpan depth = pageSpan(GSInternal::framePageBaseToBlock(ctx.zbuf.zbp), fbw, ctx.zbuf.psm, 0u, 0u, maxX, maxY);
    PageSpan texture{};
    if (state.prim.tme)
    {
        const auto &tex = ctx.tex0;
        const uint32_t extentU = (ctx.clamp & 0x2u) != 0u ? 1024u : std::max<uint32_t>(state.textureWidth, 1u);
        const uint32_t extentV = ((ctx.clamp >> 2) & 0x2u) != 0u ? 1024u : std::max<uint32_t>(state.textureHeight, 1u);
        texture = pageSpan(tex.tbp0, tex.tbw, tex.psm, 0u, 0u, extentU - 1u, extentV - 1u);
    }
    // T8H/T4HL/T4HH sample only the top byte of 32-bit words, which RGB24/Z24 writes leave alone, so
    // such reads do not depend on the order of those writes.
    const bool topReader = texture.count != 0u && readsTopByteOnly(ctx.tex0.psm);
    const PageSpan frameTop = keepsTopByte(ctx.frame.psm) ? PageSpan{} : frame;
    const PageSpan depthTop = keepsTopByte(ctx.zbuf.psm) ? PageSpan{} : depth;
    auto readsChanged = [&](const PageSpan &written, const PageSpan &writtenTop)
    {
        return topReader ? spansOverlap(texture, writtenTop) : spansOverlap(texture, written);
    };

    // A pixel's colour and depth must not share memory, and the primitive must not sample anything its
    // own writes change; otherwise it is drawn alone, in order.
    if (spansOverlap(frame, depth) || (texture.count != 0u && (readsChanged(frame, frameTop) || readsChanged(depth, depthTop))))
    {
        FlushPendingUnlocked(FlushReason::DrawAlone);
        GSStatTimer timer(kTimeAlone);
        if (gsStatsEnabled())
            gsStats().alonePrims.fetch_add(1u, std::memory_order_relaxed);
        DrawPrimitive(batch);
        return;
    }
    const bool selfAlias = texture.count != 0u && (spansOverlap(texture, frame) || spansOverlap(texture, depth));
    // Tracked batches log overwritten words through direct target addressing, and rely on every
    // primitive reading a texel before writing a pixel and on all of them writing the same pages.
    const bool samplesEveryPixel = state.prim.type == GS_PRIM_SPRITE || state.prim.type == GS_PRIM_TRIANGLE ||
                                   state.prim.type == GS_PRIM_TRISTRIP || state.prim.type == GS_PRIM_TRIFAN;
    const bool trackable = state.prim.tme && samplesEveryPixel && GSMem::TargetPixelOffset(ctx.frame.psm) && GSMem::TargetPixelOffset(ctx.zbuf.psm);
    if (selfAlias && !trackable)
    {
        FlushPendingUnlocked(FlushReason::DrawAlone);
        GSStatTimer timer(kTimeAlone);
        if (gsStatsEnabled())
            gsStats().alonePrims.fetch_add(1u, std::memory_order_relaxed);
        DrawPrimitive(batch);
        return;
    }

    // Every queued primitive must address colour and depth the same way, so a pixel's bytes stay on the
    // row range that owns the pixel.
    const uint64_t targetKey = (static_cast<uint64_t>(ctx.frame.fbp) << 0u) ^ (static_cast<uint64_t>(fbw) << 12u) ^
                               (static_cast<uint64_t>(ctx.frame.psm) << 20u) ^ (static_cast<uint64_t>(ctx.zbuf.zbp) << 28u) ^
                               (static_cast<uint64_t>(ctx.zbuf.psm) << 40u) ^ (1ull << 63u);
    const uint64_t spanKey = (static_cast<uint64_t>(frame.first) << 0u) | (static_cast<uint64_t>(frame.count) << 16u) |
                             (static_cast<uint64_t>(depth.first) << 32u) | (static_cast<uint64_t>(depth.count) << 48u);
    auto aliasesPending = [&]()
    {
        return selfAlias || anyPage(m_pendingWritePages, texture) || anyPage(m_pendingTexturePages, frame) ||
               anyPage(m_pendingTexturePages, depth);
    };
    if (!m_pending.empty())
    {
        const bool readsPendingWrites = topReader ? anyPage(m_pendingTopWritePages, texture) : anyPage(m_pendingWritePages, texture);
        const bool writesPendingReads = anyPage(m_pendingFullTexturePages, frame) || anyPage(m_pendingFullTexturePages, depth) ||
                                        anyPage(m_pendingTexturePages, frameTop) || anyPage(m_pendingTexturePages, depthTop);
        const bool tracked = m_pendingTracked || aliasesPending();
        // An untracked batch that already let a palette load read VRAM cannot become tracked (see LoadClutNow).
        const bool canTrack = m_pendingTrackable && trackable && spanKey == m_pendingSpanKey &&
                              (m_pendingTracked || m_pendingClutReadIndex == 0u);
        FlushReason reason = FlushReason::Count;
        if (targetKey != m_pendingTargetKey)
            reason = FlushReason::TargetChange;
        else if (anyPage(m_pendingDepthPages, frame) || anyPage(m_pendingFramePages, depth))
            reason = FlushReason::TargetOverlap;
        else if (readsPendingWrites)
            reason = FlushReason::ReadsPendingWrites;
        else if (writesPendingReads)
            reason = FlushReason::WritesPendingReads;
        else if (tracked && !canTrack)
            reason = FlushReason::TrackMismatch;
        if (reason != FlushReason::Count)
            FlushPendingUnlocked(reason);
    }
    if (m_pending.empty())
    {
        m_pendingSpanKey = spanKey;
        m_pendingTrackable = trackable;
    }
    m_pendingTrackable = m_pendingTrackable && trackable && spanKey == m_pendingSpanKey;
    if (!m_pendingTracked && aliasesPending())
    {
        m_pendingTracked = true;
        if (gsStatsEnabled())
        {
            GSStatCounters &stats = gsStats();
            stats.trackStart[selfAlias ? 0 : 1].fetch_add(1u, std::memory_order_relaxed);
            stats.trackSample[0] = ctx.frame.fbp;
            stats.trackSample[1] = ctx.frame.psm;
            stats.trackSample[2] = ctx.zbuf.zbp;
            stats.trackSample[3] = ctx.zbuf.psm;
            stats.trackSample[4] = ctx.tex0.tbp0;
            stats.trackSample[5] = ctx.tex0.psm;
            stats.trackSample[6] = state.prim.type;
        }
    }

    m_pendingTargetKey = targetKey;
    markPages(m_pendingWritePages, frame);
    markPages(m_pendingWritePages, depth);
    markPages(m_pendingFramePages, frame);
    markPages(m_pendingDepthPages, depth);
    markPages(m_pendingTopWritePages, frameTop);
    markPages(m_pendingTopWritePages, depthTop);
    markPages(m_pendingTexturePages, texture);
    if (!topReader)
        markPages(m_pendingFullTexturePages, texture);
    int32_t clutIndex = -1;
    if (state.prim.tme && (isFourBitIndexedPsm(ctx.tex0.psm) || isEightBitIndexedPsm(ctx.tex0.psm)))
    {
        const uint64_t clutKey = (static_cast<uint64_t>(ctx.tex0.cpsm) << 0u) | (static_cast<uint64_t>(ctx.tex0.csm) << 8u) |
                                 (static_cast<uint64_t>(ctx.tex0.csa) << 16u) | (static_cast<uint64_t>(ctx.tex0.psm) << 24u) |
                                 (static_cast<uint64_t>(state.texa.ta0) << 32u) | (static_cast<uint64_t>(state.texa.ta1) << 40u) |
                                 (static_cast<uint64_t>(state.texa.aem ? 1u : 0u) << 48u);
        if (m_pendingCluts.empty() || m_pendingClutGeneration != m_clutGeneration || m_pendingClutKey != clutKey)
        {
            m_pendingCluts.emplace_back();
            ExpandClut(state, m_pendingCluts.back().data());
            m_pendingClutGeneration = m_clutGeneration;
            m_pendingClutKey = clutKey;
        }
        clutIndex = static_cast<int32_t>(m_pendingCluts.size() - 1u);
    }
    m_pendingClutIndex.push_back(clutIndex);
    m_pending.push_back(batch);
    const PixelBounds bounds = primitiveBounds(batch);
    // A row of slack on each side: bands only use this to skip primitives that cannot reach them.
    m_pendingRows.push_back(bounds.empty() ? RowRange{0, -1} : RowRange{bounds.y0 - 1, bounds.y1 + 1});
    addRowWeights(m_pendingRowWeights, bounds);
    m_pendingPixels += bounds.area();
    if (m_pending.size() >= kMaxPendingPrimitives || m_pendingPixels >= kMaxPendingPixels)
        FlushPendingUnlocked(FlushReason::Full);
}

void GSCpuBackend::ClearPendingUnlocked()
{
    m_pending.clear();
    m_pendingCluts.clear();
    m_pendingClutIndex.clear();
    m_pendingRows.clear();
    m_pendingClutGeneration = UINT64_MAX;
    m_pendingClutReadIndex = 0u;
    m_pendingWritePages.reset();
    m_pendingTexturePages.reset();
    m_pendingFramePages.reset();
    m_pendingDepthPages.reset();
    m_pendingFullTexturePages.reset();
    m_pendingTopWritePages.reset();
    m_pendingTracked = false;
    m_pendingTrackable = true;
    m_pendingPixels = 0u;
    m_pendingRowWeights.fill(0u);
}

void GSCpuBackend::AddRasterStats(const RasterStats &stats)
{
    GSStatCounters &s = gsStats();
    s.boxPixels.fetch_add(stats.boxPixels, std::memory_order_relaxed);
    s.insidePixels.fetch_add(stats.insidePixels, std::memory_order_relaxed);
    s.writtenPixels.fetch_add(stats.writtenPixels, std::memory_order_relaxed);
    s.texSamples.fetch_add(stats.texSamples, std::memory_order_relaxed);
    s.bigBoxPixels.fetch_add(stats.bigBoxPixels, std::memory_order_relaxed);
    s.bigInsidePixels.fetch_add(stats.bigInsidePixels, std::memory_order_relaxed);
}

void GSCpuBackend::FlushPendingUnlocked(FlushReason reason)
{
    if (m_pending.empty())
        return;
    const bool statsEnabled = gsStatsEnabled();
    if (statsEnabled)
        gsStats().flushes[static_cast<size_t>(reason)].fetch_add(1u, std::memory_order_relaxed);

    bool parallel = m_pendingPixels >= kMinParallelPixels && RasterParticipants() > 1u;
    // A tracked batch too small to wake the workers is still drawn as a band, on this thread: drawn serially
    // it would get palette loads queued in between wrong (see LoadClutNow).
    bool inlineBand = false;
    uint32_t livePage = 0u;
    if (m_pendingTracked && m_texturePageCache.MatchesVram(m_vram))
    {
        // Bands read pages in place; the serial cache state is rebuilt afterwards from VRAM.
        inlineBand = !parallel;
        parallel = true;
    }
    else if (parallel && m_pendingTracked)
    {
        parallel = false;
    }
    else if (parallel && !m_texturePageCache.LivePage(livePage) && m_texturePageCache.CachedPage(livePage))
    {
        // Workers read pages in place, which matches a copied page only if VRAM still holds the same bytes.
        if (!m_texturePageCache.MatchesVram(m_vram))
            parallel = false;
        else
            m_texturePageCache.MakeLive();
    }

    const int kind = parallel ? (m_pendingTracked ? 0 : 1) : 2;
    if (statsEnabled)
    {
        GSStatCounters &stats = gsStats();
        stats.batches[kind].fetch_add(1u, std::memory_order_relaxed);
        stats.batchPrims[kind].fetch_add(m_pending.size(), std::memory_order_relaxed);
        stats.batchPixels[kind].fetch_add(m_pendingPixels, std::memory_order_relaxed);
    }
    GSStatTimer timer(kind == 0 ? kTimeTracked : kind == 1 ? kTimeUntracked : kTimeSerialBatch);
    if (parallel && m_pendingTracked)
    {
        RasterizeTrackedInParallel(inlineBand);
    }
    else if (parallel)
    {
        RasterizePendingInParallel();
    }
    else
    {
        for (size_t i = 0; i < m_pending.size(); ++i)
        {
            const int32_t clutIndex = m_pendingClutIndex[i];
            DrawPrimitive(m_pending[i], clutIndex >= 0 ? m_pendingCluts[static_cast<size_t>(clutIndex)].data() : nullptr);
        }
    }
    ClearPendingUnlocked();
}

void GSCpuBackend::RasterizeTrackedInParallel(bool inlineBand)
{
    // Every primitive samples only top bytes of the pages the batch draws into with RGB24/Z24, so every
    // pixel comes out the same drawn in any order. What a serial draw also leaves behind is the texture
    // cache: it copies a drawn-over page when loading it, so the page it ends on can hold words as they
    // were mid-batch.
    uint32_t livePage = 0u;
    if (m_texturePageCache.LivePage(livePage) &&
        m_pendingWritePages.test((livePage / GSMem::TexturePageCache::kPageSize) % 512u))
        m_texturePageCache.Snapshot(m_vram);
    uint32_t cachedBefore = UINT32_MAX;
    m_texturePageCache.CachedPage(cachedBefore);

    if (!inlineBand && (!m_workers || m_workers->participants() != RasterParticipants()))
        m_workers = std::make_unique<RasterWorkers>(RasterParticipants());
    const uint32_t bands = inlineBand ? 1u : m_workers->participants() * kBandsPerWorker;
    std::vector<RowRange> rows;
    BalanceRows(m_pendingRowWeights, bands, rows);
    const std::bitset<512> kept = m_pendingTexturePages & m_pendingWritePages;
    std::vector<GSMem::TexturePageCache> &caches = m_bandCaches;
    caches.resize(bands);
    std::vector<RasterStats> bandStats(gsStatsEnabled() ? bands : 0u);
    m_undoLogs.resize(bands);
    for (uint32_t i = 0; i < bands; ++i)
    {
        caches[i].ResetForBand(true);
        m_undoLogs[i].clear();
    }
    const std::function<void(uint32_t)> band = [&](uint32_t index)
    {
        RasterContext raster{};
        raster.rowBegin = rows[index].begin;
        raster.rowEnd = rows[index].end;
        raster.cache = &caches[index];
        raster.trackReads = true;
        raster.batched = true;
        raster.undo = &m_undoLogs[index];
        raster.undoPages = &kept;
        raster.stats = bandStats.empty() ? nullptr : &bandStats[index];
        for (size_t i = 0; i < m_pending.size(); ++i)
        {
            raster.primitive = static_cast<uint32_t>(i);
            const int32_t clutIndex = m_pendingClutIndex[i];
            raster.clut = clutIndex >= 0 ? m_pendingCluts[static_cast<size_t>(clutIndex)].data() : nullptr;
            // Most primitives miss a given band; skipping them here avoids each one's setup per band.
            if (m_pendingRows[i].end < raster.rowBegin || m_pendingRows[i].begin >= raster.rowEnd)
                continue;
            DrawPrimitiveRows(m_pending[i], raster);
        }
    };
    if (inlineBand)
        band(0u);
    else
        m_workers->runBands(bands, band);
    for (uint32_t i = 0; i < bandStats.size(); ++i)
    {
        AddRasterStats(bandStats[i]);
        gsStats().undoEntries.fetch_add(m_undoLogs[i].size(), std::memory_order_relaxed);
    }

    // The page of the last texel read in serial order is where the cache ends.
    GSMem::TexturePageCache::ReadPosition lastRead{};
    uint32_t lastPage = UINT32_MAX;
    for (uint32_t i = 0; i < bands; ++i)
    {
        if (caches[i].Track().last.after(lastRead))
        {
            lastRead = caches[i].Track().last;
            lastPage = caches[i].Track().lastPage;
        }
    }
    // A palette load from VRAM between queued primitives also read through the cache (a page the batch
    // does not write); it ran before the batch was drawn and left the cache where it ends if nothing
    // after it samples.
    GSMem::TexturePageCache::ReadPosition clutRead{};
    if (m_pendingClutReadIndex > 0u)
        clutRead = {static_cast<uint32_t>(m_pendingClutReadIndex), std::numeric_limits<int>::min(), std::numeric_limits<int>::min(), 0u, true};
    if (!lastRead.after(clutRead))
        return;
    if (!kept.test(lastPage / GSMem::TexturePageCache::kPageSize))
    {
        // Not drawn over: the serial cache reads it in place.
        m_texturePageCache.SetLivePage(lastPage);
        return;
    }

    // The serial cache copied lastPage when it read it right after its latest read of any other page.
    GSMem::TexturePageCache::ReadPosition switchRead{};
    bool switchInsidePixel = false;
    for (uint32_t i = 0; i < bands; ++i)
    {
        const auto &track = caches[i].Track();
        if (!track.last.valid)
            continue;
        // A band ending on another page read that page last; one ending on lastPage read others before.
        const GSMem::TexturePageCache::ReadPosition candidate = track.lastPage != lastPage ? track.last : track.other;
        if (candidate.after(switchRead))
        {
            switchRead = candidate;
            switchInsidePixel = track.lastPage == lastPage && track.switchedInsidePixel;
        }
    }
    if (clutRead.after(switchRead))
    {
        switchRead = clutRead;
        switchInsidePixel = false;
    }
    // With no switch in the batch, a cache already holding the page keeps its copy (snapshotted above if
    // it was read in place); otherwise the page was copied on the batch's first read, before any write.
    if (!switchRead.valid && cachedBefore == lastPage)
        return;

    // Take what VRAM holds now and undo, newest first, every write to the page made after the copy. Each
    // word belongs to one pixel, which one band drew in serial order.
    auto drawnBeforeCopy = [&](const UndoEntry &entry)
    {
        if (!switchRead.valid)
            return false;
        if (entry.primitive != switchRead.primitive)
            return entry.primitive < switchRead.primitive;
        if (entry.y != switchRead.y)
            return entry.y < switchRead.y;
        return switchInsidePixel ? entry.x < switchRead.x : entry.x <= switchRead.x;
    };
    m_pageRebuildScratch.resize(GSMem::TexturePageCache::kPageSize);
    std::memcpy(m_pageRebuildScratch.data(), m_vram + lastPage, GSMem::TexturePageCache::kPageSize);
    for (const std::vector<UndoEntry> &log : m_undoLogs)
        for (auto entry = log.rbegin(); entry != log.rend(); ++entry)
            if (entry->offset - lastPage < GSMem::TexturePageCache::kPageSize && !drawnBeforeCopy(*entry))
                std::memcpy(m_pageRebuildScratch.data() + (entry->offset - lastPage), &entry->old, sizeof(entry->old));
    m_texturePageCache.SetCopy(m_pageRebuildScratch.data(), lastPage);
}

void GSCpuBackend::RasterizePendingInParallel()
{
    // The page cached before the batch keeps whatever a sequential draw would leave in it: a page about to
    // be drawn over is snapshotted first, exactly as PrepareTexturePageCache does for the first writer.
    uint32_t livePage = 0u;
    if (m_texturePageCache.LivePage(livePage) &&
        m_pendingWritePages.test((livePage / GSMem::TexturePageCache::kPageSize) % 512u))
        m_texturePageCache.Snapshot(m_vram);

    if (!m_workers || m_workers->participants() != RasterParticipants())
        m_workers = std::make_unique<RasterWorkers>(RasterParticipants());
    const uint32_t bands = m_workers->participants() * kBandsPerWorker;
    std::vector<RowRange> rows;
    BalanceRows(m_pendingRowWeights, bands, rows);
    std::vector<GSMem::TexturePageCache> &caches = m_bandCaches;
    caches.resize(bands);
    for (GSMem::TexturePageCache &cache : caches)
        cache.ResetForBand(false);
    std::vector<RasterTrace> traces(bands);
    std::vector<RasterStats> bandStats(gsStatsEnabled() ? bands : 0u);
    const std::function<void(uint32_t)> band = [&](uint32_t index)
    {
        RasterContext raster{};
        raster.rowBegin = rows[index].begin;
        raster.rowEnd = rows[index].end;
        raster.cache = &caches[index];
        raster.trace = &traces[index];
        raster.batched = true;
        raster.stats = bandStats.empty() ? nullptr : &bandStats[index];
        for (size_t i = 0; i < m_pending.size(); ++i)
        {
            raster.primitive = static_cast<uint32_t>(i);
            const int32_t clutIndex = m_pendingClutIndex[i];
            raster.clut = clutIndex >= 0 ? m_pendingCluts[static_cast<size_t>(clutIndex)].data() : nullptr;
            // Most primitives miss a given band; skipping them here avoids each one's setup per band.
            if (m_pendingRows[i].end < raster.rowBegin || m_pendingRows[i].begin >= raster.rowEnd)
                continue;
            DrawPrimitiveRows(m_pending[i], raster);
        }
    };
    m_workers->runBands(bands, band);
    for (const RasterStats &stats : bandStats)
        AddRasterStats(stats);

    // Sequentially, the cache would end on the page of the last texel read: the last sampled pixel in
    // primitive then raster order, which lies on exactly one band.
    int best = -1;
    for (uint32_t i = 0; i < bands; ++i)
    {
        const RasterTrace &trace = traces[i];
        if (!trace.sampled)
            continue;
        if (best < 0)
        {
            best = static_cast<int>(i);
            continue;
        }
        const RasterTrace &current = traces[static_cast<size_t>(best)];
        if (trace.primitive > current.primitive ||
            (trace.primitive == current.primitive && (trace.y > current.y || (trace.y == current.y && trace.x > current.x))))
            best = static_cast<int>(i);
    }
    uint32_t lastPage = 0u;
    if (best >= 0 && traces[static_cast<size_t>(best)].primitive >= m_pendingClutReadIndex &&
        caches[static_cast<size_t>(best)].CachedPage(lastPage))
        m_texturePageCache.SetLivePage(lastPage);
}

// ---- GS thread -------------------------------------------------------------------------------------

namespace
{
    // Bounds the backlog: the producer waits rather than queueing several frames of geometry.
    constexpr size_t kMaxQueuedCommands = 2048u;
}

void GSCpuBackend::SetAsync(bool enabled)
{
    if (enabled == m_async)
        return;
    if (enabled)
    {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_queueStop = false;
        }
        m_async = true;
        m_gsThread = std::thread([this]() { RunQueue(); });
        return;
    }
    WaitForQueue();
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queueStop = true;
    }
    m_queueReady.notify_all();
    m_gsThread.join();
    m_async = false;
}

void GSCpuBackend::SetDisplayRegisters(const GSRegisters *regs)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_displayRegs = regs;
    m_lastQueuedVSync = regs ? regs->vsyncTick.load(std::memory_order_acquire) : 0u;
}

void GSCpuBackend::Enqueue(Command &&command)
{
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_queueProgress.wait(lock, [this]() { return m_queue.size() < kMaxQueuedCommands; });
        // Nothing was queued since the vblank, so this stream position is the vblank itself.
        if (m_displayRegs)
        {
            const uint64_t tick = m_displayRegs->vsyncTick.load(std::memory_order_acquire);
            if (tick != m_lastQueuedVSync)
            {
                m_lastQueuedVSync = tick;
                Command capture{};
                capture.kind = Command::Kind::CaptureFrame;
                capture.display.pmode = m_displayRegs->pmode;
                capture.display.smode2 = m_displayRegs->smode2;
                capture.display.dispfb1 = m_displayRegs->dispfb1;
                capture.display.display1 = m_displayRegs->display1;
                capture.display.dispfb2 = m_displayRegs->dispfb2;
                capture.display.display2 = m_displayRegs->display2;
                capture.display.bgcolor = m_displayRegs->bgcolor;
                capture.display.vsyncTick = tick;
                m_queue.push_back(std::move(capture));
                ++m_queueSubmitted;
            }
        }
        m_queue.push_back(std::move(command));
        ++m_queueSubmitted;
    }
    m_queueReady.notify_one();
}

void GSCpuBackend::WaitForQueue() const
{
    if (!m_async)
        return;
    std::unique_lock<std::mutex> lock(m_queueMutex);
    const uint64_t target = m_queueSubmitted;
    m_queueProgress.wait(lock, [this, target]() { return m_queueCompleted >= target; });
}

void GSCpuBackend::RunQueue()
{
    ThreadNaming::SetCurrentThreadName("GSThread");
    for (;;)
    {
        std::deque<Command> work;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            GSStatTimer idleTimer(kTimeIdle);
            m_queueReady.wait(lock, [this]() { return m_queueStop || !m_queue.empty(); });
            if (m_queue.empty())
                return;
            work.swap(m_queue);
        }
        m_queueProgress.notify_all();
        for (Command &command : work)
        {
            try
            {
                Execute(command);
            }
            catch (const std::exception &error)
            {
                // Nothing on this thread may unwind past the queue, or readers would wait forever.
                std::cerr << "[GS] command " << static_cast<int>(command.kind) << " failed on the GS thread: " << error.what() << std::endl;
            }
            // Readers wait on completed commands, so batches still queued would hold them up needlessly.
            std::lock_guard<std::mutex> lock(m_queueMutex);
            ++m_queueCompleted;
        }
        bool idle = false;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            idle = m_queue.empty();
        }
        m_queueProgress.notify_all();
        // Nothing else to do: rasterize what is batched now instead of when someone next reads.
        if (idle)
            FlushNow();
        if (gsStatsEnabled())
            printGsStatsIfDue();
    }
}

void GSCpuBackend::Execute(Command &command)
{
    uint32_t category = kTimeOther;
    switch (command.kind)
    {
    case Command::Kind::Submit:
        category = kTimeSubmitOther;
        break;
    case Command::Kind::LoadClut:
        category = kTimeClut;
        break;
    case Command::Kind::BeginTransfer:
        category = kTimeTransfer;
        break;
    case Command::Kind::UploadImage:
        category = kTimeUpload;
        break;
    case Command::Kind::CaptureFrame:
        category = kTimeCapture;
        break;
    case Command::Kind::ClearFramebuffer:
        category = kTimeClear;
        break;
    default:
        break;
    }
    GSStatTimer timer(category);
    switch (command.kind)
    {
    case Command::Kind::Submit:
        SubmitNow(command.batch);
        break;
    case Command::Kind::LoadClut:
        LoadClutNow(command.tex0, command.texclut);
        break;
    case Command::Kind::BeginTransfer:
        BeginTransferNow(command.transfer);
        break;
    case Command::Kind::UploadImage:
        UploadImageNow(command.data.data(), static_cast<uint32_t>(command.data.size()));
        break;
    case Command::Kind::TextureFlush:
        TextureFlushNow();
        break;
    case Command::Kind::ClearFramebuffer:
        (void)ClearFramebufferNow(command.context, command.args[0]);
        break;
    case Command::Kind::WriteVram:
        WriteVramNow(command.args[0], command.args[1], command.args[2], command.args[3], command.args[4], command.args[5]);
        break;
    case Command::Kind::CaptureFrame:
        CaptureFrameNow(command.display);
        break;
    }
}

void GSCpuBackend::Initialize(uint8_t *vram, uint32_t vramSize)
{
    WaitForQueue();
    InitializeNow(vram, vramSize);
}

void GSCpuBackend::Reset()
{
    WaitForQueue();
    ResetNow();
}

void GSCpuBackend::Submit(const GSPrimitiveBatch &batch)
{
    if (!m_async)
    {
        SubmitNow(batch);
        return;
    }
    if (batch.vertexCount == 0u)
        return;
    Command command;
    command.kind = Command::Kind::Submit;
    command.batch = batch;
    Enqueue(std::move(command));
}

void GSCpuBackend::LoadClut(const GSTex0Reg &tex0, const GSTexClutReg &texclut)
{
    if (!m_async)
    {
        LoadClutNow(tex0, texclut);
        return;
    }
    Command command;
    command.kind = Command::Kind::LoadClut;
    command.tex0 = tex0;
    command.texclut = texclut;
    Enqueue(std::move(command));
}

void GSCpuBackend::BeginTransfer(const GSTransferCommand &transfer)
{
    if (!m_async)
    {
        BeginTransferNow(transfer);
        return;
    }
    Command command;
    command.kind = Command::Kind::BeginTransfer;
    command.transfer = transfer;
    Enqueue(std::move(command));
}

void GSCpuBackend::UploadImage(const uint8_t *data, uint32_t sizeBytes)
{
    if (!m_async)
    {
        UploadImageNow(data, sizeBytes);
        return;
    }
    if (!data || sizeBytes == 0u)
        return;
    Command command;
    command.kind = Command::Kind::UploadImage;
    command.data.assign(data, data + sizeBytes);
    Enqueue(std::move(command));
}

void GSCpuBackend::Flush()
{
    // Queued commands are already on their way and the GS thread rasterizes batches when it runs dry;
    // waiting is left to Sync and to the readers.
    if (!m_async)
        FlushNow();
}

void GSCpuBackend::TextureFlush()
{
    if (!m_async)
    {
        TextureFlushNow();
        return;
    }
    Command command;
    command.kind = Command::Kind::TextureFlush;
    Enqueue(std::move(command));
}

void GSCpuBackend::Sync(GSSyncReason reason)
{
    // Presentation shows whatever has been drawn; holding the host thread until the guest's backlog
    // drains would only stall the window.
    if (m_async && reason == GSSyncReason::Presentation)
        return;
    WaitForQueue();
    SyncNow(reason);
}

bool GSCpuBackend::ClearFramebuffer(const GSContext &context, uint32_t rgba)
{
    if (!m_async)
        return ClearFramebufferNow(context, rgba);
    // The result depends only on the target format, so the clear itself can run later.
    const uint8_t psm = context.frame.psm;
    const bool clears = m_vram && context.frame.fbw != 0u &&
                        (psm == GS_PSM_CT32 || psm == GS_PSM_CT24 || psm == GS_PSM_CT16 || psm == GS_PSM_CT16S);
    Command command;
    command.kind = Command::Kind::ClearFramebuffer;
    command.context = context;
    command.args[0] = rgba;
    Enqueue(std::move(command));
    return clears;
}

uint32_t GSCpuBackend::ConsumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes)
{
    WaitForQueue();
    return ConsumeLocalToHostBytesNow(dst, maxBytes);
}

uint32_t GSCpuBackend::ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const
{
    WaitForQueue();
    return ReadVramNow(psm, base, bw, x, y);
}

void GSCpuBackend::WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value)
{
    if (!m_async)
    {
        WriteVramNow(psm, base, bw, x, y, value);
        return;
    }
    Command command;
    command.kind = Command::Kind::WriteVram;
    command.args[0] = psm;
    command.args[1] = base;
    command.args[2] = bw;
    command.args[3] = x;
    command.args[4] = y;
    command.args[5] = value;
    Enqueue(std::move(command));
}

void GSCpuBackend::SnapshotVram(std::vector<uint8_t> &out) const
{
    WaitForQueue();
    SnapshotVramNow(out);
}

GSTransferSnapshot GSCpuBackend::GetTransferSnapshot() const
{
    WaitForQueue();
    return GetTransferSnapshotNow();
}

void GSCpuBackend::InitializeNow(uint8_t *vram, uint32_t vramSize)
{
    if (vram && vramSize < GSMem::MEMORY_SIZE)
        throw std::invalid_argument("GS CPU backend requires at least 4 MiB of VRAM");

    std::lock_guard<std::mutex> lock(m_mutex);
    // A new VRAM binding makes queued primitives meaningless; nothing may still refer to the old memory.
    ClearPendingUnlocked();
    m_vram = vram;
    m_vramSize = vramSize;
    ResetUnlocked();
}

void GSCpuBackend::ResetNow()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend *>(this)->FlushPendingUnlocked(FlushReason::Other);
    ResetUnlocked();
}

void GSCpuBackend::ResetUnlocked()
{
    m_clut.fill(0u);
    m_clutCbp.fill(0u);
    m_clutGeneration = nextClutGeneration();
    m_texturePageCache.Invalidate();
    m_transfer = {};
    m_transfer.direction = 3u;
    m_transferState = {};
    m_transferState.direction = 3u;
    m_localToHostBuffer.clear();
    m_localToHostReadPos = 0u;
    m_uploadCarryBytes = 0u;
}

void GSCpuBackend::SubmitNow(const GSPrimitiveBatch &batch)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_vram || batch.vertexCount == 0u)
        return;
    if (gsStatsEnabled())
    {
        gsStats().prims[batch.state.prim.type & 7u].fetch_add(1u, std::memory_order_relaxed);
        if (batch.state.prim.tme)
            gsStats().texturedPrims.fetch_add(1u, std::memory_order_relaxed);
    }
    if (RasterParticipants() > 1u)
        QueuePrimitive(batch);
    else
        DrawPrimitive(batch);
}

void GSCpuBackend::LoadClutNow(const GSTex0Reg &tex0, const GSTexClutReg &texclut)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_pending.empty())
    {
        // Queued primitives captured their palettes, so the load can run now unless it reads memory they
        // will draw, or a stale cached page would make it read differently than after drawing them.
        const uint32_t sourceWidth = tex0.csm == 0u ? 1u : std::max<uint32_t>(texclut.cbw, 1u);
        const uint32_t maxX = tex0.csm == 0u ? 15u : (static_cast<uint32_t>(texclut.cou) << 4u) + 255u;
        const uint32_t maxY = tex0.csm == 0u ? 15u : texclut.cov;
        if (anyPage(m_pendingWritePages, pageSpan(tex0.cbp, sourceWidth, tex0.cpsm, 0u, 0u, maxX, maxY)) ||
            !m_texturePageCache.MatchesVram(m_vram))
            FlushPendingUnlocked(FlushReason::Clut);
    }
    if (!m_vram || (!isFourBitIndexedPsm(tex0.psm) && !isEightBitIndexedPsm(tex0.psm)))
        return;

    switch (tex0.cld)
    {
    case 0u:
    case 6u:
    case 7u:
        return;
    case 1u:
        break;
    case 2u:
        m_clutCbp[0] = tex0.cbp;
        break;
    case 3u:
        m_clutCbp[1] = tex0.cbp;
        break;
    case 4u:
        if (m_clutCbp[0] == tex0.cbp)
            return;
        m_clutCbp[0] = tex0.cbp;
        break;
    case 5u:
        if (m_clutCbp[1] == tex0.cbp)
            return;
        m_clutCbp[1] = tex0.cbp;
        break;
    default:
        return;
    }

    // LoadClutUnlocked reads VRAM (moving the texture page cache) only for 16- and 32-bit palettes. A tracked
    // batch accounts for that read when it is drawn by bands (in parallel, or as one band on this thread when
    // small), which it will be while the cache stays a faithful copy; otherwise it is drawn before the load.
    // Drawing small tracked batches before every load ended them after about 4 primitives in 3D scenes.
    const bool readsVram = tex0.cpsm == GS_PSM_CT16 || tex0.cpsm == GS_PSM_CT16S || tex0.cpsm == GS_PSM_CT32 || tex0.cpsm == GS_PSM_CT24;
    if (readsVram && m_pendingTracked && !m_texturePageCache.MatchesVram(m_vram))
        FlushPendingUnlocked(FlushReason::Clut);
    LoadClutUnlocked(tex0, texclut);
    if (readsVram)
        m_pendingClutReadIndex = m_pending.size();
}

void GSCpuBackend::LoadClutUnlocked(const GSTex0Reg &tex0, const GSTexClutReg &texclut)
{
    const bool fourBit = isFourBitIndexedPsm(tex0.psm);
    const bool sixteenBit = tex0.cpsm == GS_PSM_CT16 || tex0.cpsm == GS_PSM_CT16S;
    const bool thirtyTwoBit = tex0.cpsm == GS_PSM_CT32 || tex0.cpsm == GS_PSM_CT24;
    if (!sixteenBit && !thirtyTwoBit)
        return;

    m_clutGeneration = nextClutGeneration();
    const uint32_t entryCount = fourBit ? 16u : 256u;
    const uint32_t csaMask = sixteenBit ? 0x1Fu : 0x0Fu;
    const uint32_t destinationBase = (static_cast<uint32_t>(tex0.csa) & csaMask) << 4u;

    const bool loadCsm1Suffix = tex0.csm == 0u && thirtyTwoBit && !fourBit;
    const uint32_t firstEntry = loadCsm1Suffix ? destinationBase : 0u;

    for (uint32_t entry = firstEntry; entry < entryCount; ++entry)
    {
        uint32_t sourceX = 0u;
        uint32_t sourceY = 0u;
        uint32_t sourceWidth = 1u;

        if (tex0.csm == 0u)
        {
            const uint32_t sourceIndex = swizzleClutIndexCSM1(entry);
            sourceX = sourceIndex & 0x0Fu;
            sourceY = sourceIndex >> 4u;
        }
        else
        {
            sourceWidth = texclut.cbw != 0u ? static_cast<uint32_t>(texclut.cbw) : 1u;
            sourceX = (static_cast<uint32_t>(texclut.cou) << 4u) + entry;
            sourceY = static_cast<uint32_t>(texclut.cov);
        }

        const uint32_t raw = ReadTextureVramUnlocked(tex0.cpsm, tex0.cbp, sourceWidth, sourceX, sourceY); 
        const uint32_t destination = (loadCsm1Suffix ? entry : destinationBase + entry) & (sixteenBit ? 0x1FFu : 0x0FFu);
        if (sixteenBit)
        {
            m_clut[destination] = static_cast<uint16_t>(raw);
        }
        else
        {
            m_clut[destination] = static_cast<uint16_t>(raw & 0xFFFFu);
            m_clut[destination + 256u] = static_cast<uint16_t>(raw >> 16u);
        }
    }
}

void GSCpuBackend::FlushNow()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    FlushPendingUnlocked(FlushReason::Idle);
}

void GSCpuBackend::TextureFlushNow()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend *>(this)->FlushPendingUnlocked(FlushReason::TextureFlush);
    m_texturePageCache.Invalidate();
}

void GSCpuBackend::SyncNow(GSSyncReason)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    FlushPendingUnlocked(FlushReason::Readback);
}

uint32_t GSCpuBackend::ReadVramNow(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend *>(this)->FlushPendingUnlocked(FlushReason::Readback);
    return ReadVramUnlocked(psm, base, bw, x, y);
}

uint32_t GSCpuBackend::ReadVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const
{
    if (!m_vram)
        return 0u;
    return m_readVramFuncs[psm & 0x3Fu](m_vram, base, bw, x, y);
}

uint32_t GSCpuBackend::ReadTextureVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y)
{
    if (!m_vram)
        return 0u;

    return GSMem::ReadTexture(m_texturePageCache, m_vram, psm, base, bw, x, y);
}

void GSCpuBackend::WriteVramNow(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend *>(this)->FlushPendingUnlocked(FlushReason::Readback);
    WriteVramUnlocked(psm, base, bw, x, y, value);
}

void GSCpuBackend::WriteVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value)
{
    if (!m_vram)
        return;
    uint32_t livePage = 0u;
    if (m_texturePageCache.LivePage(livePage) &&
        spanHasPage(pageSpan(base, bw, psm, x, y, x, y), livePage / GSMem::TexturePageCache::kPageSize))
        m_texturePageCache.Snapshot(m_vram);
    m_writeVramFuncs[psm & 0x3Fu](m_vram, base, bw, x, y, value);
}

void GSCpuBackend::SnapshotVramNow(std::vector<uint8_t> &out) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend *>(this)->FlushPendingUnlocked(FlushReason::Readback);
    if (!m_vram || m_vramSize == 0u)
    {
        out.clear();
        return;
    }
    out.resize(m_vramSize);
    std::memcpy(out.data(), m_vram, m_vramSize);
}

void GSCpuBackend::CaptureFrameNow(const GSPresentationRequest &display)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        FlushPendingUnlocked(FlushReason::Capture);
        if (!m_vram || m_vramSize == 0u)
            return;
        m_captureScratch.resize(m_vramSize);
        std::memcpy(m_captureScratch.data(), m_vram, m_vramSize);
    }
    std::lock_guard<std::mutex> lock(m_captureMutex);
    m_capturedVram.swap(m_captureScratch);
    m_capturedDisplay = display;
}

GSTransferSnapshot GSCpuBackend::GetTransferSnapshotNow() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    GSTransferSnapshot result = m_transferState;
    result.localToHostPendingBytes = m_localToHostReadPos < m_localToHostBuffer.size()
                                         ? m_localToHostBuffer.size() - m_localToHostReadPos
                                         : 0u;
    return result;
}

void GSCpuBackend::PrepareTexturePageCache(const GSDrawState &state)
{
    const auto &ctx = state.context;
    const uint32_t maxX = ctx.scissor.x1;
    const uint32_t maxY = ctx.scissor.y1;
    const uint32_t fbw = std::max<u32>(ctx.frame.fbw, 1u);
    const PageSpan frame = pageSpan(GSInternal::framePageBaseToBlock(ctx.frame.fbp), fbw, ctx.frame.psm, 0u, 0u, maxX, maxY);
    const PageSpan depth = pageSpan(GSInternal::framePageBaseToBlock(ctx.zbuf.zbp), fbw, ctx.zbuf.psm, 0u, 0u, maxX, maxY);

    uint32_t livePage = 0u;
    if (m_texturePageCache.LivePage(livePage))
    {
        const uint32_t page = livePage / GSMem::TexturePageCache::kPageSize;
        if (spanHasPage(frame, page) || spanHasPage(depth, page))
            m_texturePageCache.Snapshot(m_vram);
    }

    // A primitive sampling pages it also writes must see each page as of its load, so copy on miss.
    bool copyOnMiss = false;
    if (state.prim.tme)
    {
        const auto &tex = ctx.tex0;
        const uint32_t extentU = (ctx.clamp & 0x2u) != 0u ? 1024u : std::max<uint32_t>(state.textureWidth, 1u);
        const uint32_t extentV = ((ctx.clamp >> 2) & 0x2u) != 0u ? 1024u : std::max<uint32_t>(state.textureHeight, 1u);
        const PageSpan texture = pageSpan(tex.tbp0, tex.tbw, tex.psm, 0u, 0u, extentU - 1u, extentV - 1u);
        copyOnMiss = spansOverlap(texture, frame) || spansOverlap(texture, depth);
    }
    m_texturePageCache.SetCopyOnMiss(copyOnMiss, frame.first, frame.count, depth.first, depth.count);
}

void GSCpuBackend::DrawPrimitive(const GSPrimitiveBatch &batch, const uint32_t *clut)
{
    const GSDrawState &state = batch.state;
    const auto &ctx = state.context;
    PS2_IF_AGRESSIVE_LOGS({
        const uint32_t primitiveIndex = s_debugPrimitiveCount.fetch_add(1u, std::memory_order_relaxed);
        if (primitiveIndex < 64u)
        {
            std::cout << "[gs:prim] idx=" << primitiveIndex
                      << " type=" << static_cast<uint32_t>(state.prim.type)
                      << " tme=" << static_cast<uint32_t>(state.prim.tme)
                      << " abe=" << static_cast<uint32_t>(state.prim.abe)
                      << " fst=" << static_cast<uint32_t>(state.prim.fst)
                      << " ctxt=" << static_cast<uint32_t>(state.prim.ctxt)
                      << " fbp=" << ctx.frame.fbp
                      << " fbw=" << ctx.frame.fbw
                      << " psm=0x" << std::hex << static_cast<uint32_t>(ctx.frame.psm) << std::dec
                      << " tex0=("
                      << "tbp0=" << ctx.tex0.tbp0
                      << " tbw=" << static_cast<uint32_t>(ctx.tex0.tbw)
                      << " psm=0x" << std::hex << static_cast<uint32_t>(ctx.tex0.psm) << std::dec
                      << " tw=" << static_cast<uint32_t>(ctx.tex0.tw)
                      << " th=" << static_cast<uint32_t>(ctx.tex0.th)
                      << " tcc=" << static_cast<uint32_t>(ctx.tex0.tcc)
                      << " tfx=" << static_cast<uint32_t>(ctx.tex0.tfx)
                      << " cbp=" << ctx.tex0.cbp
                      << " cpsm=0x" << std::hex << static_cast<uint32_t>(ctx.tex0.cpsm) << std::dec
                      << " csm=" << static_cast<uint32_t>(ctx.tex0.csm)
                      << " csa=" << static_cast<uint32_t>(ctx.tex0.csa)
                      << ")"
                      << " texclut=("
                      << "cbw=" << static_cast<uint32_t>(state.texclut.cbw)
                      << " cou=" << static_cast<uint32_t>(state.texclut.cou)
                      << " cov=" << state.texclut.cov
                      << ")"
                      << " ofx=" << (ctx.xyoffset.ofx >> 4)
                      << " ofy=" << (ctx.xyoffset.ofy >> 4)
                      << " scissor=(" << ctx.scissor.x0
                      << "," << ctx.scissor.y0
                      << ")-(" << ctx.scissor.x1
                      << "," << ctx.scissor.y1 << ")"
                      << " test=0x" << std::hex << ctx.test
                      << " alpha=0x" << ctx.alpha
                      << std::dec
                      << " v0=(" << batch.vertices[0].x << "," << batch.vertices[0].y << ")"
                      << " uv0=(" << (batch.vertices[0].u >> 4) << "," << (batch.vertices[0].v >> 4) << ")"
                      << " stq0=(" << batch.vertices[0].s << "," << batch.vertices[0].t << "," << batch.vertices[0].q << ")"
                      << " v1=(" << batch.vertices[1].x << "," << batch.vertices[1].y << ")"
                      << " uv1=(" << (batch.vertices[1].u >> 4) << "," << (batch.vertices[1].v >> 4) << ")"
                      << " stq1=(" << batch.vertices[1].s << "," << batch.vertices[1].t << "," << batch.vertices[1].q << ")"
                      << " v2=(" << batch.vertices[2].x << "," << batch.vertices[2].y << ")"
                      << " uv2=(" << (batch.vertices[2].u >> 4) << "," << (batch.vertices[2].v >> 4) << ")"
                      << " stq2=(" << batch.vertices[2].s << "," << batch.vertices[2].t << "," << batch.vertices[2].q << ")"
                      << " rgba0=(" << static_cast<uint32_t>(batch.vertices[0].r) << ","
                      << static_cast<uint32_t>(batch.vertices[0].g) << ","
                      << static_cast<uint32_t>(batch.vertices[0].b) << ","
                      << static_cast<uint32_t>(batch.vertices[0].a) << ")"
                      << " rgba1=(" << static_cast<uint32_t>(batch.vertices[1].r) << ","
                      << static_cast<uint32_t>(batch.vertices[1].g) << ","
                      << static_cast<uint32_t>(batch.vertices[1].b) << ","
                      << static_cast<uint32_t>(batch.vertices[1].a) << ")"
                      << " rgba2=(" << static_cast<uint32_t>(batch.vertices[2].r) << ","
                      << static_cast<uint32_t>(batch.vertices[2].g) << ","
                      << static_cast<uint32_t>(batch.vertices[2].b) << ","
                      << static_cast<uint32_t>(batch.vertices[2].a) << ")"
                      << std::endl;
        }
    });

    PS2_IF_AGRESSIVE_LOGS({
        if ((state.prim.ctxt != 0u || ctx.frame.fbp == 150u) &&
            s_debugContext1PrimitiveCount.fetch_add(1u, std::memory_order_relaxed) < 32u)
        {
            std::cout << "[gs:copy-prim]"
                      << " type=" << static_cast<uint32_t>(state.prim.type)
                      << " tme=" << static_cast<uint32_t>(state.prim.tme)
                      << " abe=" << static_cast<uint32_t>(state.prim.abe)
                      << " fst=" << static_cast<uint32_t>(state.prim.fst)
                      << " ctxt=" << static_cast<uint32_t>(state.prim.ctxt)
                      << " fbp=" << ctx.frame.fbp
                      << " fbw=" << ctx.frame.fbw
                      << " psm=0x" << std::hex << static_cast<uint32_t>(ctx.frame.psm) << std::dec
                      << " tex0=("
                      << "tbp0=" << ctx.tex0.tbp0
                      << " tbw=" << static_cast<uint32_t>(ctx.tex0.tbw)
                      << " psm=0x" << std::hex << static_cast<uint32_t>(ctx.tex0.psm) << std::dec
                      << " tcc=" << static_cast<uint32_t>(ctx.tex0.tcc)
                      << " tfx=" << static_cast<uint32_t>(ctx.tex0.tfx)
                      << " cbp=" << ctx.tex0.cbp
                      << " cpsm=0x" << std::hex << static_cast<uint32_t>(ctx.tex0.cpsm) << std::dec
                      << " csm=" << static_cast<uint32_t>(ctx.tex0.csm)
                      << " csa=" << static_cast<uint32_t>(ctx.tex0.csa)
                      << ")"
                      << " texclut=("
                      << "cbw=" << static_cast<uint32_t>(state.texclut.cbw)
                      << " cou=" << static_cast<uint32_t>(state.texclut.cou)
                      << " cov=" << state.texclut.cov
                      << ")"
                      << " ofx=" << (ctx.xyoffset.ofx >> 4)
                      << " ofy=" << (ctx.xyoffset.ofy >> 4)
                      << " scissor=(" << ctx.scissor.x0
                      << "," << ctx.scissor.y0
                      << ")-(" << ctx.scissor.x1
                      << "," << ctx.scissor.y1 << ")"
                      << " test=0x" << std::hex << ctx.test
                      << " alpha=0x" << ctx.alpha
                      << std::dec << std::endl;
        }
    });

    PrepareTexturePageCache(state);
    struct CopyOnMissReset
    {
        GSMem::TexturePageCache &cache;
        ~CopyOnMissReset() { cache.SetCopyOnMiss(false); }
    } copyOnMissReset{m_texturePageCache};

    RasterContext raster{};
    raster.cache = &m_texturePageCache;
    raster.clut = clut;
    RasterStats stats;
    if (gsStatsEnabled())
        raster.stats = &stats;
    DrawPrimitiveRows(batch, raster);
    if (raster.stats)
        AddRasterStats(stats);
}

void GSCpuBackend::DrawPrimitiveRows(const GSPrimitiveBatch &batch, const RasterContext &raster)
{
    const GSDrawState &state = batch.state;
    switch (state.prim.type)
    {
    case GS_PRIM_SPRITE:
        DrawSprite(batch, raster);
        break;
    case GS_PRIM_TRIANGLE:
    case GS_PRIM_TRISTRIP:
    case GS_PRIM_TRIFAN:
        DrawTriangle(batch, raster);
        break;
    case GS_PRIM_LINE:
    case GS_PRIM_LINESTRIP:
        DrawLine(batch, raster);
        break;
    case GS_PRIM_POINT:
    {
        const GSVertex &v = batch.vertices[0];
        const auto &ctx = state.context;
        int px = static_cast<int>(v.x) - (ctx.xyoffset.ofx >> 4);
        int py = static_cast<int>(v.y) - (ctx.xyoffset.ofy >> 4);
        if (raster.ownsRow(py))
            WritePixel(state, px, py, static_cast<u32>(v.z), v.r, v.g, v.b, v.a, v.fog);
        break;
    }
    default:
        break;
    }
}

struct GSCpuBackend::PixelPipeline
{
    const GSDrawState *state = nullptr;
    bool fge = false;
    u32 fbp = 0;
    u32 fbw = 1;
    u32 fpsm = 0;
    u32 zbp = 0;
    u32 zpsm = 0;
    uint32_t ztestMethod = 0;
    bool destinationAlphaTestNeedsRead = false;
    bool framebuffer16 = false;
    bool fbaSetsAlpha = false;
    uint8_t asel = 0;
    uint8_t bsel = 0;
    uint8_t csel = 0;
    uint8_t dsel = 0;
    uint8_t fix = 0;
    // Direct addressing for colour and depth; null falls back to the generic VRAM accessors.
    GSMem::PixelOffsetFn frameOffset = nullptr;
    GSMem::PixelOffsetFn depthOffset = nullptr;
    // Set in tracked batches: words overwritten in undoPages are logged for the pixel being drawn.
    std::vector<UndoEntry> *undo = nullptr;
    const std::bitset<512> *undoPages = nullptr;
    uint32_t primitive = 0;
};

void GSCpuBackend::WritePixel(const GSDrawState &state, int x, int y, int z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint8_t fog)
{
    WritePixel(MakePixelPipeline(state), x, y, z, r, g, b, a, fog);
}

namespace
{
    inline uint32_t readTarget(uint32_t psm, const uint8_t *pixel)
    {
        switch (psm)
        {
        case GS_PSM_CT24:
        case GS_PSM_Z24:
        {
            uint32_t value;
            std::memcpy(&value, pixel, sizeof(value));
            return value & 0x00FFFFFFu;
        }
        case GS_PSM_CT16:
        case GS_PSM_CT16S:
        case GS_PSM_Z16:
        case GS_PSM_Z16S:
        {
            uint16_t value;
            std::memcpy(&value, pixel, sizeof(value));
            return value;
        }
        default:
        {
            uint32_t value;
            std::memcpy(&value, pixel, sizeof(value));
            return value;
        }
        }
    }

    inline void writeTarget(uint32_t psm, uint8_t *pixel, uint32_t value)
    {
        switch (psm)
        {
        case GS_PSM_CT24:
        case GS_PSM_Z24:
        {
            uint32_t old;
            std::memcpy(&old, pixel, sizeof(old));
            value = (old & 0xFF000000u) | (value & 0x00FFFFFFu);
            std::memcpy(pixel, &value, sizeof(value));
            break;
        }
        case GS_PSM_CT16:
        case GS_PSM_CT16S:
        case GS_PSM_Z16:
        case GS_PSM_Z16S:
        {
            const uint16_t half = static_cast<uint16_t>(value);
            std::memcpy(pixel, &half, sizeof(half));
            break;
        }
        default:
            std::memcpy(pixel, &value, sizeof(value));
            break;
        }
    }
}

void GSCpuBackend::RecordUndo(const PixelPipeline &pipe, const uint8_t *pixel, int x, int y)
{
    // Only RGB24/Z24 targets write tracked pages, so the whole word is the pixel's.
    const uint32_t offset = static_cast<uint32_t>(pixel - m_vram);
    if (!pipe.undoPages->test(offset / GSMem::TexturePageCache::kPageSize))
        return;
    UndoEntry entry;
    entry.primitive = pipe.primitive;
    entry.y = y;
    entry.x = x;
    entry.offset = offset;
    std::memcpy(&entry.old, pixel, sizeof(entry.old));
    pipe.undo->push_back(entry);
}

__attribute__((always_inline)) inline bool GSCpuBackend::WritePixel(const PixelPipeline &pipe, int x, int y, int z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint8_t fog)
{
    const GSDrawState &state = *pipe.state;
    const auto &ctx = state.context;
    if (x < ctx.scissor.x0 || x > ctx.scissor.x1 || y < ctx.scissor.y0 || y > ctx.scissor.y1)
        return false;

    if (pipe.fge)
    {
        const uint32_t inverseFog = 255u - fog;
        auto applyFog = [&](uint8_t input, uint8_t fogColor) -> uint8_t
        {
            return static_cast<uint8_t>(((static_cast<uint32_t>(fog) * input) >> 8) + ((inverseFog * fogColor) >> 8));
        };

        r = applyFog(r, state.fogR);
        g = applyFog(g, state.fogG);
        b = applyFog(b, state.fogB);
    }

    const u32 fbp = pipe.fbp;
    const u32 fbw = pipe.fbw;
    const u32 fpsm = pipe.fpsm;
    const u32 zbp = pipe.zbp;
    const u32 zpsm = pipe.zpsm;

    const PixelWriteMask writeMask = classifyAlphaTest(ctx.test, a, static_cast<uint8_t>(fpsm));
    if (!writeMask.writesAnything())
    {
        return false;
    }

    const uint32_t ztestMethod = pipe.ztestMethod;
    const bool alphaBlendEnabled = state.prim.abe;
    const bool preserveDestinationAlpha = writeMask.writeRgb && !writeMask.writeAlpha && fpsm == GS_PSM_CT32;
    const bool destinationAlphaTestNeedsRead = pipe.destinationAlphaTestNeedsRead;

    // small optimization, avoid reading the framebuffer for simple draws
    // TODO: only one address lookup for rmw
    const bool frmw = destinationAlphaTestNeedsRead || (writeMask.writesFramebuffer() && ((ctx.frame.fbmsk != 0) || alphaBlendEnabled || preserveDestinationAlpha));

    u32 rawFramebufferPixel = 0;
    u32 fbrgba = 0;
    uint8_t *framePixel = nullptr;
    if (frmw)
    {
        if (pipe.frameOffset)
        {
            framePixel = m_vram + pipe.frameOffset(fbp, fbw, static_cast<u32>(x), static_cast<u32>(y));
            rawFramebufferPixel = readTarget(fpsm, framePixel);
        }
        else
        {
            rawFramebufferPixel = ReadVramUnlocked(fpsm, fbp, fbw, x, y);
        }
        fbrgba = rawFramebufferPixel;

        if (pipe.framebuffer16)
        {
            fbrgba = Rgba5551ToRgba8888(fbrgba);
        }
        else if (fpsm == GS_PSM_CT24)
        {
            // The GS supplies 0x80 as destination alpha for RGB24 blending.
            fbrgba |= 0x80000000u;
        }
    }

    if (!passesDestinationAlphaTest(ctx.test, static_cast<uint8_t>(fpsm), rawFramebufferPixel))
    {
        return false;
    }

    bool zpass = false;
    uint32_t storedZ = 0u;
    uint8_t *depthPixel = nullptr;
    auto readDepth = [&]() -> uint32_t
    {
        if (!pipe.depthOffset)
            return ReadVramUnlocked(zpsm, zbp, fbw, x, y);
        depthPixel = m_vram + pipe.depthOffset(zbp, fbw, static_cast<u32>(x), static_cast<u32>(y));
        return readTarget(zpsm, depthPixel);
    };
    switch (ztestMethod)
    {
    case 0:
        zpass = false;
        break;
    case 1:
        zpass = true;
        break;
    case 2:
        storedZ = readDepth();
        zpass = static_cast<uint32_t>(z) >= storedZ;
        break;
    case 3:
        storedZ = readDepth();
        zpass = static_cast<uint32_t>(z) > storedZ;
        break;
    }

    if (!zpass)
    {
        return false;
    }

    if (writeMask.writesFramebuffer())
    {
        const u8 srcR = r;
        const u8 srcG = g;
        const u8 srcB = b;

        if (state.prim.abe)
        {
            uint8_t dr = fbrgba & 0xFF;
            uint8_t dg = (fbrgba >> 8) & 0xFF;
            uint8_t db = (fbrgba >> 16) & 0xFF;
            uint8_t da = (fbrgba >> 24) & 0xFF;

            // PABE disables alpha blending when the source alpha MSB is clear.
            if (!(state.pabe && (a & 0x80u) == 0u))
            {
                const uint8_t asel = pipe.asel;
                const uint8_t bsel = pipe.bsel;
                const uint8_t csel = pipe.csel;
                const uint8_t dsel = pipe.dsel;
                const uint8_t fix = pipe.fix;

                auto pickRGB = [&](uint8_t sel, int cs, int cd) -> int
                {
                    if (sel == 0)
                        return cs;
                    if (sel == 1)
                        return cd;
                    return 0;
                };
                int cAlpha = (csel == 0) ? a : (csel == 1) ? da
                                                           : fix;

                r = clampU8(((pickRGB(asel, r, dr) - pickRGB(bsel, r, dr)) * cAlpha >> 7) + pickRGB(dsel, r, dr));
                g = clampU8(((pickRGB(asel, g, dg) - pickRGB(bsel, g, dg)) * cAlpha >> 7) + pickRGB(dsel, g, dg));
                b = clampU8(((pickRGB(asel, b, db) - pickRGB(bsel, b, db)) * cAlpha >> 7) + pickRGB(dsel, b, db));
            }
            else
            {
                r = srcR;
                g = srcG;
                b = srcB;
            }
        }

        if (writeMask.writeAlpha && pipe.fbaSetsAlpha)
        {
            a = static_cast<uint8_t>(a | 0x80u);
        }

        u32 pixel = pack32(r, g, b, a);

        if (ctx.frame.fbmsk != 0)
        {
            pixel = (pixel & ~ctx.frame.fbmsk) | (fbrgba & ctx.frame.fbmsk);
        }

        if (preserveDestinationAlpha)
        {
            pixel = (pixel & 0x00FFFFFFu) | (fbrgba & 0xFF000000u);
        }

        // format conversion
        if (pipe.framebuffer16)
        {
            pixel = Rgba8888ToRgba5551(pixel);
        }

        // PrepareTexturePageCache already made the texture page cache safe for this primitive's writes.
        if (pipe.frameOffset)
        {
            if (!framePixel)
                framePixel = m_vram + pipe.frameOffset(fbp, fbw, static_cast<u32>(x), static_cast<u32>(y));
            if (pipe.undo)
                RecordUndo(pipe, framePixel, x, y);
            writeTarget(fpsm, framePixel, pixel);
        }
        else
        {
            m_writeVramFuncs[fpsm & 0x3Fu](m_vram, fbp, fbw, x, y, pixel);
        }
    }

    if (writeMask.writeDepth && !ctx.zbuf.zmask)
    {
        if (pipe.depthOffset)
        {
            if (!depthPixel)
                depthPixel = m_vram + pipe.depthOffset(zbp, fbw, static_cast<u32>(x), static_cast<u32>(y));
            if (pipe.undo)
                RecordUndo(pipe, depthPixel, x, y);
            writeTarget(zpsm, depthPixel, static_cast<uint32_t>(z));
        }
        else
        {
            m_writeVramFuncs[zpsm & 0x3Fu](m_vram, zbp, fbw, x, y, z);
        }
    }
    return true;
}

GSCpuBackend::PixelPipeline GSCpuBackend::MakePixelPipeline(const GSDrawState &state, const RasterContext &raster)
{
    PixelPipeline pipe = MakePixelPipeline(state);
    pipe.undo = raster.undo;
    pipe.undoPages = raster.undoPages;
    pipe.primitive = raster.primitive;
    return pipe;
}

GSCpuBackend::PixelPipeline GSCpuBackend::MakePixelPipeline(const GSDrawState &state)
{
    const auto &ctx = state.context;
    PixelPipeline pipe{};
    pipe.state = &state;
    pipe.fge = state.prim.fge;
    pipe.fbp = GSInternal::framePageBaseToBlock(ctx.frame.fbp);
    pipe.fbw = std::max<u32>(ctx.frame.fbw, 1u);
    pipe.fpsm = ctx.frame.psm;
    pipe.zbp = GSInternal::framePageBaseToBlock(ctx.zbuf.zbp);
    pipe.zpsm = ctx.zbuf.psm;
    pipe.ztestMethod = static_cast<uint32_t>((ctx.test >> 17) & 3u);
    pipe.destinationAlphaTestNeedsRead = ((ctx.test >> 14) & 0x1u) != 0u &&
                                         (pipe.fpsm == GS_PSM_CT32 || pipe.fpsm == GS_PSM_CT16 || pipe.fpsm == GS_PSM_CT16S);
    pipe.framebuffer16 = bitsPerPixel(pipe.fpsm) == 16;
    pipe.fbaSetsAlpha = (ctx.fba & 0x1ull) != 0ull && ctx.frame.psm != GS_PSM_CT24;
    const uint64_t alphaReg = ctx.alpha;
    pipe.asel = alphaReg & 3;
    pipe.bsel = (alphaReg >> 2) & 3;
    pipe.csel = (alphaReg >> 4) & 3;
    pipe.dsel = (alphaReg >> 6) & 3;
    pipe.fix = static_cast<uint8_t>((alphaReg >> 32) & 0xFF);
    pipe.frameOffset = GSMem::TargetPixelOffset(pipe.fpsm);
    pipe.depthOffset = GSMem::TargetPixelOffset(pipe.zpsm);
    return pipe;
}

uint32_t GSCpuBackend::LookupCLUT(const GSDrawState &state,
                                  uint8_t index,
                                  uint8_t cpsm,
                                  uint8_t csm,
                                  uint8_t csa,
                                  uint8_t sourcePsm)
{
    const bool sixteenBit = cpsm == GS_PSM_CT16 || cpsm == GS_PSM_CT16S;
    const uint32_t csaMask = sixteenBit ? 0x1Fu : 0x0Fu;
    const uint32_t clutBase = (static_cast<uint32_t>(csa) & csaMask) << 4u;
    const uint32_t sourceIndex = isFourBitIndexedPsm(sourcePsm)
                                     ? (static_cast<uint32_t>(index) & 0x0Fu)
                                     : static_cast<uint32_t>(index);

    uint32_t clutIndex = (clutBase + sourceIndex) & (sixteenBit ? 0x1FFu : 0x0FFu);
    if (!sixteenBit && csm == 0u && isEightBitIndexedPsm(sourcePsm))
    {
        const uint32_t block = std::min((sourceIndex & 0xF0u) + clutBase, 240u);
        clutIndex = block + (sourceIndex & 0x0Fu);
    }

    switch (cpsm)
    {
    case GS_PSM_CT32:
    {
        const uint32_t raw = static_cast<uint32_t>(m_clut[clutIndex]) | (static_cast<uint32_t>(m_clut[clutIndex + 256u]) << 16u);
        return applyTexa(state.texa, cpsm, raw);
    }
    case GS_PSM_CT24:
    {
        const uint32_t raw = static_cast<uint32_t>(m_clut[clutIndex]) | (static_cast<uint32_t>(m_clut[clutIndex + 256u]) << 16u);
        return applyTexa(state.texa, cpsm, raw & 0x00FFFFFFu);
    }
    case GS_PSM_CT16:
    case GS_PSM_CT16S:
        return applyTexa(state.texa, cpsm, Rgba5551ToRgba8888(m_clut[clutIndex]));
    default:
        break;
    }

    return 0xFFFF00FFu;
}

void GSCpuBackend::ExpandClut(const GSDrawState &state, uint32_t *rgba)
{
    const auto &tex = state.context.tex0;
    for (uint32_t index = 0; index < 256u; ++index)
        rgba[index] = LookupCLUT(state, static_cast<u8>(index), tex.cpsm, tex.csm, tex.csa, tex.psm);
}

GSCpuBackend::TextureSampler GSCpuBackend::MakeTextureSampler(const GSDrawState &state, GSMem::TexturePageCache &cache, const uint32_t *clut)
{
    const auto &ctx = state.context;
    const auto &tex = ctx.tex0;
    TextureSampler sampler{};
    sampler.state = &state;
    sampler.psm = tex.psm;
    sampler.tbp0 = tex.tbp0;
    sampler.tbw = tex.tbw;
    sampler.texW = state.textureWidth;
    sampler.texH = state.textureHeight;
    const uint64_t clamp = ctx.clamp;
    sampler.wrapU = static_cast<uint8_t>(clamp & 0x3u);
    sampler.wrapV = static_cast<uint8_t>((clamp >> 2) & 0x3u);
    sampler.minU = static_cast<uint16_t>((clamp >> 4) & 0x3FFu);
    sampler.maxU = static_cast<uint16_t>((clamp >> 14) & 0x3FFu);
    sampler.minV = static_cast<uint16_t>((clamp >> 24) & 0x3FFu);
    sampler.maxV = static_cast<uint16_t>((clamp >> 34) & 0x3FFu);
    sampler.linear = state.linearFilter;
    sampler.cache = &cache;
    sampler.fetch = GSMem::TextureFetcher(tex.psm);
    sampler.fetchQuad = GSMem::TextureQuadFetcher(tex.psm);

    if (clut && (isFourBitIndexedPsm(tex.psm) || isEightBitIndexedPsm(tex.psm)))
    {
        sampler.clut = clut;
    }
    else if (isFourBitIndexedPsm(tex.psm) || isEightBitIndexedPsm(tex.psm))
    {
        // LookupCLUT depends only on these inputs and m_clut; expanding all 256 indices once lets the
        // per-texel path index a table. Kept per thread so concurrent rasterizers never share it.
        struct ExpandedClut
        {
            uint64_t generation = UINT64_MAX;
            uint8_t cpsm = 0, csm = 0, csa = 0, psm = 0, ta0 = 0, ta1 = 0;
            bool aem = false;
            std::array<uint32_t, 256> rgba{};
        };
        thread_local ExpandedClut expanded;
        if (expanded.generation != m_clutGeneration || expanded.cpsm != tex.cpsm ||
            expanded.csm != tex.csm || expanded.csa != tex.csa || expanded.psm != tex.psm ||
            expanded.ta0 != state.texa.ta0 || expanded.ta1 != state.texa.ta1 || expanded.aem != state.texa.aem)
        {
            expanded.generation = m_clutGeneration;
            expanded.cpsm = tex.cpsm;
            expanded.csm = tex.csm;
            expanded.csa = tex.csa;
            expanded.psm = tex.psm;
            expanded.ta0 = state.texa.ta0;
            expanded.ta1 = state.texa.ta1;
            expanded.aem = state.texa.aem;
            ExpandClut(state, expanded.rgba.data());
        }
        sampler.clut = expanded.rgba.data();
    }
    return sampler;
}

uint32_t GSCpuBackend::SampleTexture(const TextureSampler &sampler, float s, float t, float q, uint16_t u, uint16_t v)
{
    const int texW = sampler.texW;
    const int texH = sampler.texH;

    float texUf, texVf;
    if (sampler.state->prim.fst)
    {
        texUf = static_cast<float>(u) / 16.0f;
        texVf = static_cast<float>(v) / 16.0f;
    }
    else
    {
        const float invQ = 1.0f / fabsQ(q);
        texUf = s * invQ * static_cast<float>(texW);
        texVf = t * invQ * static_cast<float>(texH);
    }

    return SampleTextureAxes(sampler,
                             PrepareTextureAxis(texUf, texW, sampler.wrapU, sampler.minU, sampler.maxU, sampler.linear),
                             PrepareTextureAxis(texVf, texH, sampler.wrapV, sampler.minV, sampler.maxV, sampler.linear));
}

GSCpuBackend::TextureAxis GSCpuBackend::PrepareTextureAxis(float coordinate, int size, uint8_t mode, uint16_t regionMin, uint16_t regionMax, bool linear)
{
    TextureAxis axis{};
    if (!linear)
    {
        axis.c0 = wrapTextureCoordinate(static_cast<int>(coordinate), size, mode, regionMin, regionMax);
        return axis;
    }
    const float sample = coordinate - 0.5f;
    const int c0 = static_cast<int>(std::floor(sample));
    axis.frac = sample - static_cast<float>(c0);
    axis.c0 = wrapTextureCoordinate(c0, size, mode, regionMin, regionMax);
    axis.c1 = wrapTextureCoordinate(c0 + 1, size, mode, regionMin, regionMax);
    return axis;
}

__attribute__((always_inline)) inline uint32_t GSCpuBackend::SampleTexel(const TextureSampler &sampler, int u, int v)
{
    return ConvertTexel(sampler, sampler.fetch(*sampler.cache, m_vram, sampler.tbp0, sampler.tbw, static_cast<u32>(u), static_cast<u32>(v)));
}

__attribute__((always_inline)) inline uint32_t GSCpuBackend::ConvertTexel(const TextureSampler &sampler, uint32_t out)
{
    if (sampler.clut)
        return sampler.clut[static_cast<u8>(out)];

    switch (sampler.psm)
    {
    case GS_PSM_CT32:
    case GS_PSM_Z32:
    case GS_PSM_CT24:
    case GS_PSM_Z24:
        return applyTexa(sampler.state->texa, static_cast<uint8_t>(sampler.psm), out);
    case GS_PSM_CT16:
    case GS_PSM_CT16S:
    case GS_PSM_Z16:
    case GS_PSM_Z16S:
        return applyTexa(sampler.state->texa, static_cast<uint8_t>(sampler.psm), Rgba5551ToRgba8888(out));
    }

    return 0xFFFF00FFu;
}

uint32_t GSCpuBackend::SampleTextureAxes(const TextureSampler &sampler, const TextureAxis &u, const TextureAxis &v)
{
    if (!sampler.linear)
    {
        return SampleTexel(sampler, u.c0, v.c0);
    }

    const float fx = u.frac;
    const float fy = v.frac;
    uint32_t quad[4];
    sampler.fetchQuad(*sampler.cache, m_vram, sampler.tbp0, sampler.tbw, static_cast<u32>(u.c0), static_cast<u32>(u.c1),
                      static_cast<u32>(v.c0), static_cast<u32>(v.c1), quad);
    const uint32_t c00 = ConvertTexel(sampler, quad[0]);
    const uint32_t c10 = ConvertTexel(sampler, quad[1]);
    const uint32_t c01 = ConvertTexel(sampler, quad[2]);
    const uint32_t c11 = ConvertTexel(sampler, quad[3]);
    if (fx >= 0.0f && fx <= 1.0f && fy >= 0.0f && fy <= 1.0f)
        return lerpTexels(c00, c10, c01, c11, fx, fy);

    const uint8_t r = lerpChannel(static_cast<uint8_t>(c00 & 0xFFu),
                                  static_cast<uint8_t>(c10 & 0xFFu),
                                  static_cast<uint8_t>(c01 & 0xFFu),
                                  static_cast<uint8_t>(c11 & 0xFFu),
                                  fx, fy);
    const uint8_t g = lerpChannel(static_cast<uint8_t>((c00 >> 8) & 0xFFu),
                                  static_cast<uint8_t>((c10 >> 8) & 0xFFu),
                                  static_cast<uint8_t>((c01 >> 8) & 0xFFu),
                                  static_cast<uint8_t>((c11 >> 8) & 0xFFu),
                                  fx, fy);
    const uint8_t b = lerpChannel(static_cast<uint8_t>((c00 >> 16) & 0xFFu),
                                  static_cast<uint8_t>((c10 >> 16) & 0xFFu),
                                  static_cast<uint8_t>((c01 >> 16) & 0xFFu),
                                  static_cast<uint8_t>((c11 >> 16) & 0xFFu),
                                  fx, fy);
    const uint8_t a = lerpChannel(static_cast<uint8_t>((c00 >> 24) & 0xFFu),
                                  static_cast<uint8_t>((c10 >> 24) & 0xFFu),
                                  static_cast<uint8_t>((c01 >> 24) & 0xFFu),
                                  static_cast<uint8_t>((c11 >> 24) & 0xFFu),
                                  fx, fy);

    return static_cast<uint32_t>(r) |
           (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(a) << 24);
}

void GSCpuBackend::DrawSprite(const GSPrimitiveBatch &batch, const RasterContext &raster)
{
    const GSDrawState &state = batch.state;
    const GSVertex &v0 = batch.vertices[0];
    const GSVertex &v1 = batch.vertices[1];
    const auto &ctx = state.context;

    int ofx = ctx.xyoffset.ofx >> 4;
    int ofy = ctx.xyoffset.ofy >> 4;

    int x0 = static_cast<int>(v0.x) - ofx;
    int y0 = static_cast<int>(v0.y) - ofy;
    int x1 = static_cast<int>(v1.x) - ofx;
    int y1 = static_cast<int>(v1.y) - ofy;
    u32 z1 = static_cast<u32>(v1.z);

    if (x0 > x1)
        std::swap(x0, x1);
    if (y0 > y1)
        std::swap(y0, y1);

    const int unclippedX0 = x0;
    const int unclippedY0 = y0;
    const int spanX = std::max(1, x1 - x0);
    const int spanY = std::max(1, y1 - y0);
    const int unclippedX1 = unclippedX0 + spanX - 1;
    const int unclippedY1 = unclippedY0 + spanY - 1;

    // If the sprite rectangle is fully outside scissor, nothing should render.
    if (unclippedX1 < ctx.scissor.x0 || unclippedX0 > ctx.scissor.x1 ||
        unclippedY1 < ctx.scissor.y0 || unclippedY0 > ctx.scissor.y1)
        return;

    const int drawX0 = clampInt(unclippedX0, ctx.scissor.x0, ctx.scissor.x1);
    const int drawY0 = clampInt(unclippedY0, ctx.scissor.y0, ctx.scissor.y1);
    const int drawX1 = clampInt(unclippedX1, ctx.scissor.x0, ctx.scissor.x1);
    const int drawY1 = clampInt(unclippedY1, ctx.scissor.y0, ctx.scissor.y1);
    if (raster.firstRow(drawY0) > raster.lastRow(drawY1))
        return;

    const uint64_t alphaReg = ctx.alpha;
    const uint8_t alphaMode = static_cast<uint8_t>(alphaReg & 0xFFu);
    const uint8_t alphaFix = static_cast<uint8_t>((alphaReg >> 32) & 0xFFu);

    uint8_t r = v1.r, g = v1.g, b = v1.b, a = v1.a;
    uint64_t writtenPixels = 0u;

    if (state.prim.tme)
    {
        const auto &tex = ctx.tex0;
        const int texW = state.textureWidth;
        const int texH = state.textureHeight;

        float u0f, v0f, u1f, v1f;
        if (state.prim.fst)
        {
            u0f = static_cast<float>(v0.u >> 4);
            v0f = static_cast<float>(v0.v >> 4);
            u1f = static_cast<float>(v1.u >> 4);
            v1f = static_cast<float>(v1.v >> 4);
        }
        else
        {
            const float q0 = fabsQ(v0.q);
            const float q1 = fabsQ(v1.q);
            u0f = (v0.s / q0) * static_cast<float>(texW);
            v0f = (v0.t / q0) * static_cast<float>(texH);
            u1f = (v1.s / q1) * static_cast<float>(texW);
            v1f = (v1.t / q1) * static_cast<float>(texH);
        }

        float spriteW = static_cast<float>(spanX);
        float spriteH = static_cast<float>(spanY);
        if (spriteW < 1.0f)
            spriteW = 1.0f;
        if (spriteH < 1.0f)
            spriteH = 1.0f;

        // Texel coordinates depend only on the column or the row, so wrap them once per axis
        // with exactly the arithmetic SampleTexture applies to each pixel.
        const uint64_t clamp = ctx.clamp;
        const uint8_t wrapU = static_cast<uint8_t>(clamp & 0x3u);
        const uint8_t wrapV = static_cast<uint8_t>((clamp >> 2) & 0x3u);
        const uint16_t minU = static_cast<uint16_t>((clamp >> 4) & 0x3FFu);
        const uint16_t maxU = static_cast<uint16_t>((clamp >> 14) & 0x3FFu);
        const uint16_t minV = static_cast<uint16_t>((clamp >> 24) & 0x3FFu);
        const uint16_t maxV = static_cast<uint16_t>((clamp >> 34) & 0x3FFu);
        auto axisCoordinate = [&](float texF, int size) -> float
        {
            if (state.prim.fst)
            {
                const int fixed = static_cast<int>((texF * 16.0f) + 0.5f);
                const uint16_t sample = static_cast<uint16_t>(clampInt(fixed, 0, 0xFFFF));
                return static_cast<float>(sample) / 16.0f;
            }
            const float normalized = texF / static_cast<float>(size);
            const float invQ = 1.0f / fabsQ(1.0f);
            return normalized * invQ * static_cast<float>(size);
        };

        thread_local std::vector<TextureAxis> columns;
        columns.resize(static_cast<size_t>(drawX1 - drawX0 + 1));
        for (int x = drawX0; x <= drawX1; ++x)
        {
            float tx = (static_cast<float>(x - unclippedX0) + 0.5f) / spriteW;
            float texUf = u0f + (u1f - u0f) * tx;
            columns[static_cast<size_t>(x - drawX0)] =
                PrepareTextureAxis(axisCoordinate(texUf, texW), texW, wrapU, minU, maxU, state.linearFilter);
        }

        const PixelPipeline pipe = MakePixelPipeline(state, raster);
        const TextureSampler sampler = MakeTextureSampler(state, *raster.cache, raster.clut);
        for (int y = raster.firstRow(drawY0); y <= raster.lastRow(drawY1); ++y)
        {
            float ty = (static_cast<float>(y - unclippedY0) + 0.5f) / spriteH;
            float texVf = v0f + (v1f - v0f) * ty;
            const TextureAxis row = PrepareTextureAxis(axisCoordinate(texVf, texH), texH, wrapV, minV, maxV, state.linearFilter);

            for (int x = drawX0; x <= drawX1; ++x)
            {
                raster.beginSample(y, x);
                const uint32_t texel = SampleTextureAxes(sampler, columns[static_cast<size_t>(x - drawX0)], row);
                raster.sampled(y, x);

                uint8_t tr = static_cast<uint8_t>(texel & 0xFF);
                uint8_t tg = static_cast<uint8_t>((texel >> 8) & 0xFF);
                uint8_t tb = static_cast<uint8_t>((texel >> 16) & 0xFF);
                uint8_t ta = static_cast<uint8_t>((texel >> 24) & 0xFF);

                const TextureCombineResult color = combineTexture(tex, r, g, b, a, tr, tg, tb, ta);
                writtenPixels += WritePixel(pipe, x, y, z1, color.r, color.g, color.b, color.a, v1.fog) ? 1u : 0u;
            }
        }
    }
    else
    {
        const PixelPipeline pipe = MakePixelPipeline(state, raster);
        for (int y = raster.firstRow(drawY0); y <= raster.lastRow(drawY1); ++y)
        {
            for (int x = drawX0; x <= drawX1; ++x)
                writtenPixels += WritePixel(pipe, x, y, z1, r, g, b, a, v1.fog) ? 1u : 0u;
        }
    }
    if (raster.stats)
    {
        const uint64_t box = static_cast<uint64_t>(raster.lastRow(drawY1) - raster.firstRow(drawY0) + 1) *
                             static_cast<uint64_t>(drawX1 - drawX0 + 1);
        raster.stats->boxPixels += box;
        raster.stats->insidePixels += box;
        raster.stats->writtenPixels += writtenPixels;
        if (state.prim.tme)
            raster.stats->texSamples += box;
    }
}

void GSCpuBackend::DrawTriangle(const GSPrimitiveBatch &batch, const RasterContext &raster)
{
    const GSDrawState &state = batch.state;
    const GSVertex &v0 = batch.vertices[0];
    const GSVertex &v1 = batch.vertices[1];
    const GSVertex &v2 = batch.vertices[2];
    const auto &ctx = state.context;

    int ofx = ctx.xyoffset.ofx >> 4;
    int ofy = ctx.xyoffset.ofy >> 4;

    float fx0 = v0.x - static_cast<float>(ofx);
    float fy0 = v0.y - static_cast<float>(ofy);
    float fx1 = v1.x - static_cast<float>(ofx);
    float fy1 = v1.y - static_cast<float>(ofy);
    float fx2 = v2.x - static_cast<float>(ofx);
    float fy2 = v2.y - static_cast<float>(ofy);

    int minX = static_cast<int>(std::floor(std::min({fx0, fx1, fx2})));
    int maxX = static_cast<int>(std::ceil(std::max({fx0, fx1, fx2})));
    int minY = static_cast<int>(std::floor(std::min({fy0, fy1, fy2})));
    int maxY = static_cast<int>(std::ceil(std::max({fy0, fy1, fy2})));

    minX = clampInt(minX, ctx.scissor.x0, ctx.scissor.x1);
    maxX = clampInt(maxX, ctx.scissor.x0, ctx.scissor.x1);
    minY = clampInt(minY, ctx.scissor.y0, ctx.scissor.y1);
    maxY = clampInt(maxY, ctx.scissor.y0, ctx.scissor.y1);
    if (raster.firstRow(minY) > raster.lastRow(maxY))
        return;
    const PixelPipeline pipe = MakePixelPipeline(state, raster);
    const TextureSampler sampler = state.prim.tme ? MakeTextureSampler(state, *raster.cache, raster.clut) : TextureSampler{};

    float denom = (fy1 - fy2) * (fx0 - fx2) + (fx2 - fx1) * (fy0 - fy2);
    if (std::fabs(denom) < 0.001f)
        return;

    const float winding = (denom < 0.0f) ? -1.0f : 1.0f;
    const float invAbsDenom = 1.0f / std::fabs(denom);
    constexpr float kEdgeEpsilon = 1.0e-4f;

    // Each weight is linear in px along a row. Solving the three edge tests in double, widened by more than
    // the float evaluation can be off, bounds the columns worth testing; the exact per-pixel test below still
    // decides coverage, so the drawn pixels are unchanged while most of the bounding box is skipped.
    struct EdgeBound
    {
        double a = 0.0;   // weight change per pixel
        double b = 0.0;   // weight at px = 0 for the current row
        double err = 0.0; // bound on the float evaluation error of the weight
    };
    const double scale = static_cast<double>(winding) * static_cast<double>(invAbsDenom);
    const double extentX = static_cast<double>(std::max(std::fabs(static_cast<double>(minX) - fx2), std::fabs(static_cast<double>(maxX) + 1.0 - fx2)));
    auto rowEdge = [&](double A, double B, double py) -> EdgeBound
    {
        const double C = B * (py - fy2);
        EdgeBound edge;
        edge.a = A * scale;
        edge.b = (C - A * fx2) * scale;
        edge.err = (std::fabs(A) * extentX + std::fabs(C) + 1.0) * 1.0e-6 * std::fabs(scale) + 1.0e-6;
        return edge;
    };
    // Narrows [lo, hi] to the columns where a*px + b >= -eps could hold, px = x + 0.5.
    auto clipToEdge = [&](const EdgeBound &edge, int &lo, int &hi)
    {
        const double threshold = -static_cast<double>(kEdgeEpsilon) - edge.err - edge.b;
        if (std::fabs(edge.a) < 1.0e-12)
        {
            if (threshold > 0.0)
                hi = lo - 1;
            return;
        }
        const double boundary = threshold / edge.a - 0.5;
        if (!(std::fabs(boundary) < 1.0e9))
        {
            // Beyond any column: the whole row is on one side of this edge.
            if ((edge.a > 0.0) == (boundary > 0.0))
                hi = lo - 1;
            return;
        }
        const double margin = 2.0;
        if (edge.a > 0.0)
            lo = std::max(lo, static_cast<int>(std::floor(boundary - margin)));
        else
            hi = std::min(hi, static_cast<int>(std::ceil(boundary + margin)));
    };

    uint64_t insidePixels = 0u;
    uint64_t writtenPixels = 0u;
    uint64_t testedPixels = 0u;
    for (int y = raster.firstRow(minY); y <= raster.lastRow(maxY); ++y)
    {
        float py = static_cast<float>(y) + 0.5f;
        int spanX0 = minX;
        int spanX1 = maxX;
        {
            const double pyd = static_cast<double>(py);
            const EdgeBound e0 = rowEdge(static_cast<double>(fy1 - fy2), static_cast<double>(fx2 - fx1), pyd);
            const EdgeBound e1 = rowEdge(static_cast<double>(fy2 - fy0), static_cast<double>(fx0 - fx2), pyd);
            EdgeBound e2;
            e2.a = -(e0.a + e1.a);
            e2.b = 1.0 - e0.b - e1.b;
            e2.err = e0.err + e1.err + 1.0e-6;
            clipToEdge(e0, spanX0, spanX1);
            clipToEdge(e1, spanX0, spanX1);
            clipToEdge(e2, spanX0, spanX1);
        }
        if (spanX0 > spanX1)
            continue;
        testedPixels += static_cast<uint64_t>(spanX1 - spanX0 + 1);
        if (raster.batched && state.prim.tme)
        {
            // Pixel by pixel, every texel waits on its own interpolation, division and fetch, and the write
            // waits on the texel: one long dependency chain per pixel. Running each stage over a chunk of the
            // row lets independent pixels overlap. Batched primitives never sample anything their batch's
            // writes change, so sampling a chunk before writing it reads the same texels, in the same order.
            constexpr int kChunk = 32;
            int xs[kChunk];
            float w0s[kChunk];
            float w1s[kChunk];
            float w2s[kChunk];
            uint32_t texels[kChunk];
            TextureAxis axesU[kChunk];
            TextureAxis axesV[kChunk];
            for (int chunkX = spanX0; chunkX <= spanX1; chunkX += kChunk)
            {
                const int chunkEnd = std::min(spanX1, chunkX + kChunk - 1);
                int count = 0;
                for (int x = chunkX; x <= chunkEnd; ++x)
                {
                    float px = static_cast<float>(x) + 0.5f;
                    float w0 = (((fy1 - fy2) * (px - fx2) + (fx2 - fx1) * (py - fy2)) * winding) * invAbsDenom;
                    float w1 = (((fy2 - fy0) * (px - fx2) + (fx0 - fx2) * (py - fy2)) * winding) * invAbsDenom;
                    float w2 = 1.0f - w0 - w1;
                    xs[count] = x;
                    w0s[count] = w0;
                    w1s[count] = w1;
                    w2s[count] = w2;
                    count += (w0 < -kEdgeEpsilon || w1 < -kEdgeEpsilon || w2 < -kEdgeEpsilon) ? 0 : 1;
                }
                insidePixels += static_cast<uint64_t>(count);
                // Texel coordinates exactly as SampleTexture derives them.
                const float texWf = static_cast<float>(sampler.texW);
                const float texHf = static_cast<float>(sampler.texH);
                for (int i = 0; i < count; ++i)
                {
                    const float w0 = w0s[i], w1 = w1s[i], w2 = w2s[i];
                    float texUf, texVf;
                    if (state.prim.fst)
                    {
                        const uint16_t iu = static_cast<uint16_t>(v0.u * w0 + v1.u * w1 + v2.u * w2);
                        const uint16_t iv = static_cast<uint16_t>(v0.v * w0 + v1.v * w1 + v2.v * w2);
                        texUf = static_cast<float>(iu) / 16.0f;
                        texVf = static_cast<float>(iv) / 16.0f;
                    }
                    else
                    {
                        const float is = v0.s * w0 + v1.s * w1 + v2.s * w2;
                        const float it = v0.t * w0 + v1.t * w1 + v2.t * w2;
                        const float iq = v0.q * w0 + v1.q * w1 + v2.q * w2;
                        const float invQ = 1.0f / fabsQ(iq);
                        texUf = is * invQ * texWf;
                        texVf = it * invQ * texHf;
                    }
                    axesU[i] = PrepareTextureAxis(texUf, sampler.texW, sampler.wrapU, sampler.minU, sampler.maxU, sampler.linear);
                    axesV[i] = PrepareTextureAxis(texVf, sampler.texH, sampler.wrapV, sampler.minV, sampler.maxV, sampler.linear);
                }
                for (int i = 0; i < count; ++i)
                {
                    raster.beginSample(y, xs[i]);
                    texels[i] = SampleTextureAxes(sampler, axesU[i], axesV[i]);
                    raster.sampled(y, xs[i]);
                }
                for (int i = 0; i < count; ++i)
                {
                    const float w0 = w0s[i], w1 = w1s[i], w2 = w2s[i];
                    const double z = v0.z * w0 + v1.z * w1 + v2.z * w2;
                    uint8_t r, g, b, a;
                    if (state.prim.iip)
                    {
                        r = clampU8(static_cast<int>(v0.r * w0 + v1.r * w1 + v2.r * w2));
                        g = clampU8(static_cast<int>(v0.g * w0 + v1.g * w1 + v2.g * w2));
                        b = clampU8(static_cast<int>(v0.b * w0 + v1.b * w1 + v2.b * w2));
                        a = clampU8(static_cast<int>(v0.a * w0 + v1.a * w1 + v2.a * w2));
                    }
                    else
                    {
                        r = v2.r;
                        g = v2.g;
                        b = v2.b;
                        a = v2.a;
                    }
                    const uint32_t texel = texels[i];
                    const TextureCombineResult color = combineTexture(ctx.tex0, r, g, b, a,
                                                                      static_cast<uint8_t>(texel & 0xFF), static_cast<uint8_t>((texel >> 8) & 0xFF),
                                                                      static_cast<uint8_t>((texel >> 16) & 0xFF), static_cast<uint8_t>((texel >> 24) & 0xFF));
                    const uint8_t fog = clampU8(static_cast<int>(v0.fog * w0 + v1.fog * w1 + v2.fog * w2));
                    writtenPixels += WritePixel(pipe, xs[i], y, static_cast<u32>(z + 0.5), color.r, color.g, color.b, color.a, fog) ? 1u : 0u;
                }
            }
            continue;
        }
        for (int x = spanX0; x <= spanX1; ++x)
        {
            float px = static_cast<float>(x) + 0.5f;

            float w0 = (((fy1 - fy2) * (px - fx2) + (fx2 - fx1) * (py - fy2)) * winding) * invAbsDenom;
            float w1 = (((fy2 - fy0) * (px - fx2) + (fx0 - fx2) * (py - fy2)) * winding) * invAbsDenom;
            float w2 = 1.0f - w0 - w1;

            if (w0 < -kEdgeEpsilon || w1 < -kEdgeEpsilon || w2 < -kEdgeEpsilon)
                continue;
            ++insidePixels;

            double z = v0.z * w0 + v1.z * w1 + v2.z * w2;

            uint8_t r, g, b, a;
            if (state.prim.iip)
            {
                r = clampU8(static_cast<int>(v0.r * w0 + v1.r * w1 + v2.r * w2));
                g = clampU8(static_cast<int>(v0.g * w0 + v1.g * w1 + v2.g * w2));
                b = clampU8(static_cast<int>(v0.b * w0 + v1.b * w1 + v2.b * w2));
                a = clampU8(static_cast<int>(v0.a * w0 + v1.a * w1 + v2.a * w2));
            }
            else
            {
                r = v2.r;
                g = v2.g;
                b = v2.b;
                a = v2.a;
            }

            if (state.prim.tme)
            {
                float is, it, iq;
                uint16_t iu, iv;
                if (state.prim.fst)
                {
                    iu = static_cast<uint16_t>(v0.u * w0 + v1.u * w1 + v2.u * w2);
                    iv = static_cast<uint16_t>(v0.v * w0 + v1.v * w1 + v2.v * w2);
                    is = 0.0f;
                    it = 0.0f;
                    iq = 1.0f;
                }
                else
                {
                    // The GS DDA interpolates the homogeneous S, T and Q
                    // values. Texel coordinates are calculated from S/Q and
                    // T/Q only after interpolation.
                    is = v0.s * w0 + v1.s * w1 + v2.s * w2;
                    it = v0.t * w0 + v1.t * w1 + v2.t * w2;
                    iq = v0.q * w0 + v1.q * w1 + v2.q * w2;
                    iu = 0;
                    iv = 0;
                }

                raster.beginSample(y, x);
                uint32_t texel = SampleTexture(sampler, is, it, iq, iu, iv);
                raster.sampled(y, x);

                uint8_t tr = static_cast<uint8_t>(texel & 0xFF);
                uint8_t tg = static_cast<uint8_t>((texel >> 8) & 0xFF);
                uint8_t tb = static_cast<uint8_t>((texel >> 16) & 0xFF);
                uint8_t ta = static_cast<uint8_t>((texel >> 24) & 0xFF);

                const auto &tex = ctx.tex0;
                const uint8_t shadeR = r;
                const uint8_t shadeG = g;
                const uint8_t shadeB = b;
                const uint8_t shadeA = a;
                const TextureCombineResult color = combineTexture(tex, shadeR, shadeG, shadeB, shadeA, tr, tg, tb, ta);

                r = color.r;
                g = color.g;
                b = color.b;
                a = color.a;
            }

            const uint8_t fog = clampU8(static_cast<int>(v0.fog * w0 + v1.fog * w1 + v2.fog * w2));
            writtenPixels += WritePixel(pipe, x, y, static_cast<u32>(z + 0.5), r, g, b, a, fog) ? 1u : 0u;
        }
    }
    if (raster.stats)
    {
        // Pixels the edge test ran on, after the per-row column bounds.
        const uint64_t box = testedPixels;
        raster.stats->boxPixels += box;
        raster.stats->insidePixels += insidePixels;
        raster.stats->writtenPixels += writtenPixels;
        if (state.prim.tme)
            raster.stats->texSamples += insidePixels;
        if (static_cast<uint64_t>(maxY - minY + 1) * static_cast<uint64_t>(maxX - minX + 1) >= 65536u)
        {
            raster.stats->bigBoxPixels += box;
            raster.stats->bigInsidePixels += insidePixels;
        }
    }
}

void GSCpuBackend::DrawLine(const GSPrimitiveBatch &batch, const RasterContext &raster)
{
    const GSDrawState &state = batch.state;
    const GSVertex &v0 = batch.vertices[0];
    const GSVertex &v1 = batch.vertices[1];
    const auto &ctx = state.context;
    const PixelPipeline pipe = MakePixelPipeline(state, raster);

    int ofx = ctx.xyoffset.ofx >> 4;
    int ofy = ctx.xyoffset.ofy >> 4;

    int x0 = static_cast<int>(v0.x) - ofx;
    int y0 = static_cast<int>(v0.y) - ofy;
    int x1 = static_cast<int>(v1.x) - ofx;
    int y1 = static_cast<int>(v1.y) - ofy;

    int dx = std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    int totalSteps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    if (totalSteps == 0)
        totalSteps = 1;
    int step = 0;

    for (;;)
    {
        float t = static_cast<float>(step) / static_cast<float>(totalSteps);
        uint8_t r, g, b, a;
        if (state.prim.iip)
        {
            r = clampU8(static_cast<int>(v0.r + (v1.r - v0.r) * t));
            g = clampU8(static_cast<int>(v0.g + (v1.g - v0.g) * t));
            b = clampU8(static_cast<int>(v0.b + (v1.b - v0.b) * t));
            a = clampU8(static_cast<int>(v0.a + (v1.a - v0.a) * t));
        }
        else
        {
            r = v1.r;
            g = v1.g;
            b = v1.b;
            a = v1.a;
        }

        double z = (v0.z + (v1.z - v0.z) * t);
        const uint8_t fog = clampU8(static_cast<int>(v0.fog + (v1.fog - v0.fog) * t));
        if (raster.ownsRow(y0))
            WritePixel(pipe, x0, y0, static_cast<u32>(z), r, g, b, a, fog);

        if (x0 == x1 && y0 == y1)
            break;

        int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
        ++step;
    }
}

void GSCpuBackend::BeginTransferNow(const GSTransferCommand &command)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend *>(this)->FlushPendingUnlocked(FlushReason::Transfer);
    m_transfer = command;
    m_transferState.x = command.trxpos.dsax;
    m_transferState.y = command.trxpos.dsay;
    m_transferState.totalPixels = static_cast<uint32_t>(command.trxreg.rrw) * static_cast<uint32_t>(command.trxreg.rrh);
    m_transferState.copiedPixels = 0u;
    m_transferState.direction = command.direction;
    m_transferState.localToHostPendingBytes = 0u;
    m_uploadCarryBytes = 0u;

    if (command.direction == 2u)
        PerformLocalToLocalTransfer();
    else if (command.direction == 1u)
        PerformLocalToHostTransfer();
}

void GSCpuBackend::UploadImageNow(const uint8_t *data, uint32_t sizeBytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend *>(this)->FlushPendingUnlocked(FlushReason::Transfer);
    if (!data || sizeBytes == 0u || !m_vram || m_transferState.direction != 0u)
        return;
    if (m_transfer.trxreg.rrw == 0u || m_transfer.trxreg.rrh == 0u || m_transferState.totalPixels == 0u)
        return;

    const uint32_t dbp = m_transfer.bitbltbuf.dbp;
    const uint32_t dbw = std::max<uint32_t>(m_transfer.bitbltbuf.dbw, 1u);
    const uint8_t dpsm = m_transfer.bitbltbuf.dpsm;
    const uint32_t rrw = m_transfer.trxreg.rrw;
    const uint32_t dsax = m_transfer.trxpos.dsax;
    uint32_t offset = 0u;
    // Track the column/row of copiedPixels incrementally; a divide per uploaded pixel showed up in movie playback.
    uint32_t column = m_transferState.copiedPixels % rrw;
    uint32_t row = m_transferState.copiedPixels / rrw;

    auto advancePixel = [&](uint32_t count)
    {
        const uint32_t totalPixels = m_transferState.totalPixels;
        m_transferState.copiedPixels =
            std::min<uint32_t>(totalPixels, m_transferState.copiedPixels + count);

        if (m_transferState.copiedPixels >= totalPixels)
        {
            m_transferState.direction = 3u;
            m_transferState.totalPixels = 0u;
            return;
        }

        column += count;
        while (column >= rrw)
        {
            column -= rrw;
            ++row;
        }
        m_transferState.x = dsax + column;
        m_transferState.y = m_transfer.trxpos.dsay + row;
    };

    uint32_t bytesPerPixel = 0u;
    switch (dpsm)
    {
    case GS_PSM_CT32:
    case GS_PSM_Z32:
        bytesPerPixel = 4u;
        break;
    case GS_PSM_CT24:
    case GS_PSM_Z24:
        bytesPerPixel = 3u;
        break;
    case GS_PSM_CT16:
    case GS_PSM_CT16S:
    case GS_PSM_Z16:
    case GS_PSM_Z16S:
        bytesPerPixel = 2u;
        break;
    default:
        break;
    }

    if (bytesPerPixel != 0u)
    {
        auto writeDirectPixel = [&](const uint8_t *bytes)
        {
            uint32_t value = 0u;
            std::memcpy(&value, bytes, bytesPerPixel);
            WriteVramUnlocked(dpsm, dbp, dbw, m_transferState.x, m_transferState.y, value);
            advancePixel(1u);
        };

        if (m_uploadCarryBytes != 0u)
        {
            const uint32_t take = std::min<uint32_t>(bytesPerPixel - m_uploadCarryBytes, sizeBytes);
            std::memcpy(m_uploadCarry.data() + m_uploadCarryBytes, data, take);
            m_uploadCarryBytes += take;
            offset = take;
            if (m_uploadCarryBytes < bytesPerPixel)
                return;
            m_uploadCarryBytes = 0u;
            writeDirectPixel(m_uploadCarry.data());
        }

        while (m_transferState.direction == 0u && sizeBytes - offset >= bytesPerPixel)
        {
            writeDirectPixel(data + offset);
            offset += bytesPerPixel;
        }

        if (m_transferState.direction == 0u && offset < sizeBytes)
        {
            m_uploadCarryBytes = sizeBytes - offset;
            std::memcpy(m_uploadCarry.data(), data + offset, m_uploadCarryBytes);
        }
        return;
    }

    while (offset < sizeBytes && m_transferState.direction == 0u)
    {
        switch (dpsm)
        {
        case GS_PSM_T8:
        case GS_PSM_T8H:
            WriteVramUnlocked(dpsm, dbp, dbw, m_transferState.x, m_transferState.y, data[offset++]);
            advancePixel(1u);
            break;
        case GS_PSM_T4:
        case GS_PSM_T4HL:
        case GS_PSM_T4HH:
        {
            const uint8_t packed = data[offset++];
            const uint32_t firstPixel = m_transferState.copiedPixels;
            WriteVramUnlocked(dpsm, dbp, dbw,
                              dsax + column,
                              m_transfer.trxpos.dsay + row,
                              packed & 0x0Fu);
            if (firstPixel + 1u < m_transferState.totalPixels)
            {
                const bool wraps = column + 1u >= rrw;
                WriteVramUnlocked(dpsm, dbp, dbw,
                                  dsax + (wraps ? column + 1u - rrw : column + 1u),
                                  m_transfer.trxpos.dsay + (wraps ? row + 1u : row),
                                  (packed >> 4u) & 0x0Fu);
            }
            advancePixel(std::min<uint32_t>(2u, m_transferState.totalPixels - firstPixel));
            break;
        }
        default:
            return;
        }
    }
}

void GSCpuBackend::PerformLocalToLocalTransfer()
{
    if (!m_vram)
        return;

    const uint32_t rrw = m_transfer.trxreg.rrw;
    const uint32_t rrh = m_transfer.trxreg.rrh;
    const uint32_t total = rrw * rrh;
    if (total == 0u)
    {
        m_transferState.direction = 3u;
        return;
    }

    for (uint32_t pixel = 0; pixel < total; ++pixel)
    {
        uint32_t x = pixel % rrw;
        uint32_t y = pixel / rrw;
        if ((m_transfer.trxpos.dir & 0x2u) != 0u)
            x = rrw - x - 1u;
        if ((m_transfer.trxpos.dir & 0x1u) != 0u)
            y = rrh - y - 1u;

        const uint32_t value = ReadVramUnlocked(m_transfer.bitbltbuf.spsm,
                                                m_transfer.bitbltbuf.sbp,
                                                std::max<uint32_t>(m_transfer.bitbltbuf.sbw, 1u),
                                                x + m_transfer.trxpos.ssax,
                                                y + m_transfer.trxpos.ssay);
        WriteVramUnlocked(m_transfer.bitbltbuf.dpsm,
                          m_transfer.bitbltbuf.dbp,
                          std::max<uint32_t>(m_transfer.bitbltbuf.dbw, 1u),
                          x + m_transfer.trxpos.dsax,
                          y + m_transfer.trxpos.dsay,
                          value);
    }

    m_transferState.copiedPixels = total;
    m_transferState.direction = 3u;
}

void GSCpuBackend::PerformLocalToHostTransfer()
{
    m_localToHostBuffer.clear();
    m_localToHostReadPos = 0u;
    if (!m_vram)
        return;

    const uint32_t rrw = m_transfer.trxreg.rrw;
    const uint32_t rrh = m_transfer.trxreg.rrh;
    const uint32_t sbw = std::max<uint32_t>(m_transfer.bitbltbuf.sbw, 1u);
    const uint8_t spsm = m_transfer.bitbltbuf.spsm;
    const uint32_t bpp = static_cast<uint32_t>(GSMem::BitsPerPixel(static_cast<GSMem::PixelStorageMode>(spsm)));
    const uint32_t total = rrw * rrh;
    m_localToHostBuffer.reserve((static_cast<size_t>(total) * bpp + 7u) / 8u);

    for (uint32_t pixel = 0u; pixel < total; ++pixel)
    {
        const uint32_t x = pixel % rrw;
        const uint32_t y = pixel / rrw;
        const uint32_t value = ReadVramUnlocked(spsm,
                                                m_transfer.bitbltbuf.sbp,
                                                sbw,
                                                x + m_transfer.trxpos.ssax,
                                                y + m_transfer.trxpos.ssay);
        switch (bpp)
        {
        case 32:
            m_localToHostBuffer.push_back(static_cast<uint8_t>(value));
            m_localToHostBuffer.push_back(static_cast<uint8_t>(value >> 8u));
            m_localToHostBuffer.push_back(static_cast<uint8_t>(value >> 16u));
            m_localToHostBuffer.push_back(static_cast<uint8_t>(value >> 24u));
            break;
        case 24:
            m_localToHostBuffer.push_back(static_cast<uint8_t>(value));
            m_localToHostBuffer.push_back(static_cast<uint8_t>(value >> 8u));
            m_localToHostBuffer.push_back(static_cast<uint8_t>(value >> 16u));
            break;
        case 16:
            m_localToHostBuffer.push_back(static_cast<uint8_t>(value));
            m_localToHostBuffer.push_back(static_cast<uint8_t>(value >> 8u));
            break;
        case 8:
            m_localToHostBuffer.push_back(static_cast<uint8_t>(value));
            break;
        case 4:
        {
            if ((pixel & 1u) != 0u)
                break;
            uint32_t next = 0u;
            if (pixel + 1u < total)
            {
                const uint32_t nextPixel = pixel + 1u;
                const uint32_t nextX = nextPixel % rrw;
                const uint32_t nextY = nextPixel / rrw;
                next = ReadVramUnlocked(spsm, m_transfer.bitbltbuf.sbp, sbw,
                                        nextX + m_transfer.trxpos.ssax,
                                        nextY + m_transfer.trxpos.ssay);
            }
            m_localToHostBuffer.push_back(static_cast<uint8_t>((value & 0x0Fu) | ((next & 0x0Fu) << 4u)));
            break;
        }
        default:
            break;
        }
    }

    m_transferState.copiedPixels = total;
    m_transferState.localToHostPendingBytes = m_localToHostBuffer.size();
}

uint32_t GSCpuBackend::ConsumeLocalToHostBytesNow(uint8_t *dst, uint32_t maxBytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend *>(this)->FlushPendingUnlocked(FlushReason::Readback);
    if (!dst || maxBytes == 0u || m_localToHostReadPos >= m_localToHostBuffer.size())
        return 0u;
    const size_t count = std::min<size_t>(maxBytes, m_localToHostBuffer.size() - m_localToHostReadPos);
    std::memcpy(dst, m_localToHostBuffer.data() + m_localToHostReadPos, count);
    m_localToHostReadPos += count;
    m_transferState.localToHostPendingBytes = m_localToHostBuffer.size() - m_localToHostReadPos;
    return static_cast<uint32_t>(count);
}

bool GSCpuBackend::ClearFramebufferNow(const GSContext &context, uint32_t rgba)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend *>(this)->FlushPendingUnlocked(FlushReason::Other);
    if (!m_vram || context.frame.fbw == 0u)
        return false;

    const uint32_t x0 = context.scissor.x0;
    const uint32_t x1 = std::max<uint32_t>(x0, context.scissor.x1);
    const uint32_t y0 = context.scissor.y0;
    const uint32_t y1 = std::max<uint32_t>(y0, context.scissor.y1);
    uint8_t r = static_cast<uint8_t>(rgba);
    uint8_t g = static_cast<uint8_t>(rgba >> 8u);
    uint8_t b = static_cast<uint8_t>(rgba >> 16u);
    uint8_t a = static_cast<uint8_t>(rgba >> 24u);
    if ((context.fba & 1ull) != 0ull && context.frame.psm != GS_PSM_CT24)
        a |= 0x80u;

    const uint32_t fbp = GSInternal::framePageBaseToBlock(context.frame.fbp);
    const uint32_t fbw = std::max<uint32_t>(context.frame.fbw, 1u);
    if (context.frame.psm == GS_PSM_CT32 || context.frame.psm == GS_PSM_CT24)
    {
        const uint32_t source = static_cast<uint32_t>(r) |
                                (static_cast<uint32_t>(g) << 8u) |
                                (static_cast<uint32_t>(b) << 16u) |
                                (static_cast<uint32_t>(a) << 24u);
        for (uint32_t y = y0; y <= y1; ++y)
            for (uint32_t x = x0; x <= x1; ++x)
            {
                uint32_t pixel = source;
                if (context.frame.fbmsk != 0u)
                {
                    const uint32_t old = ReadVramUnlocked(context.frame.psm, fbp, fbw, x, y);
                    pixel = (pixel & ~context.frame.fbmsk) | (old & context.frame.fbmsk);
                }
                WriteVramUnlocked(context.frame.psm, fbp, fbw, x, y, pixel);
            }
        return true;
    }

    if (context.frame.psm == GS_PSM_CT16 || context.frame.psm == GS_PSM_CT16S)
    {
        const uint16_t source = encodeFramePixelPSMCT16(r, g, b, a);
        const uint16_t mask = static_cast<uint16_t>(context.frame.fbmsk);
        for (uint32_t y = y0; y <= y1; ++y)
            for (uint32_t x = x0; x <= x1; ++x)
            {
                uint16_t pixel = source;
                if (mask != 0u)
                {
                    const uint16_t old = static_cast<uint16_t>(ReadVramUnlocked(context.frame.psm, fbp, fbw, x, y));
                    pixel = static_cast<uint16_t>((pixel & ~mask) | (old & mask));
                }
                WriteVramUnlocked(context.frame.psm, fbp, fbw, x, y, pixel);
            }
        return true;
    }
    return false;
}

bool GSCpuBackend::CopyFrameToHostRgba(const GSFrameReg &frame,
                                       uint32_t width,
                                       uint32_t height,
                                       std::vector<uint8_t> &outPixels,
                                       bool preserveAlpha,
                                       bool useLocalMemoryLayout,
                                       bool frameBaseIsPages,
                                       uint32_t sourceOriginX,
                                       uint32_t sourceOriginY) const
{
    if (!m_vram || m_vramSize == 0u)
        return false;

    outPixels.assign(kHostFrameWidth * kHostFrameHeight * 4u, 0u);
    const uint32_t baseBytes = frameBaseIsPages ? frame.fbp * 8192u : frame.fbp * 256u;
    const uint32_t basePtr = frameBaseIsPages ? GSInternal::framePageBaseToBlock(frame.fbp) : frame.fbp;
    const uint32_t fbw = frame.fbw ? frame.fbw : kHostFrameWidth / 64u;
    const uint32_t bytesPerPixel = (frame.psm == GS_PSM_CT16 || frame.psm == GS_PSM_CT16S) ? 2u : 4u;
    const uint32_t stride = fbw * 64u * bytesPerPixel;

    for (uint32_t y = 0; y < height; ++y)
    {
        uint8_t *dst = outPixels.data() + y * kHostFrameWidth * 4u;
        for (uint32_t x = 0; x < width; ++x)
        {
            const uint32_t sx = sourceOriginX + x;
            const uint32_t sy = sourceOriginY + y;
            if (frame.psm == GS_PSM_CT32 || frame.psm == GS_PSM_CT24)
            {
                uint32_t color = 0u;
                if (useLocalMemoryLayout)
                    color = ReadVramUnlocked(frame.psm, basePtr, fbw, sx, sy);
                else
                {
                    const uint32_t pixelBytes = frame.psm == GS_PSM_CT24 ? 3u : 4u;
                    const uint64_t offset = static_cast<uint64_t>(baseBytes) + static_cast<uint64_t>(sy) * stride + static_cast<uint64_t>(sx) * pixelBytes;
                    if (offset + pixelBytes > m_vramSize)
                        return false;
                    color = m_vram[offset] | (static_cast<uint32_t>(m_vram[offset + 1u]) << 8u) |
                            (static_cast<uint32_t>(m_vram[offset + 2u]) << 16u);
                    if (pixelBytes == 4u)
                        color |= static_cast<uint32_t>(m_vram[offset + 3u]) << 24u;
                }
                dst[x * 4u] = static_cast<uint8_t>(color);
                dst[x * 4u + 1u] = static_cast<uint8_t>(color >> 8u);
                dst[x * 4u + 2u] = static_cast<uint8_t>(color >> 16u);
                dst[x * 4u + 3u] = preserveAlpha && frame.psm != GS_PSM_CT24 ? static_cast<uint8_t>(color >> 24u) : 255u;
            }
            else if (frame.psm == GS_PSM_CT16 || frame.psm == GS_PSM_CT16S)
            {
                uint16_t color = 0u;
                if (useLocalMemoryLayout)
                    color = static_cast<uint16_t>(ReadVramUnlocked(frame.psm, basePtr, fbw, sx, sy));
                else
                {
                    const uint64_t offset = static_cast<uint64_t>(baseBytes) + static_cast<uint64_t>(sy) * stride + static_cast<uint64_t>(sx) * 2u;
                    if (offset + 2u > m_vramSize)
                        return false;
                    std::memcpy(&color, m_vram + offset, sizeof(color));
                }
                const uint32_t r = color & 31u;
                const uint32_t g = (color >> 5u) & 31u;
                const uint32_t b = (color >> 10u) & 31u;
                dst[x * 4u] = static_cast<uint8_t>((r << 3u) | (r >> 2u));
                dst[x * 4u + 1u] = static_cast<uint8_t>((g << 3u) | (g >> 2u));
                dst[x * 4u + 2u] = static_cast<uint8_t>((b << 3u) | (b >> 2u));
                dst[x * 4u + 3u] = preserveAlpha ? ((color & 0x8000u) ? 0x80u : 0u) : 255u;
            }
            else
            {
                outPixels.clear();
                return false;
            }
        }
    }
    return true;
}

PresentationFrame GSCpuBackend::Present(const GSPresentationRequest &request)
{
    // Snapshot local memory under the backend lock, then perform the expensive
    // display conversion without holding the producer-side raster lock.
    thread_local std::vector<uint8_t> snapshot;
    GSPresentationRequest shown = request;
    bool behind = false;
    if (m_async)
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        behind = m_queueCompleted < m_queueSubmitted;
    }
    bool captured = false;
    if (behind)
    {
        // Live memory would pair the guest's current DISPFB with a buffer still being drawn.
        std::lock_guard<std::mutex> lock(m_captureMutex);
        if (!m_capturedVram.empty())
        {
            snapshot = m_capturedVram;
            shown.pmode = m_capturedDisplay.pmode;
            shown.smode2 = m_capturedDisplay.smode2;
            shown.dispfb1 = m_capturedDisplay.dispfb1;
            shown.display1 = m_capturedDisplay.display1;
            shown.dispfb2 = m_capturedDisplay.dispfb2;
            shown.display2 = m_capturedDisplay.display2;
            shown.bgcolor = m_capturedDisplay.bgcolor;
            shown.vsyncTick = m_capturedDisplay.vsyncTick;
            captured = true;
        }
    }
    if (!captured)
        SnapshotVramNow(snapshot);
    if (snapshot.empty())
        return {};

    thread_local GSCpuBackend snapshotBackend;
    snapshotBackend.Initialize(snapshot.data(), static_cast<uint32_t>(snapshot.size()));
    return snapshotBackend.PresentFromLocalMemory(shown);
}

PresentationFrame GSCpuBackend::PresentFromLocalMemory(const GSPresentationRequest &request)
{
    PresentationFrame result{};
    const GSPmodeState pmode = decodePmode(request.pmode);
    const GSSmode2State smode2 = decodeSMode2(request.smode2);
    const bool fieldMode = smode2.interlaced && !smode2.frameMode;
    const bool oddField = (request.vsyncTick & 1ull) != 0ull;
    const GSFrameReg displayFrame1 = decodeDisplayFrame(request.dispfb1);
    const GSFrameReg displayFrame2 = decodeDisplayFrame(request.dispfb2);
    const GSDisplayReadOrigin origin1 = decodeDisplayReadOrigin(request.dispfb1);
    const GSDisplayReadOrigin origin2 = decodeDisplayReadOrigin(request.dispfb2);
    uint32_t width1 = 0u, height1 = 0u, width2 = 0u, height2 = 0u;
    decodeDisplaySize(request.display1, width1, height1);
    decodeDisplaySize(request.display2, width2, height2);
    const bool valid1 = pmode.enableCrt1 && hasDisplaySetup(request.display1, displayFrame1);
    const bool valid2 = pmode.enableCrt2 && hasDisplaySetup(request.display2, displayFrame2);
    if (!valid1 && !valid2)
        return result;

    auto copySource = [&](const GSFrameReg &displayFrame,
                          const GSDisplayReadOrigin &origin,
                          uint32_t width,
                          uint32_t height,
                          bool allowPreferred,
                          bool preserveAlpha,
                          GSFrameReg &selected,
                          std::vector<uint8_t> &pixels,
                          bool &usedPreferred) -> bool
    {
        selected = displayFrame;
        pixels.clear();
        usedPreferred = false;
        if (allowPreferred && request.hasPreferredSource && request.preferredDestFbp == displayFrame.fbp &&
            (request.preferredSource.fbw != 0u || request.preferredSource.fbp != displayFrame.fbp) &&
            CopyFrameToHostRgba(request.preferredSource, width, height, pixels, preserveAlpha, true, false, 0u, 0u))
        {
            selected = request.preferredSource;
            usedPreferred = true;
        }
        if (pixels.empty() && !CopyFrameToHostRgba(displayFrame, width, height, pixels, preserveAlpha, true, true, origin.x, origin.y))
            return false;

        if (!usedPreferred && displayFrame.fbp == 0u && countNonBlackPixels(pixels, width, height) == 0u)
        {
            for (const GSFrameReg &candidate : request.contextFrames)
            {
                if (candidate.fbp == selected.fbp && candidate.fbw == selected.fbw && candidate.psm == selected.psm)
                    continue;
                std::vector<uint8_t> candidatePixels;
                if (!CopyFrameToHostRgba(candidate, width, height, candidatePixels, preserveAlpha, true, true, 0u, 0u))
                    continue;
                if (countNonBlackPixels(candidatePixels, width, height) == 0u)
                    continue;
                selected = candidate;
                pixels.swap(candidatePixels);
                break;
            }
        }
        return true;
    };

    if (valid1 && valid2)
    {
        GSFrameReg selected1{}, selected2{};
        std::vector<uint8_t> crt1, crt2;
        bool preferred1 = false, preferred2 = false;
        if (copySource(displayFrame1, origin1, width1, height1, false, true, selected1, crt1, preferred1) &&
            copySource(displayFrame2, origin2, width2, height2, false, true, selected2, crt2, preferred2))
        {
            result.width = std::max(width1, width2);
            result.height = std::max(height1, height2);
            result.pixels.assign(kHostFrameWidth * kHostFrameHeight * 4u, 0u);
            const uint8_t bgR = static_cast<uint8_t>(request.bgcolor);
            const uint8_t bgG = static_cast<uint8_t>(request.bgcolor >> 8u);
            const uint8_t bgB = static_cast<uint8_t>(request.bgcolor >> 16u);
            for (uint32_t y = 0; y < result.height; ++y)
                for (uint32_t x = 0; x < result.width; ++x)
                {
                    uint8_t *dst = result.pixels.data() + (y * kHostFrameWidth + x) * 4u;
                    dst[0] = bgR;
                    dst[1] = bgG;
                    dst[2] = bgB;
                    dst[3] = pmode.alp;
                }
            if (!pmode.slbg)
                for (uint32_t y = 0; y < height2; ++y)
                    std::memcpy(result.pixels.data() + y * kHostFrameWidth * 4u, crt2.data() + y * kHostFrameWidth * 4u, width2 * 4u);
            for (uint32_t y = 0; y < height1; ++y)
                for (uint32_t x = 0; x < width1; ++x)
                {
                    const uint8_t *src = crt1.data() + (y * kHostFrameWidth + x) * 4u;
                    uint8_t *dst = result.pixels.data() + (y * kHostFrameWidth + x) * 4u;
                    const uint32_t factor = pmode.mmod ? pmode.alp : std::min<uint32_t>(255u, static_cast<uint32_t>(src[3]) * 2u);
                    dst[0] = blendPresentationChannel(src[0], dst[0], factor);
                    dst[1] = blendPresentationChannel(src[1], dst[1], factor);
                    dst[2] = blendPresentationChannel(src[2], dst[2], factor);
                    dst[3] = pmode.amod ? dst[3] : src[3];
                }
            normalizePresentationAlpha(result.pixels, result.width, result.height);
            if (fieldMode)
                applyFieldPresentation(result.pixels, result.width, result.height, oddField);
            result.displayFbp = displayFrame1.fbp;
            result.sourceFbp = selected1.fbp;
            return result;
        }
    }

    const GSFrameReg &displayFrame = valid1 ? displayFrame1 : displayFrame2;
    const GSDisplayReadOrigin &origin = valid1 ? origin1 : origin2;
    result.width = valid1 ? width1 : width2;
    result.height = valid1 ? height1 : height2;
    GSFrameReg selected = displayFrame;
    if (!copySource(displayFrame, origin, result.width, result.height, true, false, selected, result.pixels, result.usedPreferred))
        return {};
    if (fieldMode)
        applyFieldPresentation(result.pixels, result.width, result.height, oddField);
    normalizePresentationAlpha(result.pixels, result.width, result.height);
    result.displayFbp = displayFrame.fbp;
    result.sourceFbp = selected.fbp;
    return result;
}
