#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/ps2_gs_common.h"
#include "runtime/gs/ps2_gs_psmct16.h"
#include "runtime/gs/ps2_gs_psmct32.h"
#include "runtime/gs/ps2_gs_psmt4.h"
#include "runtime/gs/ps2_gs_psmt8.h"
#include "runtime/gs/ps2_gs_memory.h"
#include "ps2_log.h"
#include <atomic>
#include <algorithm>
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
            const uint32_t cores = std::max(1u, std::thread::hardware_concurrency());
            return std::clamp(cores / 2u, 1u, 6u);
        }();
        return participants;
    }

    constexpr size_t kMaxPendingPrimitives = 8192u;
    constexpr uint64_t kMaxPendingPixels = 1u << 22u;
    // Below this, waking the workers costs more than drawing the batch on the calling thread.
    constexpr uint64_t kMinParallelPixels = 16384u;

    void markPages(std::bitset<512> &pages, const PageSpan &span)
    {
        for (uint32_t i = 0; i < span.count; ++i)
            pages.set((span.first + i) % 512u);
    }

    bool anyPage(const std::bitset<512> &pages, const PageSpan &span)
    {
        for (uint32_t i = 0; i < span.count; ++i)
            if (pages.test((span.first + i) % 512u))
                return true;
        return false;
    }
}

GSCpuBackend::~GSCpuBackend() = default;

void GSCpuBackend::SetRasterThreads(uint32_t participants)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    FlushPendingUnlocked();
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
    const PageSpan frameSpan = pageSpan(GSInternal::framePageBaseToBlock(ctx.frame.fbp), fbw, ctx.frame.psm, 0u, 0u, maxX, maxY);
    const PageSpan depthSpan = pageSpan(GSInternal::framePageBaseToBlock(ctx.zbuf.zbp), fbw, ctx.zbuf.psm, 0u, 0u, maxX, maxY);
    const PageSpan &frame = frameSpan;
    const PageSpan &depth = depthSpan;
    PageSpan texture{};
    if (state.prim.tme)
    {
        const auto &tex = ctx.tex0;
        const uint32_t extentU = (ctx.clamp & 0x2u) != 0u ? 1024u : std::max<uint32_t>(state.textureWidth, 1u);
        const uint32_t extentV = ((ctx.clamp >> 2) & 0x2u) != 0u ? 1024u : std::max<uint32_t>(state.textureHeight, 1u);
        const PageSpan span = pageSpan(tex.tbp0, tex.tbw, tex.psm, 0u, 0u, extentU - 1u, extentV - 1u);
        texture = span;
    }

    // Pixels of one primitive map to distinct memory only if colour and depth do not alias each other,
    // and rows can be split only if the primitive never samples what it draws.
    const bool selfOverlap = spansOverlap(frameSpan, depthSpan) ||
                             (texture.count != 0u && (spansOverlap(texture, frameSpan) || spansOverlap(texture, depthSpan)));
    if (selfOverlap)
    {
        FlushPendingUnlocked();
        DrawPrimitive(batch);
        return;
    }

    // Every queued primitive must address colour and depth the same way, so a pixel's bytes stay on the
    // row band that owns the pixel.
    const uint64_t targetKey = (static_cast<uint64_t>(ctx.frame.fbp) << 0u) ^ (static_cast<uint64_t>(fbw) << 12u) ^
                               (static_cast<uint64_t>(ctx.frame.psm) << 20u) ^ (static_cast<uint64_t>(ctx.zbuf.zbp) << 28u) ^
                               (static_cast<uint64_t>(ctx.zbuf.psm) << 40u) ^ (1ull << 63u);
    if (!m_pending.empty() &&
        (targetKey != m_pendingTargetKey || anyPage(m_pendingWritePages, texture) ||
         anyPage(m_pendingTexturePages, frame) || anyPage(m_pendingTexturePages, depth)))
        FlushPendingUnlocked();

    m_pendingTargetKey = targetKey;
    markPages(m_pendingWritePages, frame);
    markPages(m_pendingWritePages, depth);
    markPages(m_pendingTexturePages, texture);
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
    const uint64_t width = static_cast<uint64_t>(maxX >= ctx.scissor.x0 ? maxX - ctx.scissor.x0 + 1u : 1u);
    const uint64_t height = static_cast<uint64_t>(maxY >= ctx.scissor.y0 ? maxY - ctx.scissor.y0 + 1u : 1u);
    uint64_t estimate = width * height;
    if (batch.vertexCount >= 2u)
    {
        float minX = batch.vertices[0].x, maxXf = batch.vertices[0].x, minY = batch.vertices[0].y, maxYf = batch.vertices[0].y;
        for (uint32_t i = 1; i < std::min<uint32_t>(batch.vertexCount, 3u); ++i)
        {
            minX = std::min(minX, batch.vertices[i].x);
            maxXf = std::max(maxXf, batch.vertices[i].x);
            minY = std::min(minY, batch.vertices[i].y);
            maxYf = std::max(maxYf, batch.vertices[i].y);
        }
        const double area = (static_cast<double>(maxXf) - minX + 1.0) * (static_cast<double>(maxYf) - minY + 1.0);
        if (area >= 0.0 && area < static_cast<double>(estimate))
            estimate = static_cast<uint64_t>(area);
    }
    m_pendingPixels += estimate;
    if (m_pending.size() >= kMaxPendingPrimitives || m_pendingPixels >= kMaxPendingPixels)
        FlushPendingUnlocked();
}

void GSCpuBackend::FlushPendingUnlocked()
{
    if (m_pending.empty())
        return;

    bool parallel = m_pendingPixels >= kMinParallelPixels && RasterParticipants() > 1u;
    uint32_t livePage = 0u;
    if (parallel && !m_texturePageCache.LivePage(livePage) && m_texturePageCache.CachedPage(livePage))
    {
        // Workers read pages in place, which matches a copied page only if VRAM still holds the same bytes.
        if (!m_texturePageCache.MatchesVram(m_vram))
            parallel = false;
        else
            m_texturePageCache.MakeLive();
    }

    if (parallel)
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
    m_pending.clear();
    m_pendingCluts.clear();
    m_pendingClutIndex.clear();
    m_pendingClutGeneration = UINT64_MAX;
    m_pendingClutReadIndex = 0u;
    m_pendingWritePages.reset();
    m_pendingTexturePages.reset();
    m_pendingPixels = 0u;
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
    const uint32_t participants = m_workers->participants();

    std::vector<GSMem::TexturePageCache> caches(participants);
    std::vector<RasterTrace> traces(participants);
    const std::function<void(uint32_t)> job = [&](uint32_t index)
    {
        RasterContext raster{};
        raster.workers = participants;
        raster.index = index;
        raster.cache = &caches[index];
        raster.trace = &traces[index];
        for (size_t i = 0; i < m_pending.size(); ++i)
        {
            raster.primitive = static_cast<uint32_t>(i);
            const int32_t clutIndex = m_pendingClutIndex[i];
            raster.clut = clutIndex >= 0 ? m_pendingCluts[static_cast<size_t>(clutIndex)].data() : nullptr;
            DrawPrimitiveRows(m_pending[i], raster);
        }
    };
    m_workers->run(job);

    // Sequentially, the cache would end on the page of the last texel read: the last sampled pixel in
    // primitive then raster order, which lies on exactly one band.
    int best = -1;
    for (uint32_t i = 0; i < participants; ++i)
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

void GSCpuBackend::Initialize(uint8_t *vram, uint32_t vramSize)
{
    if (vram && vramSize < GSMem::MEMORY_SIZE)
        throw std::invalid_argument("GS CPU backend requires at least 4 MiB of VRAM");

    std::lock_guard<std::mutex> lock(m_mutex);
    // A new VRAM binding makes queued primitives meaningless; nothing may still refer to the old memory.
    m_pending.clear();
    m_pendingCluts.clear();
    m_pendingClutIndex.clear();
    m_pendingClutGeneration = UINT64_MAX;
    m_pendingClutReadIndex = 0u;
    m_pendingWritePages.reset();
    m_pendingTexturePages.reset();
    m_pendingPixels = 0u;
    m_vram = vram;
    m_vramSize = vramSize;
    ResetUnlocked();
}

void GSCpuBackend::Reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend *>(this)->FlushPendingUnlocked();
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

void GSCpuBackend::Submit(const GSPrimitiveBatch &batch)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_vram || batch.vertexCount == 0u)
        return;
    if (RasterParticipants() > 1u)
        QueuePrimitive(batch);
    else
        DrawPrimitive(batch);
}

void GSCpuBackend::LoadClut(const GSTex0Reg &tex0, const GSTexClutReg &texclut)
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
            FlushPendingUnlocked();
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

    LoadClutUnlocked(tex0, texclut);
    // LoadClutUnlocked reads VRAM (moving the texture page cache) only for 16- and 32-bit palettes.
    if (tex0.cpsm == GS_PSM_CT16 || tex0.cpsm == GS_PSM_CT16S || tex0.cpsm == GS_PSM_CT32 || tex0.cpsm == GS_PSM_CT24)
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

void GSCpuBackend::Flush()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    FlushPendingUnlocked();
}

void GSCpuBackend::TextureFlush()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend *>(this)->FlushPendingUnlocked();
    m_texturePageCache.Invalidate();
}

void GSCpuBackend::Sync(GSSyncReason)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    FlushPendingUnlocked();
}

uint32_t GSCpuBackend::ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend *>(this)->FlushPendingUnlocked();
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

void GSCpuBackend::WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend *>(this)->FlushPendingUnlocked();
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

void GSCpuBackend::SnapshotVram(std::vector<uint8_t> &out) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend *>(this)->FlushPendingUnlocked();
    if (!m_vram || m_vramSize == 0u)
    {
        out.clear();
        return;
    }
    out.resize(m_vramSize);
    std::memcpy(out.data(), m_vram, m_vramSize);
}

GSTransferSnapshot GSCpuBackend::GetTransferSnapshot() const
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
    DrawPrimitiveRows(batch, raster);
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
};

void GSCpuBackend::WritePixel(const GSDrawState &state, int x, int y, int z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint8_t fog)
{
    WritePixel(MakePixelPipeline(state), x, y, z, r, g, b, a, fog);
}

void GSCpuBackend::WritePixel(const PixelPipeline &pipe, int x, int y, int z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint8_t fog)
{
    const GSDrawState &state = *pipe.state;
    const auto &ctx = state.context;
    if (x < ctx.scissor.x0 || x > ctx.scissor.x1 || y < ctx.scissor.y0 || y > ctx.scissor.y1)
        return;

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
        return;
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
    if (frmw)
    {
        rawFramebufferPixel = ReadVramUnlocked(fpsm, fbp, fbw, x, y);
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
        return;
    }

    bool zpass = false;
    uint32_t storedZ = 0u;
    switch (ztestMethod)
    {
    case 0:
        zpass = false;
        break;
    case 1:
        zpass = true;
        break;
    case 2:
        storedZ = ReadVramUnlocked(zpsm, zbp, fbw, x, y);
        zpass = static_cast<uint32_t>(z) >= storedZ;
        break;
    case 3:
        storedZ = ReadVramUnlocked(zpsm, zbp, fbw, x, y);
        zpass = static_cast<uint32_t>(z) > storedZ;
        break;
    }

    if (!zpass)
    {
        return;
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
        m_writeVramFuncs[fpsm & 0x3Fu](m_vram, fbp, fbw, x, y, pixel);
    }

    if (writeMask.writeDepth && !ctx.zbuf.zmask)
    {
        m_writeVramFuncs[zpsm & 0x3Fu](m_vram, zbp, fbw, x, y, z);
    }
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

uint32_t GSCpuBackend::SampleTexel(const TextureSampler &sampler, int u, int v)
{
    const u32 out = GSMem::ReadTexture(*sampler.cache, m_vram, sampler.psm, sampler.tbp0, sampler.tbw, u, v);
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
    const uint32_t c00 = SampleTexel(sampler, u.c0, v.c0);
    const uint32_t c10 = SampleTexel(sampler, u.c1, v.c0);
    const uint32_t c01 = SampleTexel(sampler, u.c0, v.c1);
    const uint32_t c11 = SampleTexel(sampler, u.c1, v.c1);

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

    const uint64_t alphaReg = ctx.alpha;
    const uint8_t alphaMode = static_cast<uint8_t>(alphaReg & 0xFFu);
    const uint8_t alphaFix = static_cast<uint8_t>((alphaReg >> 32) & 0xFFu);

    uint8_t r = v1.r, g = v1.g, b = v1.b, a = v1.a;

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

        const PixelPipeline pipe = MakePixelPipeline(state);
        const TextureSampler sampler = MakeTextureSampler(state, *raster.cache, raster.clut);
        for (int y = drawY0; y <= drawY1; ++y)
        {
            if (!raster.ownsRow(y))
                continue;
            float ty = (static_cast<float>(y - unclippedY0) + 0.5f) / spriteH;
            float texVf = v0f + (v1f - v0f) * ty;
            const TextureAxis row = PrepareTextureAxis(axisCoordinate(texVf, texH), texH, wrapV, minV, maxV, state.linearFilter);

            for (int x = drawX0; x <= drawX1; ++x)
            {
                const uint32_t texel = SampleTextureAxes(sampler, columns[static_cast<size_t>(x - drawX0)], row);
                raster.sampled(y, x);

                uint8_t tr = static_cast<uint8_t>(texel & 0xFF);
                uint8_t tg = static_cast<uint8_t>((texel >> 8) & 0xFF);
                uint8_t tb = static_cast<uint8_t>((texel >> 16) & 0xFF);
                uint8_t ta = static_cast<uint8_t>((texel >> 24) & 0xFF);

                const TextureCombineResult color = combineTexture(tex, r, g, b, a, tr, tg, tb, ta);
                WritePixel(pipe, x, y, z1, color.r, color.g, color.b, color.a, v1.fog);
            }
        }
    }
    else
    {
        const PixelPipeline pipe = MakePixelPipeline(state);
        for (int y = drawY0; y <= drawY1; ++y)
        {
            if (!raster.ownsRow(y))
                continue;
            for (int x = drawX0; x <= drawX1; ++x)
                WritePixel(pipe, x, y, z1, r, g, b, a, v1.fog);
        }
    }
}

void GSCpuBackend::DrawTriangle(const GSPrimitiveBatch &batch, const RasterContext &raster)
{
    const GSDrawState &state = batch.state;
    const GSVertex &v0 = batch.vertices[0];
    const GSVertex &v1 = batch.vertices[1];
    const GSVertex &v2 = batch.vertices[2];
    const auto &ctx = state.context;
    const PixelPipeline pipe = MakePixelPipeline(state);
    const TextureSampler sampler = state.prim.tme ? MakeTextureSampler(state, *raster.cache, raster.clut) : TextureSampler{};

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

    float denom = (fy1 - fy2) * (fx0 - fx2) + (fx2 - fx1) * (fy0 - fy2);
    if (std::fabs(denom) < 0.001f)
        return;

    const float winding = (denom < 0.0f) ? -1.0f : 1.0f;
    const float invAbsDenom = 1.0f / std::fabs(denom);
    constexpr float kEdgeEpsilon = 1.0e-4f;

    for (int y = minY; y <= maxY; ++y)
    {
        if (!raster.ownsRow(y))
            continue;
        float py = static_cast<float>(y) + 0.5f;
        for (int x = minX; x <= maxX; ++x)
        {
            float px = static_cast<float>(x) + 0.5f;

            float w0 = (((fy1 - fy2) * (px - fx2) + (fx2 - fx1) * (py - fy2)) * winding) * invAbsDenom;
            float w1 = (((fy2 - fy0) * (px - fx2) + (fx0 - fx2) * (py - fy2)) * winding) * invAbsDenom;
            float w2 = 1.0f - w0 - w1;

            if (w0 < -kEdgeEpsilon || w1 < -kEdgeEpsilon || w2 < -kEdgeEpsilon)
                continue;

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
            WritePixel(pipe, x, y, static_cast<u32>(z + 0.5), r, g, b, a, fog);
        }
    }
}

void GSCpuBackend::DrawLine(const GSPrimitiveBatch &batch, const RasterContext &raster)
{
    const GSDrawState &state = batch.state;
    const GSVertex &v0 = batch.vertices[0];
    const GSVertex &v1 = batch.vertices[1];
    const auto &ctx = state.context;
    const PixelPipeline pipe = MakePixelPipeline(state);

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

void GSCpuBackend::BeginTransfer(const GSTransferCommand &command)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend *>(this)->FlushPendingUnlocked();
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

void GSCpuBackend::UploadImage(const uint8_t *data, uint32_t sizeBytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend *>(this)->FlushPendingUnlocked();
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

uint32_t GSCpuBackend::ConsumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend *>(this)->FlushPendingUnlocked();
    if (!dst || maxBytes == 0u || m_localToHostReadPos >= m_localToHostBuffer.size())
        return 0u;
    const size_t count = std::min<size_t>(maxBytes, m_localToHostBuffer.size() - m_localToHostReadPos);
    std::memcpy(dst, m_localToHostBuffer.data() + m_localToHostReadPos, count);
    m_localToHostReadPos += count;
    m_transferState.localToHostPendingBytes = m_localToHostBuffer.size() - m_localToHostReadPos;
    return static_cast<uint32_t>(count);
}

bool GSCpuBackend::ClearFramebuffer(const GSContext &context, uint32_t rgba)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const_cast<GSCpuBackend *>(this)->FlushPendingUnlocked();
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
    SnapshotVram(snapshot);
    if (snapshot.empty())
        return {};

    thread_local GSCpuBackend snapshotBackend;
    snapshotBackend.Initialize(snapshot.data(), static_cast<uint32_t>(snapshot.size()));
    return snapshotBackend.PresentFromLocalMemory(request);
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
